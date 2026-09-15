#include "eng/scene/SceneSerializer.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include "eng/log/Macros.hpp"
#include "eng/math/Quat.hpp"
#include "eng/math/Transform.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/serial/Json.hpp"

ENG_LOG_CATEGORY("scene");

// =============================================================================
// Registro reflect dos tipos math usados pelos componentes persistidos
// (nomes QUALIFICADOS = chaves estáveis entre builds — ADR-033). Registro
// idempotente por nome (primeiro vence — ADR-021), seguro em múltiplas TUs.
// =============================================================================

ENG_REFLECT_BEGIN(eng::math::Vec3)
    ENG_REFLECT_FIELD(x)
    ENG_REFLECT_FIELD(y)
    ENG_REFLECT_FIELD(z)
ENG_REFLECT_END()

ENG_REFLECT_BEGIN(eng::math::Quat)
    ENG_REFLECT_FIELD(x)
    ENG_REFLECT_FIELD(y)
    ENG_REFLECT_FIELD(z)
    ENG_REFLECT_FIELD(w)
ENG_REFLECT_END()

ENG_REFLECT_BEGIN(eng::math::Transform)
    ENG_REFLECT_FIELD_AS(position, "eng::math::Vec3")
    ENG_REFLECT_FIELD_AS(rotation, "eng::math::Quat")
    ENG_REFLECT_FIELD_AS(scale, "eng::math::Vec3")
ENG_REFLECT_END()

namespace eng::scene::detail {

namespace {

/// Registro global de componentes serializáveis. std::map → iteração
/// ORDENADA POR NOME (determinismo dos componentes por tipo, ADR-033).
/// Registro esperado em init single-threaded (ADR-034).
std::map<std::string, ComponentEntry>& componentRegistry()
{
    static std::map<std::string, ComponentEntry> registry;
    return registry;
}

/// Componentes INTERNOS da Scene: tolerados no JSON com WARN (escritores
/// externos podem vazar) — a representação canônica é "parent"/"id".
[[nodiscard]] bool isInternalComponentName(const std::string& name)
{
    return name == "eng::scene::SceneIdentity" ||
           name == "eng::scene::Hierarchy" ||
           name == "eng::scene::WorldMatrix" ||
           name == "SceneIdentity" || name == "Hierarchy" ||
           name == "WorldMatrix";
}

} // namespace

void registerComponentEntry(std::string typeName, ComponentEntry entry)
{
    componentRegistry()[std::move(typeName)] = entry;
}

const std::map<std::string, ComponentEntry>& componentEntries()
{
    return componentRegistry();
}

} // namespace eng::scene::detail

namespace eng::scene {

namespace {

// --- built-ins: registrado no carregamento do módulo ------------------------
// (SceneSerializer.cpp só entra no link se alguém usa o serializer —
// garantia de que o registro acompanha o uso.)
const bool eng_scene_builtin_components_registered = [] {
    (void)SceneSerializer::registerComponentType<eng::math::Transform>(
        "eng::math::Transform");
    return true;
}();

using eng::core::Error;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

[[nodiscard]] Error sceneError(StatusCode code, std::string message)
{
    return Error{code, "SceneSerializer: " + std::move(message)};
}

/// Registro coletado por nó durante o save.
struct NodeRecord {
    eng::ecs::Entity entity{};
    SceneEntityId id;
    eng::ecs::Entity parent{}; ///< kNoEntity = raiz
};

} // namespace

// =============================================================================
// save
// =============================================================================

eng::core::Result<std::string> SceneSerializer::save(Scene& scene)
{
    // 1. Enumerar nós (achado crítico 2: todo nó tem Hierarchy; each
    //    itera o pool com snapshot mutável-seguro — emplace de
    //    SceneIdentity durante a iteração é seguro, ADR-024).
    std::vector<NodeRecord> records;
    std::unordered_map<eng::ecs::Entity, SceneEntityId> idOf;
    scene.world().each<Hierarchy>(
        [&](eng::ecs::Entity e, const Hierarchy& hierarchy) {
            NodeRecord record;
            record.entity = e;
            record.parent = hierarchy.parent;
            if (const SceneIdentity* identity =
                    scene.world().get<SceneIdentity>(e)) {
                record.id = identity->id;
            } else {
                record.id = SceneEntityId::generate();
                // primeira serialização: identidade persistente
                (void)scene.world().emplace<SceneIdentity>(
                    e, SceneIdentity{record.id});
            }
            records.push_back(record);
        });

    for (const NodeRecord& record : records) {
        idOf[record.entity] = record.id;
    }

    // 2. Componentes (entradas ordenadas por nome — ADR-033).
    struct EntityJson {
        SceneEntityId id;
        eng::serial::JsonValue json;
    };
    const auto& entries = detail::componentEntries();

    std::vector<EntityJson> entities;
    entities.reserve(records.size());
    for (const NodeRecord& record : records) {
        eng::serial::JsonValue components = eng::serial::JsonValue::array();
        for (const auto& [name, entry] : entries) {
            if (!entry.has(scene.world(), record.entity)) {
                continue;
            }
            auto encoded =
                entry.encode(entry, scene.world(), record.entity);
            if (encoded.isError()) {
                return makeUnexpected(encoded.error());
            }
            eng::serial::JsonValue component = eng::serial::JsonValue::object();
            component.set("type", eng::serial::JsonValue::string(name));
            component.set("data", std::move(encoded.value()));
            components.append(std::move(component));
        }

        eng::serial::JsonValue entity = eng::serial::JsonValue::object();
        entity.set("id", eng::serial::JsonValue::string(record.id.toString()));
        const auto parentId = idOf.find(record.parent);
        if (record.parent != kNoEntity && parentId != idOf.end()) {
            entity.set("parent",
                       eng::serial::JsonValue::string(
                           parentId->second.toString()));
        } else {
            // Raiz — inclusive pai obsoleto por bypass do world
            // (política de ADR-025: tratado como raiz).
            entity.set("parent", eng::serial::JsonValue::null());
        }
        entity.set("components", std::move(components));
        entities.push_back(EntityJson{record.id, std::move(entity)});
    }

    // 3. Ordem canônica: entidades por SceneEntityId (hi, lo).
    std::sort(entities.begin(), entities.end(),
              [](const EntityJson& a, const EntityJson& b) {
                  return a.id < b.id;
              });

    eng::serial::JsonValue ids = eng::serial::JsonValue::array();
    eng::serial::JsonValue entitiesArray = eng::serial::JsonValue::array();
    for (EntityJson& entity : entities) {
        ids.append(eng::serial::JsonValue::string(entity.id.toString()));
        entitiesArray.append(std::move(entity.json));
    }

    eng::serial::JsonValue root = eng::serial::JsonValue::object();
    root.set("formatVersion",
             eng::serial::JsonValue::uinteger(kFormatVersion));
    root.set("sceneEntityIds", std::move(ids));
    root.set("entities", std::move(entitiesArray));
    return eng::serial::dumpJson(root);
}

// =============================================================================
// load
// =============================================================================

eng::core::Result<void> SceneSerializer::load(Scene& scene,
                                              std::string_view text)
{
    const auto parsed = eng::serial::parseJson(text);
    if (parsed.isError()) {
        return makeUnexpected(parsed.error());
    }
    const eng::serial::JsonValue& root = parsed.value();
    if (!root.isObject()) {
        return makeUnexpected(
            sceneError(StatusCode::ParseError, "raiz não é objeto"));
    }

    const auto format = root.find("formatVersion");
    if (!format.has_value() || !format->isUnsigned() ||
        format->asU64() > kFormatVersion) {
        return makeUnexpected(sceneError(
            StatusCode::NotSupported,
            "formatVersion ausente/inválida/maior que a suportada (" +
                std::to_string(kFormatVersion) + ")"));
    }

    // sceneEntityIds: lista canônica (consistência validada contra entities)
    std::vector<SceneEntityId> declaredIds;
    const auto declared = root.find("sceneEntityIds");
    if (declared.has_value() && declared->isArray()) {
        for (std::size_t i = 0; i < declared->size(); ++i) {
            const auto& entry = declared->at(i);
            if (!entry.isString()) {
                return makeUnexpected(sceneError(
                    StatusCode::ParseError,
                    "sceneEntityIds[" + std::to_string(i) +
                        "] não é string"));
            }
            auto id = SceneEntityId::fromString(entry.asString());
            if (id.isError()) {
                return makeUnexpected(id.error());
            }
            declaredIds.push_back(id.value());
        }
    }

    const auto entitiesField = root.find("entities");
    if (!entitiesField.has_value() || !entitiesField->isArray()) {
        return makeUnexpected(sceneError(StatusCode::ParseError,
                                         "'entities' ausente ou não-array"));
    }

    struct LoadedEntity {
        eng::ecs::Entity entity{};
        SceneEntityId id;
        SceneEntityId parent;
        bool hasParent = false;
    };

    const auto& entries = detail::componentEntries();
    std::vector<LoadedEntity> loaded;
    loaded.reserve(entitiesField->size());

    // Mapa incremental: detecta ids DUPLICADOS no próprio arquivo.
    std::unordered_map<SceneEntityId, eng::ecs::Entity> entityOf;

    for (std::size_t i = 0; i < entitiesField->size(); ++i) {
        const eng::serial::JsonValue entity = entitiesField->at(i);
        if (!entity.isObject()) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "entities[" + std::to_string(i) + "] não é objeto"));
        }

        const auto idField = entity.find("id");
        if (!idField.has_value() || !idField->isString()) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "entities[" + std::to_string(i) + "] sem 'id' string"));
        }
        auto id = SceneEntityId::fromString(idField->asString());
        if (id.isError()) {
            return makeUnexpected(id.error());
        }

        LoadedEntity record;
        record.id = id.value();
        record.entity = scene.createNode();
        (void)scene.world().emplace<SceneIdentity>(
            record.entity, SceneIdentity{record.id});
        if (!entityOf.emplace(record.id, record.entity).second) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "id duplicado: " + record.id.toString()));
        }

        const auto parentField = entity.find("parent");
        if (parentField.has_value() && parentField->isString()) {
            auto parent = SceneEntityId::fromString(parentField->asString());
            if (parent.isError()) {
                return makeUnexpected(parent.error());
            }
            record.parent = parent.value();
            record.hasParent = true;
        } else if (parentField.has_value() && !parentField->isNull()) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "entities[" + std::to_string(i) +
                    "]: 'parent' deve ser uuid ou null"));
        }

        // Componentes (na ordem do arquivo — que é a canônica por nome).
        const auto componentsField = entity.find("components");
        if (componentsField.has_value()) {
            if (!componentsField->isArray()) {
                return makeUnexpected(sceneError(
                    StatusCode::ParseError,
                    "entities[" + std::to_string(i) +
                        "]: 'components' não é array"));
            }
            for (std::size_t c = 0; c < componentsField->size(); ++c) {
                const eng::serial::JsonValue component =
                    componentsField->at(c);
                if (!component.isObject()) {
                    return makeUnexpected(sceneError(
                        StatusCode::ParseError,
                        "components[" + std::to_string(c) +
                            "] não é objeto"));
                }
                const auto typeField = component.find("type");
                if (!typeField.has_value() || !typeField->isString()) {
                    return makeUnexpected(sceneError(
                        StatusCode::ParseError,
                        "componente sem 'type' string"));
                }
                const std::string typeName = typeField->asString();

                if (detail::isInternalComponentName(typeName)) {
                    // Representação canônica é "id"/"parent" — tolerado.
                    ENG_WARN("componente interno '{}' ignorado no load",
                             typeName);
                    continue;
                }

                const auto it = entries.find(typeName);
                if (it == entries.end()) {
                    return makeUnexpected(sceneError(
                        StatusCode::NotSupported,
                        "tipo de componente não registrado: '" + typeName +
                            "'"));
                }
                const auto dataField = component.find("data");
                if (!dataField.has_value() || !dataField->isObject()) {
                    return makeUnexpected(sceneError(
                        StatusCode::ParseError,
                        "componente '" + typeName +
                            "' sem 'data' objeto"));
                }
                const auto emplaced = it->second.decodeAndEmplace(
                    it->second, scene.world(), record.entity, *dataField);
                if (emplaced.isError()) {
                    return makeUnexpected(emplaced.error());
                }
            }
        }
        loaded.push_back(record);
    }

    // Consistência sceneEntityIds ⇄ entities (lista canônica = conteúdo).
    {
        std::vector<SceneEntityId> actual;
        actual.reserve(loaded.size());
        for (const LoadedEntity& record : loaded) {
            actual.push_back(record.id);
        }
        std::sort(actual.begin(), actual.end());
        std::sort(declaredIds.begin(), declaredIds.end());
        if (actual != declaredIds) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "sceneEntityIds diverge do conjunto de entidades"));
        }
    }

    // Parent/child: resolução em segunda passada (pais podem vir depois).
    for (const LoadedEntity& record : loaded) {
        if (!record.hasParent) {
            continue;
        }
        const auto parent = entityOf.find(record.parent);
        if (parent == entityOf.end()) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "parent " + record.parent.toString() +
                    " não existe na cena"));
        }
        if (!scene.attach(record.entity, parent->second)) {
            return makeUnexpected(sceneError(
                StatusCode::ParseError,
                "attach de " + record.id.toString() +
                    " sob " + record.parent.toString() +
                    " falhou (ciclo? inválido?)"));
        }
    }
    return {};
}

} // namespace eng::scene
