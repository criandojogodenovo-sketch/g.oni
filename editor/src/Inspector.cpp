#include "eng/editor/Inspector.hpp"

/// Inspector — campos por offset+typeName via reflect (FASE 8, §8.4).
///
/// Zero conhecimento de componentes específicos: Transform/Name são lidos
/// como QUALQUER struct refletida. Valores trafegam como string (boundary
/// neutra — JNI recebe texto; §4 da auditoria).

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "eng/log/Macros.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/scene/Name.hpp"
#include "eng/scene/SceneSerializer.hpp"

namespace eng::editor {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;
using ComponentEntry = eng::scene::detail::ComponentEntry;

ENG_LOG_CATEGORY("editor");

[[nodiscard]] Error inspectorError(StatusCode code, std::string message)
{
    return Error{code, "Inspector: " + std::move(message)};
}

const ComponentEntry* entryOf(std::string_view component)
{
    const auto& entries = eng::scene::detail::componentEntries();
    const auto it = entries.find(std::string(component));
    return it == entries.end() ? nullptr : &it->second;
}

// =============================================================================
// Formatação/parse por typeName (primitivas + enums; structs recursam)
// =============================================================================

[[nodiscard]] std::string formatFloat(float v) noexcept
{
    char buf[32];
    const int n = std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "0";
}

[[nodiscard]] std::string formatDouble(double v) noexcept
{
    char buf[48];
    const int n = std::snprintf(buf, sizeof(buf), "%.17g", v);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : "0";
}

[[nodiscard]] std::string formatIntegral(const void* member,
                                         const eng::reflect::TypeInfo& type)
{
    if (type.name == "i8" || type.name == "i16" || type.name == "i32" ||
        type.name == "i64") {
        std::int64_t v = 0;
        if (type.name == "i8") {
            v = *static_cast<const std::int8_t*>(member);
        } else if (type.name == "i16") {
            v = *static_cast<const std::int16_t*>(member);
        } else if (type.name == "i32") {
            v = *static_cast<const std::int32_t*>(member);
        } else {
            v = *static_cast<const std::int64_t*>(member);
        }
        return std::to_string(v);
    }
    std::uint64_t v = 0;
    if (type.name == "u8") {
        v = *static_cast<const std::uint8_t*>(member);
    } else if (type.name == "u16") {
        v = *static_cast<const std::uint16_t*>(member);
    } else if (type.name == "u32") {
        v = *static_cast<const std::uint32_t*>(member);
    } else {
        v = *static_cast<const std::uint64_t*>(member);
    }
    return std::to_string(v);
}

/// Escreve um inteiro de 64 bits no tamanho exato do tipo (endianness
/// correta por acesso tipado — nada de memcpy de bytes baixos).
void writeIntegral(void* member, std::size_t size, std::int64_t v) noexcept
{
    switch (size) {
    case 1: *static_cast<std::int8_t*>(member) = static_cast<std::int8_t>(v); break;
    case 2: *static_cast<std::int16_t*>(member) = static_cast<std::int16_t>(v); break;
    case 4: *static_cast<std::int32_t*>(member) = static_cast<std::int32_t>(v); break;
    default: *static_cast<std::int64_t*>(member) = v; break;
    }
}

/// Lê o campo primitivo em `member` segundo `type` → string.
[[nodiscard]] Result<std::string> formatValue(
    const void* member, const eng::reflect::TypeInfo& type)
{
    if (type.kind == eng::reflect::TypeKind::Enum) {
        // Enum por NOME (estável — ADR-033).
        std::int64_t raw = 0;
        if (type.underlyingTypeId != 0) {
            const auto* underlying =
                eng::reflect::TypeRegistry::global().find(type.underlyingTypeId);
            if (underlying == nullptr) {
                return makeUnexpected(inspectorError(
                    StatusCode::NotFound,
                    "enum '" + type.name + "' sem tipo subjacente registrado"));
            }
            const std::string rawText = formatIntegral(member, *underlying);
            raw = std::strtoll(rawText.c_str(), nullptr, 10);
        }
        for (const auto& enumerator : type.enumerators) {
            if (enumerator.value == raw) {
                return enumerator.name;
            }
        }
        return makeUnexpected(inspectorError(
            StatusCode::NotFound, "enum '" + type.name + "' valor " +
                                      std::to_string(raw) +
                                      " sem enumerador"));
    }
    if (type.kind == eng::reflect::TypeKind::Struct) {
        return makeUnexpected(inspectorError(
            StatusCode::InvalidArgument,
            "tipo '" + type.name + "' é struct — use o caminho do subcampo"));
    }
    if (type.name == "bool") {
        return std::string(*static_cast<const bool*>(member) ? "true" : "false");
    }
    if (type.name == "f32") {
        return formatFloat(*static_cast<const float*>(member));
    }
    if (type.name == "f64") {
        return formatDouble(*static_cast<const double*>(member));
    }
    if (type.name == "string") {
        return *static_cast<const std::string*>(member);
    }
    return formatIntegral(member, type);
}

/// Escreve `value` em `member` segundo `type`. Erro preciso; sem escrita
/// parcial (valida ANTES).
[[nodiscard]] Result<void> parseValue(void* member,
                                     const eng::reflect::TypeInfo& type,
                                     std::string_view value)
{
    if (type.kind == eng::reflect::TypeKind::Enum) {
        for (const auto& enumerator : type.enumerators) {
            if (enumerator.name == value) {
                writeIntegral(member, type.size, enumerator.value);
                return {};
            }
        }
        return makeUnexpected(inspectorError(
            StatusCode::InvalidArgument,
            "valor '" + std::string(value) + "' não é enumerador de '" +
                type.name + "'"));
    }
    if (type.kind == eng::reflect::TypeKind::Struct) {
        return makeUnexpected(inspectorError(
            StatusCode::InvalidArgument,
            "tipo '" + type.name + "' é struct — use o caminho do subcampo"));
    }
    if (type.name == "bool") {
        if (value == "true") {
            *static_cast<bool*>(member) = true;
            return {};
        }
        if (value == "false") {
            *static_cast<bool*>(member) = false;
            return {};
        }
        return makeUnexpected(inspectorError(StatusCode::InvalidArgument,
                                             "bool espera true/false"));
    }
    if (type.name == "f32" || type.name == "f64") {
        std::string text(value);
        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0' || errno == ERANGE ||
            !std::isfinite(parsed)) {
            return makeUnexpected(inspectorError(
                StatusCode::InvalidArgument,
                "'" + text + "' não é número finito válido"));
        }
        if (type.name == "f32") {
            *static_cast<float*>(member) = static_cast<float>(parsed);
        } else {
            *static_cast<double*>(member) = parsed;
        }
        return {};
    }
    if (type.name == "string") {
        *static_cast<std::string*>(member) = std::string(value);
        return {};
    }
    // Inteiros — validação completa.
    std::string text(value);
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return makeUnexpected(inspectorError(StatusCode::InvalidArgument,
                                             "'" + text + "' não é inteiro"));
    }
    const long long min =
        (type.name == "i8")   ? std::numeric_limits<std::int8_t>::min()
        : (type.name == "i16") ? std::numeric_limits<std::int16_t>::min()
        : (type.name == "i32") ? std::numeric_limits<std::int32_t>::min()
                              : std::numeric_limits<std::int64_t>::min();
    const long long max =
        (type.name == "i8")   ? std::numeric_limits<std::int8_t>::max()
        : (type.name == "i16") ? std::numeric_limits<std::int16_t>::max()
        : (type.name == "i32") ? std::numeric_limits<std::int32_t>::max()
                              : std::numeric_limits<std::int64_t>::max();
    if (parsed < min || parsed > max) {
        return makeUnexpected(inspectorError(
            StatusCode::InvalidArgument,
            "'" + text + "' fora da faixa de '" + type.name + "'"));
    }
    writeIntegral(member, type.size, parsed);
    return {};
}

/// Resolve o ENDEREÇO de um campo folha por caminho "a.b.c" dentro do
/// componente `base` (struct registrada). Template sobre a constância do
/// ponteiro (leitura via get, escrita via getMutable).
struct ResolvedField {
    void* member = nullptr;
    const eng::reflect::TypeInfo* type = nullptr;
};

template <typename Ptr>
[[nodiscard]] eng::core::Result<ResolvedField> resolveFieldImpl(
    Ptr base, const eng::reflect::TypeInfo& type, std::string_view path)
{
    using CharPtr =
        std::conditional_t<std::is_const_v<std::remove_pointer_t<Ptr>>,
                           const char*, char*>;
    CharPtr cursor = static_cast<CharPtr>(base);
    const eng::reflect::TypeInfo* current = &type;

    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t dot = path.find('.', begin);
        const std::string_view part =
            path.substr(begin, dot == std::string_view::npos
                                   ? std::string_view::npos
                                   : dot - begin);
        if (part.empty()) {
            return makeUnexpected(inspectorError(
                StatusCode::InvalidArgument,
                "caminho '" + std::string(path) + "' tem componente vazio"));
        }
        const eng::reflect::PropertyInfo* found = nullptr;
        for (const auto& property : current->properties) {
            if (property.name == part) {
                found = &property;
                break;
            }
        }
        if (found == nullptr) {
            return makeUnexpected(inspectorError(
                StatusCode::NotFound,
                "campo '" + std::string(part) + "' não existe em '" +
                    current->name + "'"));
        }
        const eng::reflect::TypeInfo* next =
            eng::reflect::TypeRegistry::global().find(found->typeName);
        if (next == nullptr) {
            return makeUnexpected(inspectorError(
                StatusCode::NotFound, "tipo '" + found->typeName +
                                          "' não registrado no reflect"));
        }
        cursor = static_cast<CharPtr>(cursor) + found->offset;
        current = next;
        if (dot == std::string_view::npos) {
            break;
        }
        begin = dot + 1;
    }
    return ResolvedField{const_cast<void*>(static_cast<const void*>(cursor)),
                         current};
}

}  // namespace

// =============================================================================
// API pública
// =============================================================================

std::vector<std::string> Inspector::catalog()
{
    std::vector<std::string> names;
    for (const auto& [name, entry] : eng::scene::detail::componentEntries()) {
        (void)entry;
        names.push_back(name);
    }
    return names;
}

std::vector<std::string> Inspector::componentsOf(const eng::scene::Scene& scene,
                                                 eng::ecs::Entity entity)
{
    std::vector<std::string> present;
    const auto& world = scene.world();
    if (!scene.isNode(entity)) {
        return present;
    }
    for (const auto& [name, entry] : eng::scene::detail::componentEntries()) {
        if (entry.has(world, entity)) {
            present.push_back(name);
        }
    }
    return present;
}

Result<std::vector<Inspector::Field>> Inspector::fieldsOf(
    const eng::scene::Scene& scene, eng::ecs::Entity entity,
    std::string_view component)
{
    const ComponentEntry* entry = entryOf(component);
    if (entry == nullptr) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "componente '" + std::string(component) + "' fora do catálogo"));
    }
    if (!scene.isNode(entity) || !entry->has(scene.world(), entity)) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "entidade não possui o componente '" + std::string(component) +
                "'"));
    }
    const void* base = entry->get(scene.world(), entity);
    if (base == nullptr || entry->info == nullptr) {
        return makeUnexpected(inspectorError(StatusCode::Internal,
                                             "componente sumiu entre has/get"));
    }

    std::vector<Field> fields;
    const auto flatten = [&](auto&& self, std::string_view prefix,
                             const void* obj,
                             const eng::reflect::TypeInfo& type) -> void {
        for (const auto& property : type.properties) {
            const eng::reflect::TypeInfo* fieldType =
                eng::reflect::TypeRegistry::global().find(property.typeName);
            if (fieldType == nullptr) {
                ENG_WARN("Inspector: tipo '{}' do campo '{}' não registrado",
                         property.typeName, property.name);
                continue;
            }
            const void* member = static_cast<const char*>(obj) + property.offset;
            const std::string path =
                prefix.empty()
                    ? property.name
                    : std::string(prefix) + "." + property.name;
            if (fieldType->kind == eng::reflect::TypeKind::Struct) {
                // Struct conhecida → recursão (Vec3/Quat/...).
                self(self, path, member, *fieldType);
                continue;
            }
            auto value = formatValue(member, *fieldType);
            if (value.isError()) {
                ENG_WARN("Inspector: campo '{}.{}' ilegível ({})", path,
                         property.name, value.error().message);
                continue;
            }
            fields.push_back(Field{path, property.typeName,
                                   std::move(value.value())});
        }
    };
    flatten(flatten, "", base, *entry->info);
    return fields;
}

Result<std::string> Inspector::getField(const eng::scene::Scene& scene,
                                         eng::ecs::Entity entity,
                                         std::string_view component,
                                         std::string_view fieldPath)
{
    const ComponentEntry* entry = entryOf(component);
    if (entry == nullptr) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "componente '" + std::string(component) + "' fora do catálogo"));
    }
    if (!scene.isNode(entity) || !entry->has(scene.world(), entity)) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "entidade não possui o componente '" + std::string(component) +
                "'"));
    }
    // Leitura por caminho — ponteiro CONST (get do catálogo; sem mutação).
    const void* base = entry->get(scene.world(), entity);
    if (base == nullptr || entry->info == nullptr) {
        return makeUnexpected(inspectorError(StatusCode::Internal,
                                             "componente sumiu entre has/get"));
    }
    auto resolved = resolveFieldImpl(base, *entry->info, fieldPath);
    if (resolved.isError()) {
        return makeUnexpected(resolved.error());
    }
    return formatValue(resolved.value().member, *resolved.value().type);
}

Result<void> Inspector::setField(eng::scene::Scene& scene,
                                  eng::ecs::Entity entity,
                                  std::string_view component,
                                  std::string_view fieldPath,
                                  std::string_view value)
{
    const ComponentEntry* entry = entryOf(component);
    if (entry == nullptr) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "componente '" + std::string(component) + "' fora do catálogo"));
    }
    if (!scene.isNode(entity) || !entry->has(scene.world(), entity)) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "entidade não possui o componente '" + std::string(component) +
                "'"));
    }
    void* base = entry->getMutable(scene.world(), entity);
    if (base == nullptr || entry->info == nullptr) {
        return makeUnexpected(inspectorError(StatusCode::Internal,
                                             "componente sumiu entre has/get"));
    }
    auto resolved = resolveFieldImpl(base, *entry->info, fieldPath);
    if (resolved.isError()) {
        return makeUnexpected(resolved.error());
    }
    auto written = parseValue(resolved.value().member,
                              *resolved.value().type, value);
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    return {};
}

bool Inspector::isRemovable(std::string_view component)
{
    // Transform é a geometria do nó (Scene emplanta); Name é o rótulo
    // mínimo do editor. Hierarchy/SceneIdentity/WorldMatrix são internos
    // (nem aparecem no catálogo — isInternalComponentName no serializer).
    return component != "eng::math::Transform" && component != "eng::scene::Name";
}

Result<void> Inspector::addComponent(eng::scene::Scene& scene,
                                      eng::ecs::Entity entity,
                                      std::string_view component)
{
    const ComponentEntry* entry = entryOf(component);
    if (entry == nullptr) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "componente '" + std::string(component) + "' fora do catálogo"));
    }
    if (!scene.isNode(entity)) {
        return makeUnexpected(inspectorError(StatusCode::InvalidArgument,
                                              "entidade obsoleta"));
    }
    if (entry->has(scene.world(), entity)) {
        return makeUnexpected(inspectorError(
            StatusCode::AlreadyExists,
            "entidade já possui '" + std::string(component) + "'"));
    }
    return entry->emplaceDefault(*entry, scene.world(), entity);
}

Result<void> Inspector::removeComponent(eng::scene::Scene& scene,
                                         eng::ecs::Entity entity,
                                         std::string_view component)
{
    const ComponentEntry* entry = entryOf(component);
    if (entry == nullptr) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "componente '" + std::string(component) + "' fora do catálogo"));
    }
    if (!isRemovable(component)) {
        return makeUnexpected(inspectorError(
            StatusCode::InvalidArgument,
            "componente '" + std::string(component) + "' é protegido"));
    }
    if (!scene.isNode(entity) || !entry->has(scene.world(), entity)) {
        return makeUnexpected(inspectorError(
            StatusCode::NotFound,
            "entidade não possui '" + std::string(component) + "'"));
    }
    (void)entry->removeFrom(scene.world(), entity);
    return {};
}

}  // namespace eng::editor
