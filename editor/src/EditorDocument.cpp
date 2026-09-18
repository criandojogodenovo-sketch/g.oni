#include "eng/editor/EditorDocument.hpp"

/// EditorDocument — estado + comandos (FASE 8; separação editor×runtime
/// ADR-044: clone por serialização, edição rejeitada em Play).

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <utility>

#include "eng/animation/Animation.hpp"
#include "eng/log/Macros.hpp"
#include "eng/math/Mat4.hpp"
#include "eng/math/Quat.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/project/ProjectPaths.hpp"
#include "eng/editor/NiScriptComponent.hpp"
#include "eng/editor/SpriteData.hpp"
#include "eng/editor/NiRuntime.hpp"
#include "eng/scene/Name.hpp"
#include "eng/scene/SceneIdentity.hpp"
#include "eng/scene/SceneSerializer.hpp"
#include "eng/serial/Json.hpp"
#include "eng/editor/TextureCache.hpp"

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
    ensureEditorComponentsRegistered(); // FASE 10: catálogo de gameplay
    auto document = std::unique_ptr<EditorDocument>(new EditorDocument{});
    document->fs_ = &fs;
    document->workspaceRoot_ = workspaceRoot;
    // Contrato do header: "cena vazia PRONTA PARA EDIÇÃO" — o optional é
    // emitido aqui (bug C-3 da auditoria final: acessores como
    // sceneInFocus() faziam &*scene_ vazio → UB latente no estado
    // pré-projeto, mascarado pelo ensureProjectOnFirstRun da Activity).
    document->scene_.emplace();  // Scene não é movível — ADR-025
    document->niRuntime_ = std::make_unique<NiRuntime>(); // FASE 11
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
        ++selectionRevision_;  // seleção morreu com a entidade (P1.8)
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
    ++selectionRevision_;  // clone entrou na cena → hierarquia/inspector (P1.7)
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
    ++selectionRevision_;  // Inspector → viewport: mudou transform (P1.9)
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
        ++selectionRevision_;
        return {};
    }
    selection_ = entity;
    ++selectionRevision_;  // hierarquia selecionou → UI sincroniza (P1.9)
    return {};
}

void EditorDocument::deselect() noexcept
{
    selection_.reset();
    ++selectionRevision_;
}

bool EditorDocument::isSelected(eng::ecs::Entity entity) const noexcept
{
    return selection_.has_value() && *selection_ == entity;
}

// =============================================================================
// Ferramentas + gizmo (P1.3–P1.6) + sprite (P1.10)
// =============================================================================

GizmoBounds EditorDocument::selectionBounds(TextureCache* textures) const
{
    GizmoBounds bounds;
    if (!selection_.has_value() || mode_ != Mode::Edit) {
        return bounds;  // sem seleção/em Play: inválido (sem gizmo)
    }
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr || !scene->isNode(*selection_)) {
        return bounds;  // stale: seleção morreu (regressão P1.1)
    }
    auto quads = viewport_.buildQuads(*scene, selection_);
    for (const EntityQuad& quad : quads) {
        if (quad.entity != *selection_) {
            continue;
        }
        // P1.2: tamanho DESENHADO = escala × (região em px / ppu) para
        // sprites texturizados — o MESMO número do renderer/hit-test.
        float worldHalfW = quad.sizeX * 0.5f;
        float worldHalfH = quad.sizeY * 0.5f;
        if (!quad.textureAsset.empty() && textures != nullptr &&
            assets_ != nullptr) {
            const auto info =
                textures->imageInfo(*assets_, quad.textureAsset);
            if (info.valid) {
                const float regionPx =
                    static_cast<float>(info.width) * (quad.u1 - quad.u0);
                const float regionPy =
                    static_cast<float>(info.height) * (quad.v1 - quad.v0);
                const float ppu = quad.spritePpu > 0.f ? quad.spritePpu : 1.f;
                worldHalfW = quad.sizeX * regionPx / ppu * 0.5f;
                worldHalfH = quad.sizeY * regionPy / ppu * 0.5f;
            }
        }
        // Pivot desloca o centro visual do sprite (o renderer desenha o
        // quad com o offset do pivot — o bounds tem de casar).
        const float pivotOffX = (quad.pivotX - 0.5f) * worldHalfW * 2.f;
        const float pivotOffY = (quad.pivotY - 0.5f) * worldHalfH * 2.f;
        const float cosR = std::cos(quad.rotation);
        const float sinR = std::sin(quad.rotation);
        bounds.worldX = quad.worldX + pivotOffX * cosR - pivotOffY * sinR;
        bounds.worldY = quad.worldY + pivotOffX * sinR + pivotOffY * cosR;
        bounds.halfW = std::max(worldHalfW, pxToWorldMin());
        bounds.halfH = std::max(worldHalfH, pxToWorldMin());
        bounds.rotation = quad.rotation;
        bounds.valid = true;
        return bounds;
    }
    return bounds;
}

/// Meio-tamanho mínimo visível em mundo (handle sempre tocável).
float EditorDocument::pxToWorldMin() const noexcept
{
    const float zoom = viewport_.effectiveCamera().zoom;
    return zoom > 0.f ? Viewport::kMinQuadPixels * 0.5f / zoom : 0.5f;
}

GizmoHandle EditorDocument::gizmoDragBegin(float screenX, float screenY,
                                            TextureCache* textures)
{
    if (mode_ != Mode::Edit) {
        return GizmoHandle::None;  // edição é rejeitada em Play (§8.7)
    }
    const GizmoBounds bounds = selectionBounds(textures);
    if (!bounds.valid) {
        return GizmoHandle::None;
    }
    const GizmoHandle handle =
        gizmo_.hitTest(viewport_, tool_, bounds, screenX, screenY);
    if (handle == GizmoHandle::None) {
        return GizmoHandle::None;
    }
    // Transform INICIAL da EDIÇÃO (só em Edit — o gizmo não toca o clone).
    auto transform = this->transform(*selection_);
    if (transform.isError()) {
        return GizmoHandle::None;  // seleção stale no meio da operação
    }
    GizmoTransform start{};
    start.posX = transform.value().position.x;
    start.posY = transform.value().position.y;
    start.rotationDeg = transform.value().rotationDegrees.z;
    start.scaleX = transform.value().scale.x;
    start.scaleY = transform.value().scale.y;
    gizmo_.beginDrag(handle, start, viewport_, bounds, screenX, screenY);
    return handle;
}

Result<void> EditorDocument::gizmoDragTo(float screenX, float screenY)
{
    if (!gizmo_.dragging() || mode_ != Mode::Edit) {
        return {};
    }
    if (!selection_.has_value()) {
        gizmoDragEnd();
        return makeUnexpected(
            documentError(StatusCode::NotFound, "seleção perdida no drag"));
    }
    const GizmoBounds bounds = selectionBounds(nullptr);
    if (!bounds.valid) {
        gizmoDragEnd();
        return makeUnexpected(
            documentError(StatusCode::NotFound, "entidade obsoleta"));
    }
    const GizmoTransform target =
        gizmo_.dragTo(viewport_, bounds, screenX, screenY);

    // Aplica ao ECS REAL (posição/rotZ/escala XY — plano 2D).
    auto current = transform(*selection_);
    if (current.isError()) {
        gizmoDragEnd();
        return makeUnexpected(current.error());
    }
    TransformDesc desc = current.value();
    desc.position.x = target.posX;
    desc.position.y = target.posY;
    desc.rotationDegrees.z = target.rotationDeg;
    desc.scale.x = target.scaleX;
    desc.scale.y = target.scaleY;
    auto applied = setTransform(*selection_, desc);
    if (applied.isError()) {
        gizmoDragEnd();
        return applied;
    }
    ++selectionRevision_;  // Inspector atualiza ao vivo (P1.9)
    return {};
}

void EditorDocument::gizmoDragEnd() noexcept
{
    gizmo_.endDrag();
}

GizmoDrawData EditorDocument::gizmoDraw(TextureCache* textures) const
{
    GizmoDrawData draw;
    if (mode_ != Mode::Edit || tool_ == EditorTool::Select) {
        return draw;
    }
    const GizmoBounds bounds = selectionBounds(textures);
    if (!bounds.valid) {
        return draw;
    }
    draw.quads = gizmo_.layoutQuads(viewport_, tool_, bounds);
    draw.segments = gizmo_.layoutSegments(viewport_, tool_, bounds);
    return draw;
}

Result<eng::ecs::Entity> EditorDocument::createSprite(std::string_view name)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    // Numeração automática: Sprite, Sprite 2, Sprite 3… (varre a
    // hierarquia — nomes únicos mantêm a UI legível; P1.10).
    std::string base{name.empty() ? "Sprite" : std::string(name)};
    auto nodes = hierarchySnapshot();
    if (std::any_of(nodes.begin(), nodes.end(),
                    [&](const HierarchyNode& n) { return n.name == base; })) {
        for (int suffix = 2;; ++suffix) {
            const std::string candidate = base + " " + std::to_string(suffix);
            if (!std::any_of(nodes.begin(), nodes.end(),
                             [&](const HierarchyNode& n) {
                                 return n.name == candidate;
                             })) {
                base = candidate;
                break;
            }
        }
    }
    auto entity = createEntity(base, eng::scene::kNoEntity);
    if (entity.isError()) {
        return makeUnexpected(entity.error());
    }
    // SpriteData default: ppu 48 (1 texel : 1 px no zoom padrão), sem
    // textura → o renderer desenha o PLACEHOLDER xadrez (P1.10 — não
    // confundir placeholder com sprite renderizado).
    if (scene_->world().emplace<eng::editor::SpriteData>(
            entity.value(), eng::editor::SpriteData{}) == nullptr) {
        (void)scene_->destroyNode(entity.value());
        return makeUnexpected(documentError(StatusCode::Internal,
                                            "SpriteData não emplantou"));
    }
    selection_ = entity.value();
    ++selectionRevision_;
    sceneDirty_ = true;
    return entity;
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
    // Handles de EDIÇÃO chegam aqui (seleção pré-Play, JNI); o clone tem
    // os PRÓPRIOS — traduz na fronteira (bug do clone aleatório).
    auto fields = Inspector::fieldsOf(*scene, toFocus(entity), component);
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
    // P1.9 (BUG REAL): Transform é TRS com rotação em QUAT — escrever
    // "rotation.z=30" direto no campo produzia um quat inválido que
    // decomponha para ~178° (graus viravam componente de quat). TODA
    // escrita de Transform pela UI passa pela API TRS (graus ↔ quat na
    // MESMA convenção do gizmo/setTransform) — uma fonte de verdade.
    if (component == "eng::math::Transform") {
        return setTransformField(entity, fieldPath, value);
    }
    auto written =
        Inspector::setField(*scene_, entity, component, fieldPath, value);
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    sceneDirty_ = true;
    return {};
}

namespace {

/// Campo TRS de "eng::math::Transform" (posição/rotação/escala × x/y/z).
[[nodiscard]] bool parseTransformField(std::string_view fieldPath,
                                       std::string_view value, int& axis,
                                       float& out)
{
    // fieldPath vem como "position.x" | "rotation.y" | "scale.z".
    constexpr std::string_view kPrefixes[3] = {"position.", "rotation.",
                                              "scale."};
    int group = -1;
    for (int i = 0; i < 3; ++i) {
        if (fieldPath.substr(0, kPrefixes[i].size()) == kPrefixes[i]) {
            group = i;
            fieldPath.remove_prefix(kPrefixes[i].size());
            break;
        }
    }
    if (group < 0 || fieldPath.size() != 1) {
        return false;
    }
    const char c = fieldPath[0];
    if (c != 'x' && c != 'y' && c != 'z') {
        return false;
    }
    axis = (group << 2) | (c == 'x' ? 0 : (c == 'y' ? 1 : 2));
    // Parse float estrito SEM exceções (lib é -fno-exceptions — ADR-004):
    // strtof + fim-da-string; rejeita lixo/NaN/inf (contrato Inspector).
    const std::string text{value};
    const char* begin = text.c_str();
    char* end = nullptr;
    out = std::strtof(begin, &end);
    return end != begin && *end == '\0' && std::isfinite(out);
}

}  // namespace

Result<void> EditorDocument::setTransformField(eng::ecs::Entity entity,
                                                std::string_view fieldPath,
                                                std::string_view value)
{
    int axis = -1;
    float v = 0.f;
    if (parseTransformField(fieldPath, value, axis, v)) {
        auto current = transform(entity);
        if (current.isError()) {
            return makeUnexpected(current.error());
        }
        TransformDesc desc = current.value();
        switch (axis) {
        // position (0-2)
        case (0 << 2) | 0: desc.position.x = v; break;
        case (0 << 2) | 1: desc.position.y = v; break;
        case (0 << 2) | 2: desc.position.z = v; break;
        // rotation em GRAUS (3-5) — quatFromDegrees na escrita
        case (1 << 2) | 0: desc.rotationDegrees.x = v; break;
        case (1 << 2) | 1: desc.rotationDegrees.y = v; break;
        case (1 << 2) | 2: desc.rotationDegrees.z = v; break;
        // scale (6-8) — P1.5: impedir valores inválidos (0/neg/NaN)
        case (2 << 2) | 0:
            desc.scale.x = std::clamp(
                v, eng::editor::TransformGizmo::kScaleMin,
                eng::editor::TransformGizmo::kScaleMax);
            break;
        case (2 << 2) | 1:
            desc.scale.y = std::clamp(
                v, eng::editor::TransformGizmo::kScaleMin,
                eng::editor::TransformGizmo::kScaleMax);
            break;
        case (2 << 2) | 2:
            desc.scale.z = std::clamp(
                v, eng::editor::TransformGizmo::kScaleMin,
                eng::editor::TransformGizmo::kScaleMax);
            break;
        default: break;
        }
        return setTransform(entity, desc);  // dirty + revision bump
    }
    // Campo não-TRS (não existe hoje): cai no caminho genérico.
    auto written =
        Inspector::setField(*scene_, entity, "eng::math::Transform",
                            fieldPath, value);
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
// Play/Stop (§8.7, ADR-044) + Tick architecture (P0-5, ADR-051)
// =============================================================================

namespace {

/// ScriptTick (evolução P0-5): NI-Script no agendador. Vive no EDITOR
/// porque depende de NiRuntime (camada de composição — mesmo padrão do
/// catálogo de componentes, ADR-043).
class ScriptTick final : public eng::tick::TickSystem {
public:
    explicit ScriptTick(NiRuntime& runtime) noexcept : runtime_(runtime) {}

    [[nodiscard]] const char* name() const override { return "ScriptTick"; }
    [[nodiscard]] eng::tick::Phase phase() const override
    {
        return eng::tick::Phase::Update;
    }
    [[nodiscard]] int order() const override { return 30; }

    void tick(eng::scene::Scene& /*scene*/, float dt) override
    {
        if (runtime_.empty()) {
            return;
        }
        runtime_.tick(dt);
    }

private:
    NiRuntime& runtime_;
};

}  // namespace

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
    // FASE 11: scripts do clone compilam/instanciam AGORA (ADR-044 — a
    // edição nunca é tocada); @init roda na criação, `up start` a seguir.
    niRuntime_->setActionQuery(
        [](std::string_view action, int phase, void* user) {
            auto* input = static_cast<eng::input::InputSystem*>(user);
            const eng::input::ActionState state = input->action(action);
            return phase == 0 ? state.down
                   : phase == 1 ? state.pressed
                                : state.released;
        },
        &runtimeInput_);
    niRuntime_->start(*runtimeScene_);
    niRuntime_->fireStart();

    // BUG DO CLONE ALEATÓRIO (pego pelo teste §10 — flaky ~40%): o save
    // ordena entidades por SceneEntityId (ADR-033, byte-estável) e o
    // load recria nessa ordem — UUID é aleatório, então os ÍNDICES do
    // clone NÃO correspondem aos da edição. Handles de edição usados
    // contra o clone endereçavam a entidade ERRADA (inspector/move em
    // Play liam outra entidade). Correção na fronteira CERTA: o mapa
    // edição→runtime vivo enquanto o clone existir; leitores do foco
    // traduzem por toFocus(). A seleção pré-Play é REMAPEADA (continua
    // selecionada no clone — o que o usuário esperava ao apertar Play).
    editToRuntime_.clear();
    {
        std::unordered_map<eng::scene::SceneEntityId, eng::ecs::Entity>
            runtimeById;
        runtimeScene_->world().each<eng::scene::SceneIdentity>(
            [&](eng::ecs::Entity runtimeEntity,
                const eng::scene::SceneIdentity& identity) {
                runtimeById[identity.id] = runtimeEntity;
            });
        scene_->world().each<eng::scene::SceneIdentity>(
            [&](eng::ecs::Entity editEntity,
                const eng::scene::SceneIdentity& identity) {
                const auto it = runtimeById.find(identity.id);
                if (it != runtimeById.end()) {
                    editToRuntime_[editEntity] = it->second;
                }
            });
    }
    if (selection_.has_value()) {
        const auto mapped = editToRuntime_.find(*selection_);
        if (mapped != editToRuntime_.end()) {
            selection_ = mapped->second;
        }
    }

    // Evolução P0-5 (ADR-051): o frame do jogo é o TICK SCHEDULER —
    // sistemas ordenados por (fase, ordem, inserção). Mesma ordem de
    // execução de antes (física → animação → partículas → scripts →
    // câmera), agora DECLARADA, testável e extensível.
    scheduler_ = std::make_unique<eng::tick::TickScheduler>();
    (void)scheduler_->addSystem(std::make_unique<eng::tick::PhysicsTick>(
        physicsWorld_, physicsAccumulator_));
    (void)scheduler_->addSystem(
        std::make_unique<eng::tick::AnimationTick>(runtimeAnimations_));
    (void)scheduler_->addSystem(std::make_unique<eng::tick::ParticleTick>());
    (void)scheduler_->addSystem(
        std::make_unique<ScriptTick>(*niRuntime_));
    (void)scheduler_->addSystem(
        std::make_unique<eng::tick::CameraTickSystem>());

    mode_ = Mode::Play;
    // Câmera de jogo resolvida SEM rodar o frame: `up update` (e qualquer
    // sistema com efeito) só roda em tick() explícito do host — contrato
    // FASE 11 (play() não avança o mundo). O CameraTick ainda não tem
    // cache (nenhum frame rodou): resolução direta (ADR-051).
    syncGameCamera(eng::tick::resolveActiveCamera(*runtimeScene_));
    {
        std::string ticks;
        for (const std::string& name : scheduler_->systemOrder()) {
            ticks += ticks.empty() ? name : ", " + name;
        }
        ENG_INFO("PLAY: runtime clone pronto ({} nós, {} scripts; ticks: {})",
                 runtimeScene_->nodeCount(), niRuntime_->size(), ticks);
    }
    return {};
}

void EditorDocument::stop() noexcept
{
    if (mode_ == Mode::Play) {
        mode_ = Mode::Edit;
        scheduler_.reset();  // ticks morrem com o clone (ADR-051)
        gameCameraActive_ = false;
        viewport_.setGameCamera(nullptr);  // câmera do editor volta
        niRuntime_->shutdown(); // `up destroy` + descarte (bindings morrem
                                // JUNTOS com o clone — ADR-044)
        runtimeScene_.reset();
        // Seleção pode apontar o CLONE (tap em Play) — handle órfão na
        // edição. O contrato documentado do viewportTap ("stop reseta")
        // agora é REAL: seleção limpa no retorno à edição.
        selection_.reset();
        ++selectionRevision_;  // UI percebe o reset (P1.9)
        editToRuntime_.clear();
        ENG_INFO("STOP: runtime descartado — edição intacta");
    }
}

void EditorDocument::tick(float deltaSeconds) noexcept
{
    // Em Edit o runtime fica PARADO (gestos do editor não vazam — §6.4).
    if (mode_ != Mode::Play) {
        return;
    }
    // FASE 9 (§6.1): input com janela de um update por frame.
    runtimeInput_.update();

    // Evolução P0-5 (ADR-051): frame completo pelo TickScheduler —
    // física (timestep fixo), animação, partículas, scripts e câmera
    // nas fases/ordens declaradas no play(). Determinismo: a ordem é
    // fixa e cada sistema vê o estado deixado pelos anteriores.
    scheduler_->runFrame(*runtimeScene_, deltaSeconds);

    // Câmera de jogo (P0-5): o CameraTick cacheou a ativa no frame; o
    // viewport passa a ver POR ELA (render/hit-test/arraste seguem).
    const auto* cameraSystem = static_cast<const eng::tick::CameraTickSystem*>(
        scheduler_->find("CameraTick"));
    syncGameCamera(
        cameraSystem != nullptr
            ? cameraSystem->activeCamera()
            : eng::tick::resolveActiveCamera(*runtimeScene_));
}

void EditorDocument::syncGameCamera(
    const eng::tick::ActiveCamera& active) noexcept
{
    if (active.found()) {
        gameCamera_.posX = active.data.posX;
        gameCamera_.posY = active.data.posY;
        gameCamera_.zoom =
            active.data.zoom > 0.f ? active.data.zoom : 48.f;
        viewport_.setGameCamera(&gameCamera_);
        gameCameraActive_ = true;
    } else {
        viewport_.setGameCamera(nullptr);
        gameCameraActive_ = false;
    }
}

void EditorDocument::gameTouch(int canonicalPhase, std::uint32_t pointerId,
                               float x, float y, float pressure)
{
    eng::input::InputEvent event;
    event.device = eng::input::DeviceKind::Touch;
    event.pointerId = pointerId;
    switch (canonicalPhase) {
    case 0: event.touchPhase = eng::input::TouchPhase::Down; break;
    case 1: event.touchPhase = eng::input::TouchPhase::Move; break;
    case 2: event.touchPhase = eng::input::TouchPhase::Up; break;
    default: event.touchPhase = eng::input::TouchPhase::Cancelled; break;
    }
    event.x = x;
    event.y = y;
    event.pressure = pressure;
    runtimeInput_.queueEvent(event);
}

void EditorDocument::setGameViewportSize(float width, float height) noexcept
{
    runtimeInput_.setScreenSize(width, height);
}

// =============================================================================
// Viewport (§8.6)
// =============================================================================

std::optional<eng::ecs::Entity> EditorDocument::viewportTap(
    float screenX, float screenY, TextureCache* textures)
{
    const eng::scene::Scene* scene = sceneInFocus();
    if (scene == nullptr) {
        return std::nullopt;
    }
    auto quads = viewport_.buildQuads(*scene, selection_);
    // RECOVERY P0: resolve as dimensões em PIXELS das texturas dos sprites
    // (decode sem GPU, cacheado pelo TextureCache do host) — o hit-test
    // precisa do tamanho DESENHADO, não da escala local. Sem cache (tests/
    // hosts sem texturas), os quads seguem com dimensões 0 e o hit-test usa
    // o caminho da escala (comportamento anterior).
    if (textures != nullptr && assets_ != nullptr) {
        for (auto& quad : quads) {
            if (quad.textureAsset.empty() || quad.textureWidthPx > 0u) {
                continue;
            }
            const auto info =
                textures->imageInfo(*assets_, quad.textureAsset);
            if (info.valid) {
                quad.textureWidthPx =
                    static_cast<std::uint32_t>(info.width);
                quad.textureHeightPx =
                    static_cast<std::uint32_t>(info.height);
            }
        }
    }
    auto hit = viewport_.hitTest(quads, screenX, screenY, 14.f);
    if (hit.has_value()) {
        // Seleção do EDITOR segue o foco (em Play seleciona no clone — a
        // seleção é visual e transitória; stop reseta).
        selection_ = *hit;
    } else {
        selection_.reset();
    }
    ++selectionRevision_;  // tap mudou o estado → Inspector segue (P1.9)
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
    // Handle de edição → handle do clone (mesma tradução do inspector).
    const eng::ecs::Entity focusEntity = toFocus(entity);
    if (!scene->isNode(focusEntity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    // Câmera EM FOCO (P1): em Play sob câmera de jogo o arraste-debug
    // precisa do zoom que o usuário está VENDO, não o do editor.
    const float zoom = viewport_.effectiveCamera().zoom;
    const float worldDx = screenDx / zoom;
    const float worldDy = -screenDy / zoom;
    auto* local =
        const_cast<eng::scene::Scene*>(scene)->localTransform(focusEntity);
    if (local == nullptr) {
        return makeUnexpected(documentError(StatusCode::Internal,
                                           "sem Transform"));
    }
    local->position.x += worldDx;
    local->position.y += worldDy;
    if (mode_ == Mode::Edit) {
        sceneDirty_ = true;
        ++selectionRevision_;  // viewport → Inspector: drag move (P1.9)
    }
    return {};
}

// =============================================================================
// Tradução de handles (bug do clone aleatório — play/stop)
// =============================================================================

eng::ecs::Entity EditorDocument::toFocus(eng::ecs::Entity entity) const noexcept
{
    if (mode_ != Mode::Play || editToRuntime_.empty()) {
        return entity;
    }
    const auto it = editToRuntime_.find(entity);
    return it != editToRuntime_.end() ? it->second : entity;
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

// =============================================================================
// Scripts NI-Script (evolução P0-7, ADR-053)
// =============================================================================

namespace {

/// Nome de script válido: não-vazio, sem '/', sem '..', sem '\0'.
/// A extensão .nis é forçada por scriptCreate; aqui aceita-se o nome
/// COMO ESTÁ (read/write casam com o que listou).
[[nodiscard]] bool isValidScriptName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 128) {
        return false;
    }
    if (name.find("..") != std::string_view::npos ||
        name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos ||
        name.find('\0') != std::string_view::npos) {
        return false;
    }
    return true;
}

/// Garante extensão .nis (adiciona quando ausente).
[[nodiscard]] std::string withNisExtension(std::string_view name)
{
    std::string out(name);
    if (out.size() < 4 || out.compare(out.size() - 4, 4, ".nis") != 0) {
        out += ".nis";
    }
    return out;
}

/// Template canônico de script novo: compila LIMPO (o teste do editor
/// PROVA via scriptCompile), sintaxe idêntica aos casos da FASE 11.
[[nodiscard]] std::string defaultScriptTemplate()
{
    return "# Script NI-Script do G.ONI\n"
           "# Linguagem: docs/ni-script/ (indentacao por blocos, 'stop' fecha)\n"
           "\n"
           "add &BL\n"
           "\n"
           "var speed: float = 2.0\n"
           "var ticks: int = 0\n"
           "\n"
           "up start:\n"
           "    # roda uma vez ao entrar em PLAY\n"
           "stop\n"
           "\n"
           "up update:\n"
           "    # roda por frame — exemplo: move a entidade no eixo X\n"
           "    var me = self()\n"
           "    me.position.x = me.position.x + speed\n"
           "    ticks = ticks + 1\n"
           "stop\n"
           "\n"
           "up destroy:\n"
           "    # limpeza ao sair do PLAY\n"
           "stop\n";
}

}  // namespace

Result<std::vector<std::string>> EditorDocument::scriptList() const
{
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(
            StatusCode::InvalidState, "nenhum projeto aberto"));
    }
    auto listed = assets_->list("scripts");
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    std::vector<std::string> names;
    names.reserve(listed.value().size());
    for (const auto& entry : listed.value()) {
        names.push_back(entry.name);
    }
    return names;
}

Result<std::string> EditorDocument::scriptRead(std::string_view name) const
{
    if (!isValidScriptName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de script invalido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto bytes = assets_->read("scripts", name);
    if (bytes.isError()) {
        return makeUnexpected(bytes.error());
    }
    std::string text;
    text.reserve(bytes.value().size());
    for (const std::byte b : bytes.value()) {
        text.push_back(static_cast<char>(b));
    }
    return text;
}

Result<void> EditorDocument::scriptWrite(std::string_view name,
                                         std::string_view content)
{
    if (!isValidScriptName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de script invalido"));
    }
    if (assets_ == nullptr || !project_.has_value()) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    const eng::fs::Path path = project_->paths().assetsRoot() /
                               eng::fs::Path{"scripts"} /
                               eng::fs::Path{std::string(name)};
    if (path.isAbsolute()) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "caminho absoluto proibido"));
    }
    auto existed = fs_->exists(path);
    if (existed.isError()) {
        return makeUnexpected(existed.error());
    }
    auto written = fs_->writeAllText(path, std::string(content));
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    if (!existed.value()) {
        // Arquivo NOVO: cataloga no registry (upsert + persist).
        auto registered = assets_->registerExisting("scripts", name);
        if (registered.isError()) {
            // O arquivo existe mas o meta não persistiu — erro real
            // (o browser não listaria o script).
            return makeUnexpected(registered.error());
        }
    }
    return {};
}

Result<void> EditorDocument::scriptCreate(std::string_view rawName)
{
    if (!isValidScriptName(rawName)) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "nome de script invalido (vazio/caracteres proibidos)"));
    }
    const std::string name = withNisExtension(rawName);
    if (assets_ == nullptr || !project_.has_value()) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto listed = scriptList();
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    for (const auto& existing : listed.value()) {
        if (existing == name) {
            return makeUnexpected(documentError(
                StatusCode::AlreadyExists,
                "script '" + name + "' ja existe"));
        }
    }
    return scriptWrite(name, defaultScriptTemplate());
}

Result<void> EditorDocument::scriptDelete(std::string_view name)
{
    if (!isValidScriptName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de script invalido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    // AssetBrowser::remove apaga arquivo + meta e persiste.
    return assets_->remove("scripts", name);
}

Result<EditorDocument::ScriptCheck> EditorDocument::scriptCompile(
    std::string_view source) const
{
    // MESMA tabela visível ao runtime de Play (NiRuntime::start) — o que
    // valida aqui é o que o jogo vai compilar lá.
    eng::ni::NiNativeTable natives;
    natives.addBaseLibrary();
    natives.addStandardHost();

    ScriptCheck check;
    std::vector<eng::ni::NiDiag> diags;
    const eng::ni::CompileOptions options{&natives};
    auto program = eng::ni::compile(source, options, &diags);
    check.ok = static_cast<bool>(program);
    check.diags.reserve(diags.size());
    for (const auto& diag : diags) {
        check.diags.push_back(
            ScriptDiag{diag.line, diag.col, diag.message});
    }
    return check;
}

Result<void> EditorDocument::scriptAssign(eng::ecs::Entity entity,
                                           std::string_view name)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (!scene_->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                            "entidade obsoleta"));
    }
    auto content = scriptRead(name);
    if (content.isError()) {
        return makeUnexpected(content.error());
    }
    // Caminho pelo catálogo ÚNICO (mesma via do Inspector): componente
    // presente → escreve source; ausente → adiciona default e escreve.
    if (!scene_->world().has<eng::editor::NiScriptComponent>(entity)) {
        auto added = Inspector::addComponent(
            *scene_, entity, "eng::editor::NiScriptComponent");
        if (added.isError()) {
            return makeUnexpected(added.error());
        }
    }
    auto written = Inspector::setField(
        *scene_, entity, "eng::editor::NiScriptComponent", "source",
        content.value());
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    sceneDirty_ = true;
    ENG_INFO("script anexado: {} ({} bytes) → entidade {}", name,
             content.value().size(), entity.index);
    return {};
}

}  // namespace eng::editor
