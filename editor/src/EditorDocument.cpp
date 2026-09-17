#include "eng/editor/EditorDocument.hpp"

/// EditorDocument — estado + comandos (FASE 8; separação editor×runtime
/// ADR-044: clone por serialização, edição rejeitada em Play).

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "eng/log/Macros.hpp"
#include "eng/math/Mat4.hpp"
#include "eng/math/Quat.hpp"
#include "eng/project/ProjectPaths.hpp"
#include "eng/scene/Name.hpp"
#include "eng/scene/SceneSerializer.hpp"
#include "eng/serial/Json.hpp"

namespace eng::editor {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

ENG_LOG_CATEGORY("editor");

[[nodiscard]] Error documentError(StatusCode code, std::string message)
{
    return Error{code, "EditorDocument: " + std::move(message)};
}

/// Euler (graus) ↔ Quat — MESMA convenção de Quat::fromEulerAngles
/// (R = RotY(yaw)·RotX(pitch)·RotZ(roll); ADR do math). Round-trip
/// testado em EditorTests.
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
    // Extração YXZ da matriz de rotação pura (quaternion):
    //   pitch = asin(-M[1][2]); yaw = atan2(M[0][2], M[2][2]);
    //   roll = atan2(M[1][0], M[1][1]) — at(col,row) do column-major.
    const float sinPitch =
        std::clamp(-m.at(2, 1), -1.f, 1.f);
    const float pitch = std::asin(sinPitch);
    float yaw = 0.f;
    float roll = 0.f;
    if (std::abs(std::cos(pitch)) > 1e-4f) {
        yaw = std::atan2(m.at(2, 0), m.at(2, 2));
        roll = std::atan2(m.at(0, 1), m.at(1, 1));
    } else {
        // Gimbal lock: yaw degenerado — convenção yaw=0 (documentada).
        roll = std::atan2(-m.at(1, 0), m.at(0, 0));
    }
    return eng::math::Vec3{pitch * kRadToDeg, yaw * kRadToDeg,
                           roll * kRadToDeg};
}

/// Path RELATIVO seguro (§8.1/§D7): não absoluto e SEM componente ".." —
/// anti-traversal que funciona tanto com workspace relativo (testes)
/// quanto absoluto (Android filesDir — o root é ESCOLHA do host; o que
/// não pode é escapar DE DENTRO do projeto).
[[nodiscard]] bool isSafeRelativePath(std::string_view path) noexcept
{
    if (path.empty() || path.find('\0') != std::string_view::npos) {
        return false;
    }
    if (path.front() == '/') {
        return false;
    }
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t slash = path.find('/', begin);
        const std::string_view component =
            path.substr(begin, slash == std::string_view::npos
                                   ? std::string_view::npos
                                   : slash - begin);
        if (component == ".." || component.empty()) {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        begin = slash + 1;
    }
    return true;
}

[[nodiscard]] eng::fs::Path scenesRootOf(
    const eng::project::ProjectFile& project)
{
    const auto& roots = project.config.sceneRoots;
    if (!roots.empty()) {
        return project.paths().resolve(roots.front());
    }
    return project.paths().projectDir() / eng::fs::Path{"scenes"};
}

}  // namespace

// =============================================================================
// Criação
// =============================================================================

Result<std::unique_ptr<EditorDocument>> EditorDocument::create(
    eng::fs::FileSystem& fs, const eng::fs::Path& workspaceRoot)
{
    auto document = std::unique_ptr<EditorDocument>(new EditorDocument{});
    document->fs_ = &fs;
    document->workspaceRoot_ = workspaceRoot;
    return document;
}

EditorDocument::~EditorDocument() = default;

// =============================================================================
// Projeto (§8.1)
// =============================================================================

Result<void> EditorDocument::newProject(std::string_view name)
{
    if (name.empty()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidArgument, "nome vazio"));
    }
    if (name.find('/') != std::string_view::npos || name == "." ||
        name == "..") {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "nome de projeto é um nome de pasta (sem '/' ou '..')"));
    }
    const eng::fs::Path root = workspaceRoot_ / eng::fs::Path{std::string(name)};
    auto exists = fs_->exists(root);
    if (exists.isError()) {
        return makeUnexpected(exists.error());
    }
    if (exists.value()) {
        return makeUnexpected(documentError(StatusCode::AlreadyExists,
                                            "projeto '" + std::string(name) +
                                                "' já existe"));
    }

    // Estrutura: assets/<categorias> + scenes + project.goni.json.
    const eng::fs::Path assets = root / eng::fs::Path{"assets"};
    auto made = fs_->mkdirs(assets);
    if (made.isError()) {
        return makeUnexpected(made.error());
    }
    for (const auto& category : AssetBrowser::categories()) {
        auto cat = fs_->mkdirs(assets / eng::fs::Path{category});
        if (cat.isError()) {
            return makeUnexpected(cat.error());
        }
    }
    auto scenes = fs_->mkdirs(root / eng::fs::Path{"scenes"});
    if (scenes.isError()) {
        return makeUnexpected(scenes.error());
    }

    eng::project::ProjectFile file;
    file.config.projectId = eng::project::ProjectId::generate();
    file.config.name = std::string(name);
    file.config.engineVersion = eng::core::Version{0, 1, 0};
    file.config.assetRegistryPath = eng::fs::Path{"assets/asset_registry.json"};
    file.config.sceneRoots = {eng::fs::Path{"scenes"}};
    file.filePath = root / eng::fs::Path{"project.goni.json"};
    auto written = file.writeTo(*fs_);
    if (written.isError()) {
        return makeUnexpected(written.error());
    }

    project_ = std::move(file);
    projectDirty_ = false;
    assets_ = std::make_unique<AssetBrowser>(
        *fs_, project_->paths().assetsRoot(),
        project_->paths().resolve(project_->config.assetRegistryPath));
    auto loaded = assets_->loadRegistry();
    if (loaded.isError()) {
        return makeUnexpected(loaded.error());
    }

    auto fresh = newScene();
    if (fresh.isError()) {
        return makeUnexpected(fresh.error());
    }
    ENG_INFO("projeto criado: {}", name);
    return {};
}

Result<void> EditorDocument::openProject(const eng::fs::Path& projectRoot)
{
    if (!isSafeRelativePath(projectRoot.str())) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "projectRoot deve ser relativo ao workspace (sem ..)"));
    }
    // Simétrico a newProject: o caminho é resolvido CONTRA o workspace
    // (nunca CWD — no Android o CWD não é o workspace). Bug pego pelo
    // teste de segunda execução (dirs persistem entre runs).
    const eng::fs::Path root = workspaceRoot_ / projectRoot;
    const eng::fs::Path file = root / eng::fs::Path{"project.goni.json"};
    auto opened = eng::project::ProjectFile::readFrom(*fs_, file);
    if (opened.isError()) {
        return makeUnexpected(opened.error());
    }

    project_ = std::move(opened.value());
    projectDirty_ = false;
    assets_ = std::make_unique<AssetBrowser>(
        *fs_, project_->paths().assetsRoot(),
        project_->paths().resolve(project_->config.assetRegistryPath));
    auto loaded = assets_->loadRegistry();
    if (loaded.isError()) {
        return makeUnexpected(loaded.error());
    }

    auto fresh = newScene();
    if (fresh.isError()) {
        return makeUnexpected(fresh.error());
    }
    ENG_INFO("projeto aberto: {}", project_->config.name);
    return {};
}

Result<void> EditorDocument::saveProject()
{
    if (!hasProject()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "sem projeto aberto"));
    }
    auto written = project_->writeTo(*fs_);
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    projectDirty_ = false;
    return {};
}

std::string EditorDocument::projectName() const
{
    return hasProject() ? project_->config.name : std::string{};
}

Result<void> EditorDocument::setProjectName(std::string_view name)
{
    if (!hasProject()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "sem projeto aberto"));
    }
    if (name.empty()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidArgument, "nome vazio"));
    }
    if (mode_ == Mode::Play) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                             "projeto é somente-leitura em Play"));
    }
    project_->config.name = std::string(name);
    projectDirty_ = true;
    return {};
}

eng::fs::Path EditorDocument::projectRoot() const
{
    return hasProject() ? project_->paths().projectDir()
                      : eng::fs::Path{};
}

// =============================================================================
// Cena (§8.2)
// =============================================================================

Result<void> EditorDocument::newScene()
{
    if (mode_ == Mode::Play) {
        return makeUnexpected(documentError(
            StatusCode::InvalidState, "cena é somente-leitura em Play"));
    }
    scene_.emplace(); // constrói in place (Scene não é movível — ADR-025)
    selection_.reset();
    sceneDirty_ = false;
    return {};
}

Result<void> EditorDocument::saveScene(std::string_view scenePath)
{
    if (!hasProject()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "sem projeto aberto"));
    }
    if (mode_ == Mode::Play) {
        return makeUnexpected(documentError(
            StatusCode::InvalidState, "cena é somente-leitura em Play"));
    }
    if (!isSafeRelativePath(scenePath)) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "path de cena deve ser RELATIVO ao projeto (§8.1, sem ..)"));
    }
    const eng::fs::Path path{std::string(scenePath)};
    auto text = eng::scene::SceneSerializer::save(*scene_);
    if (text.isError()) {
        return makeUnexpected(text.error());
    }
    const eng::fs::Path full = scenesRootOf(*project_) / path;
    auto made = fs_->mkdirs(full.parent());
    if (made.isError()) {
        return makeUnexpected(made.error());
    }
    auto written = fs_->writeAllText(full, text.value());
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    sceneDirty_ = false;
    return {};
}

Result<void> EditorDocument::loadScene(std::string_view scenePath)
{
    if (!hasProject()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "sem projeto aberto"));
    }
    if (mode_ == Mode::Play) {
        return makeUnexpected(documentError(
            StatusCode::InvalidState, "cena é somente-leitura em Play"));
    }
    if (!isSafeRelativePath(scenePath)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "path relativo obrigatório (sem ..)"));
    }
    const eng::fs::Path path{std::string(scenePath)};
    const eng::fs::Path full = scenesRootOf(*project_) / path;
    auto text = fs_->readAllText(full);
    if (text.isError()) {
        return makeUnexpected(text.error());
    }
    auto fresh = newScene();
    if (fresh.isError()) {
        return makeUnexpected(fresh.error());
    }
    auto loaded = eng::scene::SceneSerializer::load(*scene_, text.value());
    if (loaded.isError()) {
        return makeUnexpected(loaded.error());
    }
    sceneDirty_ = false;
    return {};
}

// =============================================================================
// Entidades (§8.2/§8.3)
// =============================================================================

Result<void> EditorDocument::requireEditMode() const
{
    if (mode_ == Mode::Play) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState,
                          "edição rejeitada: editor em PLAY (§8.7)"));
    }
    return {};
}

Result<eng::ecs::Entity> EditorDocument::createEntity(
    std::string_view name, eng::ecs::Entity parent)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    const eng::ecs::Entity entity = scene_->createNode();
    auto placed = scene_->world().emplace<eng::scene::Name>(
        entity, eng::scene::Name{name.empty() ? "Entity" : std::string(name)});
    if (placed == nullptr) {
        return makeUnexpected(
            documentError(StatusCode::Internal, "Name não emplantou"));
    }
    if (parent != eng::scene::kNoEntity) {
        auto attached = scene_->attach(entity, parent);
        if (!attached) {
            return makeUnexpected(documentError(
                StatusCode::InvalidArgument,
                "attach rejeitou o pai (inválido/ciclo)"));
        }
    }
    sceneDirty_ = true;
    return entity;
}

Result<void> EditorDocument::deleteEntity(eng::ecs::Entity entity)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (!scene_->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    if (!scene_->destroyNode(entity)) {
        return makeUnexpected(
            documentError(StatusCode::Internal, "destroyNode falhou"));
    }
    if (selection_.has_value() && *selection_ == entity) {
        selection_.reset();
    }
    sceneDirty_ = true;
    return {};
}

Result<void> EditorDocument::renameEntity(eng::ecs::Entity entity,
                                          std::string_view name)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (name.empty()) {
        return makeUnexpected(
            documentError(StatusCode::InvalidArgument, "nome vazio"));
    }
    auto* current = scene_->world().get<eng::scene::Name>(entity);
    if (current == nullptr) {
        current = scene_->world().emplace<eng::scene::Name>(
            entity, eng::scene::Name{std::string(name)});
        if (current == nullptr) {
            return makeUnexpected(documentError(StatusCode::NotFound,
                                                "entidade obsoleta"));
        }
    } else {
        current->value = std::string(name);
    }
    sceneDirty_ = true;
    return {};
}

Result<eng::ecs::Entity> EditorDocument::duplicateEntity(
    eng::ecs::Entity entity)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (!scene_->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }

    // Coleta a subárvore (ordem determinística por nível — collectSubtree
    // é privada; caminhada manual BFS equivalente via eachChild).
    std::vector<eng::ecs::Entity> subtree;
    subtree.push_back(entity);
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        scene_->eachChild(subtree[i], [&](eng::ecs::Entity child) {
            subtree.push_back(child);
        });
    }

    const std::string originalName = nameOf(*scene_, entity);
    std::string cloneName = originalName + ".alt";
    // Nome com sufixo repetido: numera para manter o rótulo legível.
    if (originalName.size() >= 4 &&
        originalName.compare(originalName.size() - 4, 4, ".alt") == 0) {
        cloneName = originalName + "2";
    }

    // Mapa velho→novo preservando a hierarquia.
    std::unordered_map<eng::ecs::Entity, eng::ecs::Entity> remap;
    for (const eng::ecs::Entity source : subtree) {
        const eng::ecs::Entity clone = scene_->createNode();
        remap[source] = clone;
    }
    for (const eng::ecs::Entity source : subtree) {
        const eng::ecs::Entity clone = remap.at(source);
        // Componentes por encode/decode do catálogo (Name/Transform/...).
        for (const auto& [typeName, entry] :
             eng::scene::detail::componentEntries()) {
            if (typeName == "eng::scene::Name") {
                continue; // tratado abaixo (nome único do clone)
            }
            if (!entry.has(scene_->world(), source)) {
                continue;
            }
            auto encoded = entry.encode(entry, scene_->world(), source);
            if (encoded.isError()) {
                ENG_WARN("duplicate: encode de '{}' falhou ({})", typeName,
                         encoded.error().message);
                continue;
            }
            auto decoded = entry.decodeAndEmplace(entry, scene_->world(),
                                                  clone, encoded.value());
            if (decoded.isError()) {
                ENG_WARN("duplicate: decode de '{}' falhou ({})", typeName,
                         decoded.error().message);
            }
        }
        const std::string sourceName = nameOf(*scene_, source);
        const std::string cloneNameThis =
            (source == entity) ? cloneName : sourceName;
        (void)scene_->world().emplace<eng::scene::Name>(
            clone, eng::scene::Name{cloneNameThis});

        // Hierarquia: mesmo pai (ou raiz se o original era raiz).
        const eng::ecs::Entity parent = scene_->parentOf(source);
        if (parent != eng::scene::kNoEntity) {
            const auto parentIt = remap.find(parent);
            const eng::ecs::Entity newParent =
                parentIt != remap.end() ? parentIt->second : parent;
            (void)scene_->attach(clone, newParent);
        }
    }

    sceneDirty_ = true;
    return remap.at(entity);
}

Result<void> EditorDocument::reparentEntity(eng::ecs::Entity entity,
                                            eng::ecs::Entity parent)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (parent != eng::scene::kNoEntity && !scene_->isNode(parent)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                           "novo pai obsoleto"));
    }
    if (parent == eng::scene::kNoEntity) {
        // Raiz = detach (Scene::attach exige pai válido; já-raiz é no-op).
        if (!scene_->detach(entity) &&
            scene_->parentOf(entity) != eng::scene::kNoEntity) {
            return makeUnexpected(
                documentError(StatusCode::InvalidArgument,
                              "entidade obsoleta"));
        }
    } else if (!scene_->attach(entity, parent)) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "attach rejeitou (inválido/self/ciclo — ADR-025)"));
    }
    sceneDirty_ = true;
    return {};
}

Result<TransformDesc> EditorDocument::transform(
    eng::ecs::Entity entity) const
{
    const auto* local = scene_->localTransform(entity);
    if (local == nullptr) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    TransformDesc desc;
    desc.position = local->position;
    desc.rotationDegrees = degreesFromQuat(local->rotation);
    desc.scale = local->scale;
    return desc;
}

Result<void> EditorDocument::setTransform(eng::ecs::Entity entity,
                                          const TransformDesc& desc)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    auto* local = scene_->localTransform(entity);
    if (local == nullptr) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    local->position = desc.position;
    local->rotation = quatFromDegrees(desc.rotationDegrees);
    local->scale = desc.scale;
    sceneDirty_ = true;
    return {};
}

// =============================================================================
// Seleção
// =============================================================================

Result<void> EditorDocument::select(eng::ecs::Entity entity)
{
    if (entity != eng::scene::kNoEntity && !scene_->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    if (entity == eng::scene::kNoEntity) {
        selection_.reset();
        return {};
    }
    selection_ = entity;
    return {};
}

void EditorDocument::deselect() noexcept
{
    selection_.reset();
}

bool EditorDocument::isSelected(eng::ecs::Entity entity) const noexcept
{
    return selection_.has_value() && *selection_ == entity;
}

// =============================================================================
// Componentes (§8.4)
// =============================================================================

std::vector<Inspector::Field> EditorDocument::inspectorFields(
    eng::ecs::Entity entity, std::string_view component) const
{
    // Leitura ALLOWED em Play (inspeção do runtime — ferramenta de debug).
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr) {
        return {};
    }
    auto fields = Inspector::fieldsOf(*scene, entity, component);
    if (fields.isError()) {
        ENG_WARN("inspector: {} (entity {}.{})", fields.error().message,
                 entity.index, entity.generation);
        return {};
    }
    return fields.value();
}

Result<void> EditorDocument::setInspectorField(eng::ecs::Entity entity,
                                               std::string_view component,
                                               std::string_view fieldPath,
                                               std::string_view value)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    auto written =
        Inspector::setField(*scene_, entity, component, fieldPath, value);
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    sceneDirty_ = true;
    return {};
}

Result<void> EditorDocument::addComponent(eng::ecs::Entity entity,
                                          std::string_view component)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    auto added = Inspector::addComponent(*scene_, entity, component);
    if (added.isError()) {
        return makeUnexpected(added.error());
    }
    sceneDirty_ = true;
    return {};
}

Result<void> EditorDocument::removeComponent(eng::ecs::Entity entity,
                                             std::string_view component)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    auto removed = Inspector::removeComponent(*scene_, entity, component);
    if (removed.isError()) {
        return makeUnexpected(removed.error());
    }
    sceneDirty_ = true;
    return {};
}

// =============================================================================
// Play/Stop (§8.7 — ADR-044)
// =============================================================================

Result<void> EditorDocument::play()
{
    if (mode_ == Mode::Play) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "já em Play"));
    }
    // Clone por serialização: o round-trip é teste da FASE 3; a edição
    // permanece intocada por construção (nenhum ponteiro compartilhado).
    auto snapshot = eng::scene::SceneSerializer::save(*scene_);
    if (snapshot.isError()) {
        return makeUnexpected(snapshot.error());
    }
    runtimeScene_.emplace();
    auto loaded =
        eng::scene::SceneSerializer::load(*runtimeScene_, snapshot.value());
    if (loaded.isError()) {
        runtimeScene_.reset();
        return makeUnexpected(loaded.error());
    }
    mode_ = Mode::Play;
    ENG_INFO("PLAY: runtime clone pronto ({} nós)", runtimeScene_->nodeCount());
    return {};
}

void EditorDocument::stop() noexcept
{
    if (mode_ == Mode::Play) {
        mode_ = Mode::Edit;
        runtimeScene_.reset();
        ENG_INFO("STOP: runtime descartado — edição intacta");
    }
}

void EditorDocument::tick(float deltaSeconds) noexcept
{
    // FASE 8: contrato do loop apenas. FASE 9 adiciona input/audio em Play;
    // FASE 10 adiciona physics/animation/particles. Sem trabalho agora —
    // SEM comportamento inventado (missão §13: não inventar sucesso).
    (void)deltaSeconds;
}

// =============================================================================
// Viewport (§8.6)
// =============================================================================

std::optional<eng::ecs::Entity> EditorDocument::viewportTap(float screenX,
                                                           float screenY)
{
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr) {
        return std::nullopt;
    }
    const auto quads = viewport_.buildQuads(*scene, selection_);
    auto hit = viewport_.hitTest(quads, screenX, screenY, 14.f);
    if (hit.has_value()) {
        // Seleção do EDITOR segue o foco (em Play seleciona no clone — a
        // seleção é visual e transitória; stop reseta).
        selection_ = *hit;
    } else {
        selection_.reset();
    }
    return hit;
}

void EditorDocument::viewportPan(float screenDx, float screenDy) noexcept
{
    viewport_.pan(screenDx, screenDy);
}

void EditorDocument::viewportZoom(float factor, float focusX,
                                  float focusY) noexcept
{
    viewport_.zoomAt(factor, focusX, focusY);
}

Result<void> EditorDocument::moveEntityScreen(eng::ecs::Entity entity,
                                              float screenDx, float screenDy)
{
    // Edit: move na EDIÇÃO (dirty). Play: move no CLONE (debug §8.7) —
    // a escrita é permitida em Play APENAS aqui (mutação de runtime).
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "sem cena"));
    }
    if (!scene->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    const float worldDx = screenDx / viewport_.camera().zoom;
    const float worldDy = -screenDy / viewport_.camera().zoom;
    auto* local =
        const_cast<eng::scene::Scene*>(scene)->localTransform(entity);
    if (local == nullptr) {
        return makeUnexpected(documentError(StatusCode::Internal,
                                           "sem Transform"));
    }
    local->position.x += worldDx;
    local->position.y += worldDy;
    if (mode_ == Mode::Edit) {
        sceneDirty_ = true;
    }
    return {};
}

// =============================================================================
// Hierarquia (§8.3)
// =============================================================================

std::vector<EditorDocument::HierarchyNode>
EditorDocument::hierarchySnapshot() const
{
    std::vector<HierarchyNode> nodes;
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr) {
        return nodes;
    }
    nodes.reserve(scene->nodeCount());

    const auto visit = [&](auto&& self, eng::ecs::Entity node,
                           int depth) -> void {
        nodes.push_back(HierarchyNode{node, nameOf(*scene, node), depth});
        scene->eachChild(node, [&](eng::ecs::Entity child) {
            self(self, child, depth + 1);
        });
    };

    std::vector<eng::ecs::Entity> roots;
    scene->world().each<eng::scene::Hierarchy>(
        [&](eng::ecs::Entity node, const eng::scene::Hierarchy& hierarchy) {
            if (hierarchy.parent == eng::scene::kNoEntity &&
                scene->isNode(node)) {
                roots.push_back(node);
            }
        });
    std::sort(roots.begin(), roots.end(),
              [](eng::ecs::Entity a, eng::ecs::Entity b) {
                  return a.index < b.index;
              });
    for (const eng::ecs::Entity root : roots) {
        visit(visit, root, 0);
    }
    return nodes;
}

eng::scene::Scene* EditorDocument::sceneInFocus() noexcept
{
    return mode_ == Mode::Play && runtimeScene_.has_value() ? &*runtimeScene_
                                                           : &*scene_;
}

const eng::scene::Scene* EditorDocument::sceneInFocus() const noexcept
{
    return mode_ == Mode::Play && runtimeScene_.has_value() ? &*runtimeScene_
                                                           : &*scene_;
}

std::string EditorDocument::nameOf(const eng::scene::Scene& scene,
                                  eng::ecs::Entity entity)
{
    const auto* name = scene.world().get<eng::scene::Name>(entity);
    if (name == nullptr || name->value.empty()) {
        return "Entity";
    }
    return name->value;
}

// =============================================================================
// Pack/unpack para JNI
// =============================================================================

std::uint64_t EditorDocument::packEntity(eng::ecs::Entity entity) noexcept
{
    // index+1 evita ambiguidade com 0 ("nenhuma") mesmo com geração 0.
    return (static_cast<std::uint64_t>(entity.index) + 1ull) << 32ull |
           static_cast<std::uint64_t>(entity.generation);
}

eng::ecs::Entity EditorDocument::unpackEntity(std::uint64_t packed) noexcept
{
    if (packed == 0ull) {
        return eng::scene::kNoEntity;
    }
    const std::uint32_t index =
        static_cast<std::uint32_t>((packed >> 32ull) - 1ull);
    const std::uint32_t generation = static_cast<std::uint32_t>(packed);
    return eng::ecs::Entity{index, generation};
}

}  // namespace eng::editor
