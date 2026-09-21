/// NiRuntime do editor — implementação (FASE 11; ver NiRuntime.hpp).
///
/// Bindings registrados sobre o CATÁLOGO ÚNICO + reflexão (mesmo
/// mecanismo do Inspector — auditoria D2; nenhum componente hard-coded
/// aqui: a tabela deriva de eng::scene::detail::componentEntries()):
///   position / scale / transform — eng::math::Transform (refletido)
///   rotation — euler↔quat CUSTOM (convenção de graus do script)
///   name — eng::scene::Name
///   <catálogo> — alias canônico E apelido curto (rigidbody, animator...)

#include "eng/editor/NiRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "eng/editor/Diagnostics.hpp"
#include "eng/editor/NiScriptComponent.hpp"
#include "eng/log/Macros.hpp"
#include "eng/math/Quat.hpp"
#include "eng/niscript/NiBindings.hpp"
#include "eng/scene/Name.hpp"
#include "eng/scene/SceneSerializer.hpp"

namespace eng::editor {

ENG_LOG_CATEGORY("editor.ni");

NiRuntime::NiRuntime() = default;
NiRuntime::~NiRuntime() { shutdown(); }

// =============================================================================
// Host — serviços do jogo sobre o CLONE
// =============================================================================

struct NiRuntime::HostImpl final : eng::ni::NiHost {
    NiRuntime* runtime = nullptr;
    eng::scene::Scene* scene = nullptr;

    float deltaSeconds() const override { return runtime->delta_; }
    bool actionDown(std::string_view action) const override
    {
        return runtime->queryAction_(action, 0);
    }
    bool actionPressed(std::string_view action) const override
    {
        return runtime->queryAction_(action, 1);
    }
    bool actionReleased(std::string_view action) const override
    {
        return runtime->queryAction_(action, 2);
    }
    eng::ecs::Entity spawn(std::string_view name) override
    {
        const eng::ecs::Entity e = scene->createNode();
        (void)scene->world().emplace<eng::scene::Name>(
            e, eng::scene::Name{std::string(name)});
        return e;
    }
    bool despawn(eng::ecs::Entity entity) override
    {
        return scene->destroyNode(entity);
    }
    eng::ecs::Entity find(std::string_view name) const override
    {
        eng::ecs::Entity found{0xFFFFFFFFu, 0xFFFFFFFFu};
        scene->world().each<eng::scene::Name>(
            [&](eng::ecs::Entity e, const eng::scene::Name& n) {
                if (found.index == 0xFFFFFFFFu && n.value == name) {
                    found = e;
                }
            });
        return found;
    }
};

bool NiRuntime::queryAction_(std::string_view action, int phase) const
{
    if (actionQuery_ != nullptr) {
        return actionQuery_(action, phase, actionQueryUser_);
    }
    return false;
}

void NiRuntime::setActionQuery(bool (*query)(std::string_view, int,
                                             void*),
                               void* user) noexcept
{
    actionQuery_ = query;
    actionQueryUser_ = user;
}

// =============================================================================
// Ciclo de vida
// =============================================================================

namespace {

/// Euler (graus) ↔ Quat — MESMA convenção de EditorDocument (R =
/// RotY·RotX·RotZ; round-trip testado em EditorTests).
[[nodiscard]] eng::math::Quat quatFromDegrees(
    const eng::math::Vec3& degrees) noexcept
{
    constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
    return eng::math::Quat::fromEulerAngles(
        degrees.x * kDegToRad, degrees.y * kDegToRad, degrees.z * kDegToRad);
}

[[nodiscard]] eng::math::Vec3 degreesFromQuat(
    const eng::math::Quat& rotation) noexcept
{
    constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;
    const eng::math::Mat4 m = rotation.toMatrix();
    const float sinPitch = std::clamp(-m.at(2, 1), -1.f, 1.f);
    const float pitch = std::asin(sinPitch);
    float yaw = 0.f;
    float roll = 0.f;
    if (std::abs(std::cos(pitch)) > 1e-4f) {
        yaw = std::atan2(m.at(2, 0), m.at(2, 2));
        roll = std::atan2(m.at(0, 1), m.at(1, 1));
    } else {
        yaw = std::atan2(-m.at(0, 2), m.at(0, 0));
    }
    return eng::math::Vec3{pitch * kRadToDeg, yaw * kRadToDeg,
                           roll * kRadToDeg};
}

/// Binding CUSTOM de rotation (euler em GRAUS — convenção do script,
/// design §3; o genérico refletido só expõe subcampos de Quat).
struct RotationAdapter {
    eng::ecs::World* world = nullptr;
};

bool rotationGet(void* user, eng::ecs::Entity e, std::string_view path,
                 eng::ni::NiValue& out, eng::ni::NiFault& fault)
{
    (void)path; // açúcar "rotation" = campo inteiro
    const auto* self = static_cast<const RotationAdapter*>(user);
    if (e.index == 0xFFFFFFFFu) {
        fault.kind = eng::ni::NiFault::Kind::EntityNull;
        fault.message = "leitura em entidade nula";
        return false;
    }
    if (!self->world->valid(e)) {
        fault.kind = eng::ni::NiFault::Kind::EntityStale;
        fault.message = "entidade obsoleta";
        return false;
    }
    const auto* transform = self->world->get<eng::math::Transform>(e);
    if (transform == nullptr) {
        fault.kind = eng::ni::NiFault::Kind::ComponentMissing;
        fault.message = "Transform ausente na entidade";
        return false;
    }
    const eng::math::Vec3 degrees = degreesFromQuat(transform->rotation);
    out = eng::ni::niVec3(degrees.x, degrees.y, degrees.z);
    return true;
}

bool rotationSet(void* user, eng::ecs::Entity e, std::string_view path,
                 const eng::ni::NiValue& v, eng::ni::NiFault& fault)
{
    (void)path;
    const auto* self = static_cast<const RotationAdapter*>(user);
    if (v.type != eng::ni::NiType::Vec3) {
        fault.kind = eng::ni::NiFault::Kind::Type;
        fault.message = "rotation exige vec3 (graus)";
        return false;
    }
    if (e.index == 0xFFFFFFFFu) {
        fault.kind = eng::ni::NiFault::Kind::EntityNull;
        fault.message = "escrita em entidade nula";
        return false;
    }
    if (!self->world->valid(e)) {
        fault.kind = eng::ni::NiFault::Kind::EntityStale;
        fault.message = "entidade obsoleta";
        return false;
    }
    auto* transform = self->world->get<eng::math::Transform>(e);
    if (transform == nullptr) {
        fault.kind = eng::ni::NiFault::Kind::ComponentMissing;
        fault.message = "Transform ausente na entidade";
        return false;
    }
    transform->rotation = quatFromDegrees(
        eng::math::Vec3{static_cast<float>(v.d[0]),
                        static_cast<float>(v.d[1]),
                        static_cast<float>(v.d[2])});
    return true;
}

/// Fetch de entrada de catálogo (ComponentEntry é type-erased sobre
/// World — get/getMutable por ponteiro de função).
struct CatalogFetch {
    eng::ecs::World* world = nullptr;
    const eng::scene::detail::ComponentEntry* entry = nullptr;
};

const void* catalogFetchC(void* user, eng::ecs::Entity e)
{
    const auto* fetch = static_cast<const CatalogFetch*>(user);
    return fetch->entry->get(*fetch->world, e);
}

void* catalogFetchM(void* user, eng::ecs::Entity e)
{
    const auto* fetch = static_cast<const CatalogFetch*>(user);
    return fetch->entry->getMutable(*fetch->world, e);
}

bool worldValid(void* user, eng::ecs::Entity e)
{
    return static_cast<eng::ecs::World*>(user)->valid(e);
}

} // namespace

void NiRuntime::start(eng::scene::Scene& runtimeScene)
{
    shutdown();
    scene_ = &runtimeScene;

    natives_.addBaseLibrary();
    natives_.addStandardHost();
    host_ = std::make_unique<HostImpl>();
    host_->runtime = this;
    host_->scene = scene_;

    auto* world = &scene_->world();

    const auto fetchT = [](void* user, eng::ecs::Entity e) -> const void* {
        return static_cast<eng::ecs::World*>(user)
            ->get<eng::math::Transform>(e);
    };
    const auto fetchTm = [](void* user, eng::ecs::Entity e) -> void* {
        return static_cast<eng::ecs::World*>(user)
            ->get<eng::math::Transform>(e);
    };
    (void)eng::ni::niAddReflectionBinding(
        bindings_, "position", "eng::math::Transform", fetchT, fetchTm,
        world, "position", &worldValid);
    (void)eng::ni::niAddReflectionBinding(
        bindings_, "scale", "eng::math::Transform", fetchT, fetchTm, world,
        "scale", &worldValid);
    (void)eng::ni::niAddReflectionBinding(
        bindings_, "transform", "eng::math::Transform", fetchT, fetchTm,
        world, "", &worldValid);
    (void)eng::ni::niAddReflectionBinding(
        bindings_, "name", "eng::scene::Name",
        [](void* user, eng::ecs::Entity e) -> const void* {
            return static_cast<eng::ecs::World*>(user)
                ->get<eng::scene::Name>(e);
        },
        [](void* user, eng::ecs::Entity e) -> void* {
            return static_cast<eng::ecs::World*>(user)
                ->get<eng::scene::Name>(e);
        },
        world, "value", &worldValid);

    auto rotationState = std::make_shared<RotationAdapter>();
    rotationState->world = world;
    {
        eng::ni::NiComponentBinding binding;
        binding.alias = "rotation";
        binding.get = &rotationGet;
        binding.set = &rotationSet;
        binding.user = rotationState.get();
        binding.keepAlive = rotationState;
        bindings_.add(std::move(binding));
    }

    for (const auto& [typeName, entry] :
         eng::scene::detail::componentEntries()) {
        auto fetch = std::make_shared<CatalogFetch>();
        fetch->world = world;
        fetch->entry = &entry;
        (void)eng::ni::niAddReflectionBinding(
            bindings_, typeName, typeName, &catalogFetchC, &catalogFetchM,
            fetch.get(), "", &worldValid);
        // apelido curto (última parte do nome canônico, minúscula)
        const std::size_t colon = typeName.rfind(':');
        std::string lower;
        const std::string_view tail =
            colon == std::string::npos
                ? std::string_view(typeName)
                : std::string_view(typeName).substr(colon + 1);
        for (const char c : tail) {
            lower.push_back(static_cast<char>(
                c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
        if (!lower.empty() && lower != "name" && lower != "transform") {
            auto fetch2 = std::make_shared<CatalogFetch>();
            fetch2->world = world;
            fetch2->entry = &entry;
            (void)eng::ni::niAddReflectionBinding(
                bindings_, lower, typeName, &catalogFetchC, &catalogFetchM,
                fetch2.get(), "", &worldValid);
        }
    }

    // Compila + instancia scripts do CLONE (ordem determinística do each).
    // P4.1 (T2/D5): TODOS os resultados vão para stats_ (fonte da UI —
    // toast/painel do editor) E para o diagnóstico persistido (marcos
    // SCRIPT_* — a forense do device passa a mostrar porquê um script
    // "não faz nada"). O silêncio do P3.5 era o defeito D5.
    stats_ = NiScriptStats{};
    const eng::ni::CompileOptions options{&natives_};
    scene_->world().each<eng::editor::NiScriptComponent>(
        [&](eng::ecs::Entity e, const eng::editor::NiScriptComponent& c) {
            if (c.source.empty()) {
                return;
            }
            ++stats_.scriptsFound;
            std::vector<eng::ni::NiDiag> diags;
            auto program = eng::ni::compile(c.source, options, &diags);
            if (!program.ok()) {
                ++stats_.scriptsFailed;
                ENG_ERROR(
                    "ni-script: compilacao falhou na entidade {} ({} "
                    "erro(s))",
                    e.index, diags.size());
                for (const eng::ni::NiDiag& d : diags) {
                    ENG_ERROR("  {}:{} {}", d.line, d.col, d.message);
                }
                if (stats_.firstCompileError.empty() && !diags.empty()) {
                    const eng::ni::NiDiag& d = diags.front();
                    char buf[192];
                    std::snprintf(buf, sizeof buf, "%u:%u %s",
                                  static_cast<unsigned>(d.line),
                                  static_cast<unsigned>(d.col),
                                  d.message.c_str());
                    stats_.firstCompileError = buf;
                    stats_.firstFailedEntity = e.index;
                    diag::mark("SCRIPT_COMPILE", "failed", buf);
                }
                return;
            }
            ++stats_.scriptsCompiled;
            set_.create(std::move(program).value(), e);
        });
    stats_.instances =
        static_cast<std::uint32_t>(set_.size());
    if (stats_.scriptsFound > 0 && stats_.scriptsFailed == 0) {
        diag::mark("SCRIPT_COMPILE", "ok",
                   "todos os scripts compilaram");
    }
    if (stats_.scriptsFound == 0) {
        diag::mark("SCRIPT_COMPILE", "skipped", "nenhum script na cena");
    }

    // @init de TODAS as instâncias (ordem de criação — docs/ni-script/07)
    const eng::ni::NiExecContext::Params p = params();
    for (const auto& instance : set_.asVector()) {
        (void)vm_.run(*instance, "@init", p);
    }
}

void NiRuntime::fireStart()
{
    const eng::ni::NiExecContext::Params p = params();
    for (const auto& instance : set_.asVector()) {
        (void)vm_.run(*instance, "start", p);
    }
}

void NiRuntime::tick(float deltaSeconds)
{
    delta_ = deltaSeconds;
    const eng::ni::NiExecContext::Params p = params();
    for (const auto& instance : set_.asVector()) {
        (void)vm_.run(*instance, "update", p);
        // P4.1 (T2/D5): contagem VISÍVEL de ticks + faults — o editor
        // mostra "N scripts, T ticks" e o ÚLTIMO fault do runtime; com
        // isto o autor distingue "script compila mas não roda" de
        // "roda e falha no binding".
        ++stats_.ticks;
        if (stats_.firstUpdateTick == 0) {
            stats_.firstUpdateTick = stats_.ticks;
        }
        if (const std::optional<eng::ni::NiFault>& fault =
                instance->lastFault();
            fault.has_value()) {
            ++stats_.faults;
            char buf[192];
            std::snprintf(buf, sizeof buf, "%s @ entidade %u",
                          fault->message.c_str(),
                          static_cast<unsigned>(instance->self().index));
            stats_.lastFaultMessage = buf;
            diag::mark("SCRIPT_FAULT", "runtime", buf);
        }
    }
}

void NiRuntime::shutdown() noexcept
{
    if (!set_.empty() && scene_ != nullptr) {
        const eng::ni::NiExecContext::Params p = params();
        for (const auto& instance : set_.asVector()) {
            (void)vm_.run(*instance, "destroy", p);
        }
    }
    set_.clear();
    bindings_ = eng::ni::NiBindingTable{};
    natives_ = eng::ni::NiNativeTable{};
    scene_ = nullptr;
    host_.reset();
}

std::vector<const eng::ni::NiScriptState*> NiRuntime::instances() const
{
    std::vector<const eng::ni::NiScriptState*> result;
    for (const auto& instance : set_.asVector()) {
        result.push_back(instance.get());
    }
    return result;
}

eng::ni::NiExecContext::Params NiRuntime::params() const
{
    eng::ni::NiExecContext::Params p;
    p.natives = &natives_;
    p.host = host_.get();
    p.bindings = &bindings_;
    // const: o VM apenas LÊ o conjunto (propagação de emit — §4); os
    // globais/links das instâncias pertencem a cada NiScriptState.
    p.set = const_cast<eng::ni::NiInstanceSet*>(&set_);
    p.budget = eng::ni::kDefaultBudget;
    return p;
}

} // namespace eng::editor
