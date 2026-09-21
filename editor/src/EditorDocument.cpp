#include "eng/editor/EditorDocument.hpp"

/// EditorDocument — estado + comandos (FASE 8; separação editor×runtime
/// ADR-044: clone por serialização, edição rejeitada em Play).

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <utility>

#include "eng/animation/Animation.hpp"
#include "eng/audio/Wav.hpp"
#include "eng/log/Macros.hpp"
#include "eng/math/Mat4.hpp"
#include "eng/math/Quat.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/project/ProjectPaths.hpp"
#include "eng/editor/AnimationAssets.hpp"
#include "eng/editor/AudioSource.hpp"
#include "eng/editor/NiScriptComponent.hpp"
#include "eng/editor/SpriteData.hpp"
#include "eng/render/Light2D.hpp"
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
    materialCache_.clear();  // projeto novo: materiais novos
    auto loaded = assets_->loadRegistry();
    if (loaded.isError()) {
        return makeUnexpected(loaded.error());
    }

    auto fresh = newScene();
    if (fresh.isError()) {
        return makeUnexpected(fresh.error());
    }
    // Diagnóstico (P3 §0): operação/projeto/caminho para o logcat.
    ENG_INFO("project-op: criar | projeto='{}' | caminho='{}'", name,
             (workspaceRoot_ / eng::fs::Path{std::string(name)}).str());
    (void)rememberLastUsedProject(name);  // best-effort (logado dentro)
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
    materialCache_.clear();  // projeto aberto: materiais do anterior não valem
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
    // Diagnóstico (P3 §0): operação/projeto/caminho/estado para o logcat.
    ENG_INFO(
        "project-op: abrir | projeto='{}' | caminho='{}' | doc.hasProject={}",
        project_->config.name, file.str(), project_.has_value());
    // Registra o NOME DA PASTA (não config.name — settings renomeia o
    // config mas não a pasta; o restore precisa do nome que EXISTE no
    // disco para listar/abrir).
    (void)rememberLastUsedProject(projectRoot.filename().str());
    return {};
}

// =============================================================================
// Startup (bug Android "AlreadyExists" — P3 §0)
// =============================================================================

namespace {

/// Nome do projeto default criado numa instalação limpa (§8.1).
constexpr std::string_view kDefaultProjectName{"MeuJogo"};
/// Registro do último projeto usado (raiz do workspace — oculto).
constexpr std::string_view kLastProjectFile{".goni_last_project"};

}  // namespace

Result<std::vector<std::string>> EditorDocument::listProjects() const
{
    auto entries = fs_->list(workspaceRoot_, false);
    if (entries.isError()) {
        // Workspace ausente = instalação limpa SEM projetos (não é erro
        // de I/O — o host cria o diretório no primeiro uso).
        if (entries.error().code == StatusCode::NotFound) {
            return std::vector<std::string>{};
        }
        return makeUnexpected(entries.error());
    }
    std::vector<std::string> names{};
    for (const auto& entry : entries.value()) {
        if (!entry.isDirectory) {
            continue;
        }
        const std::string name{entry.path.filename().str()};
        // Ocultos (staging SAF ".import_tmp", marcadores internos) não
        // são projetos — mesmo filtro do seletor de projetos da Activity.
        if (name.empty() || name.front() == '.') {
            continue;
        }
        auto marker = fs_->exists(
            entry.path / eng::fs::Path{"project.goni.json"});
        if (marker.isError() || !marker.value()) {
            continue;  // diretório comum (lixo/não-projeto): ignora
        }
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());  // determinístico
    return names;
}

std::string EditorDocument::lastUsedProject() const
{
    auto text =
        fs_->readAllText(workspaceRoot_ / eng::fs::Path{kLastProjectFile});
    if (text.isError()) {
        return {};
    }
    // Sem newline/espaço — o registro é uma linha crua; trim defensivo.
    std::string_view name{text.value()};
    while (!name.empty() &&
           (name.front() == '\n' || name.front() == '\r' ||
            name.front() == ' ')) {
        name.remove_prefix(1);
    }
    while (!name.empty() &&
           (name.back() == '\n' || name.back() == '\r' ||
            name.back() == ' ')) {
        name.remove_suffix(1);
    }
    return std::string{name};
}

Result<void> EditorDocument::rememberLastUsedProject(std::string_view name)
{
    // Best-effort por DESIGN: falhar em lembrar não pode derrubar a
    // operação de projeto (o fallback é abrir o default/primeiro).
    const auto made = fs_->mkdirs(workspaceRoot_);
    if (made.isError()) {
        ENG_WARN("startup: não criou raiz do workspace p/ registro: {}",
                 made.error().message);
        return makeUnexpected(made.error());
    }
    auto written = fs_->writeAllText(
        workspaceRoot_ / eng::fs::Path{kLastProjectFile}, name);
    if (written.isError()) {
        ENG_WARN("startup: falha ao registrar último projeto: {}",
                 written.error().message);
        return makeUnexpected(written.error());
    }
    return {};
}

Result<std::string> EditorDocument::ensureStartupProject()
{
    // Caso 1: projeto JÁ em memória (reentrada na mesma sessão) — no-op.
    if (hasProject()) {
        ENG_INFO("startup: projeto já em memória ('{}') — no-op",
                 project_->config.name);
        return project_->config.name;
    }
    auto listed = listProjects();
    if (listed.isError()) {
        ENG_ERROR("startup: falha ao listar workspace: {}",
                  listed.error().message);
        return makeUnexpected(listed.error());
    }
    const auto& projects = listed.value();

    // Caso 2: workspace vazio (instalação limpa) — cria o default.
    if (projects.empty()) {
        ENG_INFO("startup: workspace sem projetos — criando default '{}'",
                 kDefaultProjectName);
        auto created = newProject(kDefaultProjectName);
        if (created.isError()) {
            ENG_ERROR("startup: criação do default falhou: {}",
                      created.error().message);
            return makeUnexpected(created.error());
        }
        return std::string{kDefaultProjectName};
    }

    // Caso 3: projetos existem — ABRE (nunca cria sobre existente).
    // Preferência: último usado → default → primeiro (alfabético).
    std::string last = lastUsedProject();
    if (std::find(projects.begin(), projects.end(), last) == projects.end()) {
        last.clear();  // registro ausente/stale: não vale
    }
    std::string chosen{last};
    if (chosen.empty() &&
        std::find(projects.begin(), projects.end(),
                  std::string{kDefaultProjectName}) != projects.end()) {
        chosen = std::string{kDefaultProjectName};
    }
    if (chosen.empty()) {
        chosen = projects.front();
    }
    ENG_INFO(
        "startup: {} projeto(s) no workspace — abrindo '{}' (último usado: "
        "'{}')",
        projects.size(), chosen, last.empty() ? "-" : last);
    auto opened = openProject(eng::fs::Path{chosen});
    if (opened.isError()) {
        ENG_ERROR("startup: falha ao abrir '{}': {}", chosen,
                  opened.error().message);
        return makeUnexpected(opened.error());
    }
    return chosen;
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
    // P4.1 (T1/D1 — re-armo determinístico): mudança de seleção MATA o
    // drag em voo. Nenhum estado de drag sobrevive — o gizmo é
    // reconstruído do (seleção, ferramenta, câmera) a cada frame e o
    // beginDrag é a ÚNICA forma de armá-lo.
    gizmoDragEnd();
    selection_ = entity;
    ++selectionRevision_;  // hierarquia selecionou → UI sincroniza (P1.9)
    return {};
}

void EditorDocument::deselect() noexcept
{
    gizmoDragEnd();  // P4.1 (T1/D1): re-armo — drag não sobrevive
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
        // P2 (bug §5): origem do NÓ separada do centro visual — o MOVE
        // opera sobre a ORIGEM (o que o Transform guarda); rotate/scale
        // continuam no centro visual (pivot).
        bounds.originX = quad.worldX;
        bounds.originY = quad.worldY;
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
    // P2 (bug §5, R2): o gizmo opera em MUNDO — a origem do nó vem dos
    // bounds ATUAIS (não do transform local, que só coincide na raiz).
    start.posX = bounds.originX;
    start.posY = bounds.originY;
    start.rotationDeg = transform.value().rotationDegrees.z;
    start.scaleX = transform.value().scale.x;
    start.scaleY = transform.value().scale.y;
    gizmo_.beginDrag(handle, start, viewport_, bounds, screenX, screenY);

    // Contexto do drag (vivo até gizmoDragEnd): mesmas texturas do begin
    // (R1 — bounds consistentes entre begin E dragTo) + inversa 2x2 do
    // pai (R2 — delta de mundo → espaço local do filho).
    dragTextures_ = textures;
    dragParentInv_ = parentInverse2D(*selection_);
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
    // R1 (bug §5): MESMA fonte de bounds do beginDrag — o cache de
    // texturas do drag, capturado no begin. Antes: nullptr aqui e
    // texturizado no begin → com pivot != (0.5,0.5) o centro de
    // referência do rotate/scale MUDAVA no meio do drag.
    const GizmoBounds bounds = selectionBounds(dragTextures_);
    if (!bounds.valid) {
        gizmoDragEnd();
        return makeUnexpected(
            documentError(StatusCode::NotFound, "entidade obsoleta"));
    }
    const GizmoTransform target =
        gizmo_.dragTo(viewport_, bounds, screenX, screenY);

    // Aplica ao ECS REAL. R2 (bug §5): o alvo do MOVE está em MUNDO —
    // converte o delta pela INVERSA do pai (raiz: identidade). Somar o
    // delta de mundo direto na posição LOCAL movia filhos de pais
    // rotacionados no EIXO ERRADO da tela.
    auto current = transform(*selection_);
    if (current.isError()) {
        gizmoDragEnd();
        return makeUnexpected(current.error());
    }
    TransformDesc desc = current.value();
    const float worldDeltaX = target.posX - bounds.originX;
    const float worldDeltaY = target.posY - bounds.originY;
    desc.position.x += dragParentInv_[0] * worldDeltaX +
                       dragParentInv_[1] * worldDeltaY;
    desc.position.y += dragParentInv_[2] * worldDeltaX +
                       dragParentInv_[3] * worldDeltaY;
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
    dragTextures_ = nullptr;     // contexto do drag morre com o drag (§5)
    dragParentInv_ = {1.f, 0.f, 0.f, 1.f};
}

/// Inversa 2x2 da parte LINEAR do world matrix do PAI (identidade na
/// raiz). Converte deltas de MUNDO → espaço LOCAL do filho (bug §5 R2):
/// column-major → m00=at(0,0), m01=at(1,0), m10=at(0,1), m11=at(1,1);
/// inv = 1/det · [[m11,-m01],[-m10,m00]]. Det 0 (pai degenerado) →
/// identidade honesta (drag continua respondendo, sem NaN).
std::array<float, 4> EditorDocument::parentInverse2D(
    eng::ecs::Entity entity) const noexcept
{
    std::array<float, 4> identity{1.f, 0.f, 0.f, 1.f};
    if (!scene_.has_value()) {
        return identity;
    }
    const eng::ecs::Entity parent = scene_->parentOf(entity);
    if (parent == eng::scene::kNoEntity) {
        return identity;
    }
    const eng::math::Mat4 pw = scene_->computeWorldMatrix(parent);
    const float m00 = pw.at(0, 0), m01 = pw.at(1, 0);
    const float m10 = pw.at(0, 1), m11 = pw.at(1, 1);
    const float det = m00 * m11 - m01 * m10;
    if (std::abs(det) < 1e-9f) {
        return identity;
    }
    const float inv = 1.f / det;
    return {m11 * inv, -m01 * inv, -m10 * inv, m00 * inv};
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
    ++selectionRevision_;  // Inspector reflete o componente novo (P2)
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
    ++selectionRevision_;
    return {};
}

// =============================================================================
// Componentes authoráveis (P2 §2/§14)
// =============================================================================

namespace {

/// Hint de dependência por tipo (P2 §14 — o catálogo é ÚNICO; os hints
/// informam o AUTOR sem inventar componentes falsos). Vazios = sem
/// dependência. O formato é texto livre para a UI exibir como está.
[[nodiscard]] std::string dependencyHintFor(std::string_view component)
{
    if (component == "eng::physics::RigidBody") {
        return "Colisão requer Collider (corpo sem collider atravessa)";
    }
    if (component == "eng::animation::Animator") {
        return "Requer um clip em assets/animations (painel Animação)";
    }
    if (component == "eng::editor::AudioSource") {
        return "Requer um WAV em assets/audio (importe no Assets)";
    }
    if (component == "eng::editor::NiScriptComponent") {
        return "Prefira anexar pelo painel Scripts (editor embutido)";
    }
    return {};
}

}  // namespace

std::vector<EditorDocument::ComponentMeta>
EditorDocument::addableComponents(eng::ecs::Entity entity) const
{
    std::vector<ComponentMeta> out;
    if (!scene_.has_value() || !scene_->isNode(entity)) {
        return out;
    }
    for (const auto& name : Inspector::catalog()) {
        // Built-ins obrigatórios não são addáveis (todo nó já os tem).
        if (!Inspector::isRemovable(name)) {
            continue;
        }
        // Já presente → o Inspector lista os campos; nada a adicionar.
        if (!Inspector::componentsOf(*scene_, entity).empty()) {
            bool present = false;
            for (const auto& has : Inspector::componentsOf(*scene_, entity)) {
                if (has == name) {
                    present = true;
                    break;
                }
            }
            if (present) {
                continue;
            }
        }
        out.push_back(ComponentMeta{name, true, dependencyHintFor(name)});
    }
    return out;
}

Result<std::vector<std::string>>
EditorDocument::addComponentWithDependencies(eng::ecs::Entity entity,
                                             std::string_view component)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    auto added = addComponent(entity, component);
    if (added.isError()) {
        return makeUnexpected(added.error());
    }
    std::vector<std::string> created{std::string(component)};

    // Auto-criação SEGURA (P2 §14 — aditiva, nunca destrutiva):
    // Animator num clip com FRAMES precisa de SpriteData para o autor
    // VER o flipbook; SpriteData default é placeholder inofensivo.
    if (component == "eng::animation::Animator") {
        // Animator default tem clip "idle" — sem banco em Edit, nada a
        // validar aqui; o preview/assign cuida do resto.
    }
    return created;
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

/// AudioTick (P2 §12): AudioSources do CLONE no mixer REAL. Primeiro
/// frame de cada voz playOnStart dispara UMA vez (set); vozes loop vivem
/// até o stopAll do stop(). O backend (AAudio/null) pertence ao HOST —
/// aqui apenas o CAMINHO REAL de vozes/mixer.
class AudioTick final : public eng::tick::TickSystem {
public:
    AudioTick(EditorDocument& document) noexcept : document_(document) {}

    [[nodiscard]] const char* name() const override { return "AudioTick"; }
    [[nodiscard]] eng::tick::Phase phase() const override
    {
        return eng::tick::Phase::Update;
    }
    [[nodiscard]] int order() const override { return 40; }

    void tick(eng::scene::Scene& scene, float /*dt*/) override
    {
        // playOnStart dispara UMA VEZ por Play (o set acumula — sem
        // clear: o loop de vida é o do próprio Play/stop).
        scene.world().each<eng::editor::AudioSource>(
            [&](eng::ecs::Entity entity,
                const eng::editor::AudioSource& source) {
                const bool already =
                    std::find(started_.begin(), started_.end(),
                              entity.index) != started_.end();
                if (!source.playOnStart || already ||
                    source.soundAsset.empty()) {
                    return;
                }
                started_.push_back(entity.index);
                auto sound = document_.soundFor(source.soundAsset);
                if (sound.isError()) {
                    ENG_WARN("AudioTick: {}", sound.error().message);
                    return;
                }
                auto played = document_.audioMixer().playSound(
                    *sound.value(), eng::audio::AudioMixer::kMasterBus,
                    source.volume, source.loop);
                if (played.isError()) {
                    ENG_WARN("AudioTick: {}", played.error().message);
                }
            });
        // Manutenção da thread do jogo: recolhe vozes encerradas.
        document_.audioMixer().tick();
    }

private:
    AudioTick(const AudioTick&) = delete;
    AudioTick& operator=(const AudioTick&) = delete;

    EditorDocument& document_;
    std::vector<std::uint32_t> started_;  ///< índices já disparados
};

}  // namespace

Result<void> EditorDocument::play()
{
    if (mode_ == Mode::Play) {
        return makeUnexpected(
            documentError(StatusCode::InvalidState, "já em Play"));
    }
    // P4.1 (T1/D1 — re-armo): entrar em Play mata o drag do gizmo (o
    // clone é outra cena — nenhum estado de edição vaza para o runtime).
    gizmoDragEnd();
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
    // áudio → câmera), agora DECLARADA, testável e extensível.
    // P2 §8: o banco de animação é preenchido com TODOS os clips do
    // projeto (assets reais — o AnimationTick REAL os executa).
    loadAnimationBank();
    scheduler_ = std::make_unique<eng::tick::TickScheduler>();
    (void)scheduler_->addSystem(std::make_unique<eng::tick::PhysicsTick>(
        physicsWorld_, physicsAccumulator_));
    (void)scheduler_->addSystem(
        std::make_unique<eng::tick::AnimationTick>(runtimeAnimations_));
    (void)scheduler_->addSystem(std::make_unique<eng::tick::ParticleTick>());
    (void)scheduler_->addSystem(
        std::make_unique<ScriptTick>(*niRuntime_));
    (void)scheduler_->addSystem(std::make_unique<AudioTick>(*this));
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
        gizmoDragEnd();  // P4.1 (T1/D1): re-armo no retorno à edição
        scheduler_.reset();  // ticks morrem com o clone (ADR-051)
        gameCameraActive_ = false;
        viewport_.setGameCamera(nullptr);  // câmera do editor volta
        niRuntime_->shutdown(); // `up destroy` + descarte (bindings morrem
                                // JUNTOS com o clone — ADR-044)
        audioMixer_.stopAll();  // P2 §12: vozes do Play morrem com o clone
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
    // Em Edit o runtime fica PARADO (gestos do editor não vazam — §6.4);
    // o PREVIEW de animação (P2 §8) avança com o frame do host — o
    // renderFrame chama tick() sempre, e o preview é o único consumidor
    // de tempo em Edit.
    if (mode_ != Mode::Play) {
        previewTick(deltaSeconds);
        return;
    }
    // FASE 9 (§6.1): input com janela de um update por frame.
    runtimeInput_.update();

    // Evolução P0-5 (ADR-051): frame completo pelo TickScheduler —
    // física (timestep fixo), animação, partículas, scripts, áudio e
    // câmera nas fases/ordens declaradas no play(). Determinismo: a ordem é
    // fixa e cada sistema vê o estado deixado pelos anteriores.
    scheduler_->runFrame(*runtimeScene_, deltaSeconds);

    // P2 §8: FRAMES do clip do Animator aplicados ao SpriteData do clone
    // (o AnimationTick avançou o cursor; a aplicação visual é da camada
    // que conhece SpriteData — editor). O MESMO código do preview.
    applyAnimatorFrames(*runtimeScene_);

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
    // P4.1 (T1/D1 — re-armo): um novo toque de seleção mata QUALQUER
    // drag residual — o estado do gizmo nunca atravessa gestos.
    gizmoDragEnd();
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
    // P2 (bug §5 R2): mesmo fixo do gizmo — delta de MUNDO convertido
    // para o espaço LOCAL do pai (filho de pai girado/escalado segue o
    // eixo de TELA, não o eixo local do pai).
    const std::array<float, 4> inv = parentInverse2D(focusEntity);
    local->position.x += inv[0] * worldDx + inv[1] * worldDy;
    local->position.y += inv[2] * worldDx + inv[3] * worldDy;
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

// =============================================================================
// Animação authorável (P2 §8)
// =============================================================================

namespace {

constexpr std::string_view kAnimCategory = "animations";

// --- materiais (P3 §3) -------------------------------------------------------
constexpr std::string_view kMaterialCategory = "materials";
constexpr std::string_view kMaterialExt = ".mat.json";

/// Nome de asset de material válido (mesma política de scripts/animações).
[[nodiscard]] bool isValidMaterialName(std::string_view rawName)
{
    if (rawName.empty() || rawName.size() > 96) {
        return false;
    }
    for (const char c : rawName) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                        c == ' ' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return rawName != "." && rawName != "..";
}

/// Força a extensão .mat.json (a UI envia o nome cru).
[[nodiscard]] std::string withMaterialExtension(std::string_view name)
{
    std::string out{name};
    if (out.size() < kMaterialExt.size() ||
        out.compare(out.size() - kMaterialExt.size(), kMaterialExt.size(),
                    kMaterialExt) != 0) {
        out += kMaterialExt;
    }
    return out;
}

/// Nome de asset de animação válido (mesma política de scripts: sem
/// path/nul; a extensão é forçada por animationCreate).
[[nodiscard]] bool isValidAnimName(std::string_view name) noexcept
{
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return name.find("..") == std::string_view::npos &&
           name.find('/') == std::string_view::npos &&
           name.find('\\') == std::string_view::npos &&
           name.find('\0') == std::string_view::npos;
}

/// Garante a extensão .anim.json (adiciona quando ausente).
[[nodiscard]] std::string withAnimExtension(std::string_view name)
{
    constexpr std::string_view kExt = ".anim.json";
    std::string out{name};
    if (out.size() < kExt.size() ||
        out.compare(out.size() - kExt.size(), kExt.size(), kExt) != 0) {
        out += kExt;
    }
    return out;
}

}  // namespace

Result<std::vector<EditorDocument::AnimSummary>>
EditorDocument::animationList() const
{
    auto listed = assets_->list(kAnimCategory);
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    std::vector<AnimSummary> out;
    out.reserve(listed.value().size());
    for (const auto& entry : listed.value()) {
        AnimSummary summary;
        summary.name = entry.name;
        auto content = animationRead(entry.name);
        if (content.isError()) {
            summary.clip = entry.name;  // ilegível: lista honesta com erro
            continue;
        }
        std::vector<AnimDiag> diags;
        auto decoded = animationDecode(content.value(), &diags);
        if (decoded.isError()) {
            summary.clip = entry.name;
            continue;
        }
        summary.clip = decoded.value().clip.name;
        summary.duration = decoded.value().clip.duration();
        summary.frames = decoded.value().clip.frames.size();
        summary.keys = decoded.value().clip.position.size() +
                       decoded.value().clip.rotation.size() +
                       decoded.value().clip.scale.size();
        summary.loop = decoded.value().meta.loop;
        out.push_back(std::move(summary));
    }
    return out;
}

Result<std::string> EditorDocument::animationRead(
    std::string_view name) const
{
    if (!isValidAnimName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de animação inválido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto bytes = assets_->read(kAnimCategory, name);
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

Result<void> EditorDocument::animationWrite(std::string_view name,
                                            std::string_view json)
{
    if (!isValidAnimName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de animação inválido"));
    }
    if (assets_ == nullptr || !project_.has_value()) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    // Valida ANTES de gravar: lixo não entra no projeto (§15).
    std::vector<AnimDiag> diags;
    auto decoded = animationDecode(json, &diags);
    if (decoded.isError()) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "JSON rejeitado: " + decoded.error().message));
    }
    const eng::fs::Path path = project_->paths().assetsRoot() /
                               eng::fs::Path{std::string(kAnimCategory)} /
                               eng::fs::Path{std::string(name)};
    if (path.isAbsolute()) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "caminho absoluto proibido"));
    }
    auto existed = fs_->exists(path);
    if (existed.isError()) {
        return makeUnexpected(existed.error());
    }
    auto written = fs_->writeAllText(path, std::string(json));
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    if (!existed.value()) {
        auto registered =
            assets_->registerExisting(kAnimCategory, name);
        if (registered.isError()) {
            return makeUnexpected(registered.error());
        }
    }
    return {};
}

Result<void> EditorDocument::animationCreate(std::string_view rawName)
{
    if (!isValidAnimName(rawName)) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "nome de animação inválido (vazio/caracteres proibidos)"));
    }
    const std::string name = withAnimExtension(rawName);
    auto existing = assets_->list(kAnimCategory);
    if (existing.isError()) {
        return makeUnexpected(existing.error());
    }
    for (const auto& entry : existing.value()) {
        if (entry.name == name) {
            return makeUnexpected(
                documentError(StatusCode::AlreadyExists,
                              "animação '" + name + "' já existe"));
        }
    }
    // Template de FLIPBOOK puro: frames começam em t=0 (o authoring
    // adiciona texturas via animationAddFrame; TRS fica desligado —
    // defaults inteligentes do assign). frameHold = 1/fps (o slot).
    const std::string clipBase(rawName);
    const std::string json = "{\n"
                             "  \"name\": \"" + clipBase + "\",\n"
                             "  \"fps\": 8,\n"
                             "  \"loop\": true,\n"
                             "  \"frameHold\": 0.125,\n"
                             "  \"frames\": []\n"
                             "}\n";
    return animationWrite(name, json);
}

Result<void> EditorDocument::animationDelete(std::string_view name)
{
    if (!isValidAnimName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de animação inválido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    return assets_->remove(kAnimCategory, name);
}

// =============================================================================
// Materiais (P3 §3) — assets/materials/<nome>.mat.json
// =============================================================================

Result<std::vector<EditorDocument::MaterialSummary>>
EditorDocument::materialList() const
{
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto listed = assets_->list(kMaterialCategory);
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    std::vector<MaterialSummary> out;
    out.reserve(listed.value().size());
    for (const auto& entry : listed.value()) {
        auto content = materialRead(entry.name);
        if (content.isError()) {
            continue;  // ilegível: NÃO lista (a UI só oferece o válido)
        }
        auto decoded = eng::render::materialDecode(content.value());
        if (decoded.isError()) {
            continue;
        }
        MaterialSummary summary;
        summary.name = entry.name;
        summary.shader = decoded.value().material.shader;
        summary.tintR = decoded.value().material.tintR;
        summary.tintG = decoded.value().material.tintG;
        summary.tintB = decoded.value().material.tintB;
        summary.tintA = decoded.value().material.tintA;
        out.push_back(std::move(summary));
    }
    return out;
}

Result<std::string> EditorDocument::materialRead(
    std::string_view name) const
{
    if (!isValidMaterialName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de material inválido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto bytes = assets_->read(kMaterialCategory, name);
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

Result<void> EditorDocument::materialWrite(std::string_view name,
                                            std::string_view json)
{
    if (!isValidMaterialName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de material inválido"));
    }
    if (assets_ == nullptr || !project_.has_value()) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    // Valida ANTES de gravar: lixo não entra no projeto (§15).
    auto decoded = eng::render::materialDecode(json);
    if (decoded.isError()) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "JSON rejeitado: " + decoded.error().message));
    }
    const eng::fs::Path path =
        project_->paths().assetsRoot() /
        eng::fs::Path{std::string(kMaterialCategory)} /
        eng::fs::Path{std::string(name)};
    if (path.isAbsolute()) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "caminho absoluto proibido"));
    }
    auto existed = fs_->exists(path);
    if (existed.isError()) {
        return makeUnexpected(existed.error());
    }
    auto written = fs_->writeAllText(path, std::string(json));
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    if (!existed.value()) {
        auto registered = assets_->registerExisting(kMaterialCategory, name);
        if (registered.isError()) {
            return makeUnexpected(registered.error());
        }
    }
    // Cache sai (shader/tint podem ter mudado — o próximo resolve relê).
    materialCache_.erase(std::string(name));
    return {};
}

Result<void> EditorDocument::materialCreate(std::string_view rawName)
{
    if (!isValidMaterialName(rawName)) {
        return makeUnexpected(documentError(
            StatusCode::InvalidArgument,
            "nome de material inválido (vazio/caracteres proibidos)"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    const std::string name = withMaterialExtension(rawName);
    auto existing = assets_->list(kMaterialCategory);
    if (existing.isError()) {
        return makeUnexpected(existing.error());
    }
    for (const auto& entry : existing.value()) {
        if (entry.name == name) {
            return makeUnexpected(documentError(
                StatusCode::AlreadyExists,
                "material '" + name + "' já existe"));
        }
    }
    eng::render::MaterialAsset asset;
    asset.name = rawName;
    asset.material.shader = eng::render::kShaderLit;
    auto encoded = eng::render::materialEncode(asset);
    if (encoded.isError()) {
        return makeUnexpected(encoded.error());
    }
    return materialWrite(name, encoded.value());
}

Result<void> EditorDocument::materialDelete(std::string_view name)
{
    if (!isValidMaterialName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de material inválido"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    materialCache_.erase(std::string(name));
    return assets_->remove(kMaterialCategory, name);
}

Result<std::vector<std::string>> EditorDocument::materialNames() const
{
    auto listed = materialList();
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    std::vector<std::string> names;
    names.reserve(listed.value().size());
    for (const auto& summary : listed.value()) {
        names.push_back(summary.name);
    }
    return names;
}

void EditorDocument::resolveMaterials(std::vector<EntityQuad>& quads) const
{
    if (assets_ == nullptr) {
        return;  // sem projeto: default já está nos quads ("lit" neutro)
    }
    for (EntityQuad& quad : quads) {
        if (!quad.isSprite || quad.materialAsset.empty()) {
            continue;  // default: materialShader="lit", tint intactos
        }
        const std::string name = withMaterialExtension(quad.materialAsset);
        auto cached = materialCache_.find(name);
        if (cached == materialCache_.end()) {
            auto content = materialRead(name);
            if (content.isError()) {
                ENG_WARN("material '{}' ilegível — usando default lit "
                         "neutro",
                         name);
                materialCache_[name] = eng::render::SpriteMaterial{};
            } else {
                auto decoded = eng::render::materialDecode(content.value());
                if (decoded.isError()) {
                    ENG_WARN("material '{}' inválido ({}) — usando default "
                             "lit neutro",
                             name, decoded.error().message);
                    materialCache_[name] = eng::render::SpriteMaterial{};
                } else {
                    materialCache_[name] = decoded.value().material;
                }
            }
            cached = materialCache_.find(name);
        }
        const eng::render::SpriteMaterial& material = cached->second;
        quad.materialShader = material.shader;
        quad.tintR *= material.tintR;
        quad.tintG *= material.tintG;
        quad.tintB *= material.tintB;
        quad.tintA *= material.tintA;
    }
}

Result<void> EditorDocument::animationAssign(eng::ecs::Entity entity,
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
    // O nome aceito aqui é o ARQUIVO; o CLIP é o nome interno (fonte:
    // o JSON — "o que o banco vai indexar").
    auto content = animationRead(withAnimExtension(name));
    if (content.isError()) {
        return makeUnexpected(content.error());
    }
    auto decoded = animationDecode(content.value());
    if (decoded.isError()) {
        return makeUnexpected(decoded.error());
    }
    const std::string clipName = decoded.value().clip.name;

    // Componente Animator (adiciona default quando ausente — §14).
    if (!scene_->world().has<eng::animation::Animator>(entity)) {
        auto added = Inspector::addComponent(
            *scene_, entity, "eng::animation::Animator");
        if (added.isError()) {
            return makeUnexpected(added.error());
        }
    }
    // Auto-criação SEGURA (P2 §14): clip com FRAMES precisa de SpriteData
    // para o autor VER o flipbook; default é placeholder inofensivo.
    if (!decoded.value().clip.frames.empty() &&
        !scene_->world().has<eng::editor::SpriteData>(entity)) {
        auto sprite = Inspector::addComponent(
            *scene_, entity, "eng::editor::SpriteData");
        if (sprite.isError()) {
            return makeUnexpected(sprite.error());
        }
    }
    auto* animator = scene_->world().get<eng::animation::Animator>(entity);
    animator->clip = clipName;
    animator->time = 0.f;
    animator->loop = decoded.value().meta.loop;
    animator->playing = false;  // o Play inicia (autoplay é do runtime)
    animator->previousClip.clear();
    animator->blendRemaining = 0.f;
    // Defaults INTELIGENTES: track vazia → apply* DESLIGADO (clip de
    // flipbook puro não zera o Transform do autor; o TRS fica dele).
    animator->applyPosition = !decoded.value().clip.position.empty();
    animator->applyRotation = !decoded.value().clip.rotation.empty();
    animator->applyScale = !decoded.value().clip.scale.empty();
    animator->applySprite = !decoded.value().clip.frames.empty();
    sceneDirty_ = true;
    ++selectionRevision_;
    ENG_INFO("animação anexada: {} (clip '{}', {} frames) → entidade {}",
             name, clipName, decoded.value().clip.frames.size(),
             entity.index);
    return {};
}

Result<float> EditorDocument::animationAddFrame(
    std::string_view name, std::string_view textureAsset)
{
    if (!isValidAnimName(name) || textureAsset.empty()) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome/textura inválidos"));
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    // A textura precisa EXISTIR no projeto (authoring real — sem
    // referências quebradas silenciosas).
    auto textures = assets_->list("textures");
    if (textures.isError()) {
        return makeUnexpected(textures.error());
    }
    bool found = false;
    for (const auto& entry : textures.value()) {
        if (entry.name == textureAsset) {
            found = true;
            break;
        }
    }
    if (!found) {
        return makeUnexpected(documentError(
            StatusCode::NotFound,
            "textura '" + std::string(textureAsset) +
                "' não está no projeto (importe-a primeiro)"));
    }

    const std::string fileName = withAnimExtension(name);
    auto content = animationRead(fileName);
    if (content.isError()) {
        return makeUnexpected(content.error());
    }
    auto decoded = animationDecode(content.value());
    if (decoded.isError()) {
        return makeUnexpected(decoded.error());
    }
    // Cadência de flipbook: primeiro frame em t=0; o seguinte uma cadeia
    // (1/fps) depois do último — o duration estende pelo frameHold.
    const float step =
        decoded.value().meta.fps > 0.f ? 1.f / decoded.value().meta.fps : 0.125f;
    const float when = decoded.value().clip.frames.empty()
                           ? 0.f
                           : decoded.value().clip.frames.back().time + step;

    // Reconstrói o JSON acrescentando o frame (codec canônico — o dump
    // é estável, round-trip testado).
    eng::animation::SpriteFrameKey key;
    key.time = when;
    key.textureAsset = std::string(textureAsset);
    decoded.value().clip.frames.push_back(std::move(key));
    auto encoded = animationEncode(decoded.value());
    if (encoded.isError()) {
        return makeUnexpected(encoded.error());
    }
    auto written = animationWrite(fileName, encoded.value());
    if (written.isError()) {
        return makeUnexpected(written.error());
    }
    return when;
}

Result<void> EditorDocument::animationSetMeta(std::string_view name,
                                               bool loop, float fps)
{
    if (!isValidAnimName(name)) {
        return makeUnexpected(documentError(StatusCode::InvalidArgument,
                                            "nome de animação inválido"));
    }
    if (!std::isfinite(fps) || fps <= 0.f || fps > 120.f) {
        return makeUnexpected(
            documentError(StatusCode::InvalidArgument,
                          "fps deve estar em (0, 120]"));
    }
    const std::string fileName = withAnimExtension(name);
    auto content = animationRead(fileName);
    if (content.isError()) {
        return makeUnexpected(content.error());
    }
    auto decoded = animationDecode(content.value());
    if (decoded.isError()) {
        return makeUnexpected(decoded.error());
    }
    decoded.value().meta.loop = loop;
    decoded.value().meta.fps = fps;
    auto encoded = animationEncode(decoded.value());
    if (encoded.isError()) {
        return makeUnexpected(encoded.error());
    }
    return animationWrite(fileName, encoded.value());
}

Result<void> EditorDocument::previewStart(eng::ecs::Entity entity,
                                          std::string_view clipName)
{
    auto guard = requireEditMode();
    if (guard.isError()) {
        return makeUnexpected(guard.error());
    }
    if (!scene_->isNode(entity)) {
        return makeUnexpected(documentError(StatusCode::NotFound,
                                           "entidade obsoleta"));
    }
    // Resolve o clip por ARQUIVO (nome do asset) ou nome INTERNO do clip.
    auto listed = animationList();
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    std::string fileName;
    for (const auto& summary : listed.value()) {
        if (summary.name == clipName || summary.clip == clipName) {
            fileName = summary.name;
            break;
        }
    }
    if (fileName.empty()) {
        return makeUnexpected(
            documentError(StatusCode::NotFound,
                          "animação '" + std::string(clipName) +
                              "' não encontrada"));
    }
    auto content = animationRead(fileName);
    if (content.isError()) {
        return makeUnexpected(content.error());
    }
    auto decoded = animationDecode(content.value());
    if (decoded.isError()) {
        return makeUnexpected(decoded.error());
    }
    auto original = transform(entity);
    if (original.isError()) {
        return makeUnexpected(original.error());
    }
    preview_ = PreviewState{entity, decoded.value().clip.name, 0.f,
                            decoded.value().meta.loop, original.value()};
    return {};
}

void EditorDocument::previewTick(float deltaSeconds) noexcept
{
    if (!preview_.has_value() || mode_ != Mode::Edit) {
        return;
    }
    PreviewState& state = *preview_;
    const eng::animation::AnimationClip* clip =
        runtimeAnimations_.find(state.clip);
    // O banco em EDIT carrega na hora do preview (fonte: assets).
    if (clip == nullptr) {
        if (!assets_) {
            return;
        }
        auto listed = assets_->list(kAnimCategory);
        if (listed.isError()) {
            return;
        }
        for (const auto& entry : listed.value()) {
            auto content = animationRead(entry.name);
            if (content.isError()) {
                continue;
            }
            auto decoded = animationDecode(content.value());
            if (decoded.isError()) {
                continue;
            }
            runtimeAnimations_.add(std::move(decoded.value().clip));
        }
        clip = runtimeAnimations_.find(state.clip);
    }
    if (clip == nullptr || clip->duration() <= 0.f) {
        return;
    }
    state.time += deltaSeconds;
    if (state.loop) {
        state.time = std::fmod(state.time, clip->duration());
    } else if (state.time >= clip->duration()) {
        state.time = clip->duration();
    }
    // MESMO sampler do runtime (AnimationSystem::sample — fonte única).
    const auto pose =
        eng::animation::AnimationSystem::sample(*clip, state.time);
    eng::editor::TransformDesc desc = state.original;
    desc.position = pose.position;
    desc.rotationDegrees = degreesFromQuat(pose.rotation);
    desc.scale = pose.scale;
    (void)setTransform(state.entity, desc);  // dirty (honesto: preview edita)
    // Frames no sprite da ENTIDADE em edição (mesma aplicação do Play).
    applyAnimatorFrames(*scene_);
}

void EditorDocument::previewStop() noexcept
{
    if (!preview_.has_value()) {
        return;
    }
    // Restaura o TRANSFORM original (preview não deixa sujeira).
    if (scene_.has_value() && scene_->isNode(preview_->entity)) {
        (void)setTransform(preview_->entity, preview_->original);
    }
    preview_.reset();
}

void EditorDocument::loadAnimationBank()
{
    // MERGE, sem clear: clips adicionados programaticamente (API C++/testes)
    // com nomes ÚNICOS sobrevivem; assets são a FONTE DE AUTORIA e
    // sobrescrevem clipes de mesmo nome (insert_or_assign do banco).
    if (assets_ == nullptr) {
        return;
    }
    auto listed = assets_->list(kAnimCategory);
    if (listed.isError()) {
        return;
    }
    for (const auto& entry : listed.value()) {
        auto content = animationRead(entry.name);
        if (content.isError()) {
            ENG_WARN("animation: {} ilegível ({})", entry.name,
                     content.error().message);
            continue;
        }
        auto decoded = animationDecode(content.value());
        if (decoded.isError()) {
            ENG_WARN("animation: {} inválida ({})", entry.name,
                     decoded.error().message);
            continue;
        }
        runtimeAnimations_.add(std::move(decoded.value().clip));
    }
}

void EditorDocument::applyAnimatorFrames(eng::scene::Scene& scene)
{
    scene.world().each<eng::animation::Animator>(
        [&](eng::ecs::Entity entity,
            const eng::animation::Animator& animator) {
            if (!animator.applySprite) {
                return;
            }
            const eng::animation::AnimationClip* clip =
                runtimeAnimations_.find(animator.clip);
            if (clip == nullptr) {
                return;
            }
            const auto* frame =
                eng::animation::sampleFrame(*clip, animator.time);
            if (frame == nullptr) {
                return;
            }
            auto* sprite = scene.world().get<eng::editor::SpriteData>(entity);
            if (sprite == nullptr) {
                return;
            }
            sprite->textureAsset = frame->textureAsset;
            sprite->u0 = frame->u0;
            sprite->v0 = frame->v0;
            sprite->u1 = frame->u1;
            sprite->v1 = frame->v1;
        });
}

// =============================================================================
// Áudio authorável (P2 §12)
// =============================================================================

Result<std::shared_ptr<const eng::audio::Sound>>
EditorDocument::soundFor(std::string_view assetName)
{
    const std::string key{assetName};
    const auto cached = soundCache_.find(key);
    if (cached != soundCache_.end()) {
        return cached->second;
    }
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto bytes = assets_->read("audio", key);
    if (bytes.isError()) {
        return makeUnexpected(bytes.error());
    }
    auto wav = eng::audio::Wav::parse(
        std::span{bytes.value().data(), bytes.value().size()});
    if (wav.isError()) {
        return makeUnexpected(wav.error());
    }
    auto sound = eng::audio::Sound::fromWav(wav.value());
    if (sound.isError()) {
        return makeUnexpected(sound.error());
    }
    auto shared = std::make_shared<const eng::audio::Sound>(
        std::move(sound.value()));
    soundCache_[key] = shared;
    return shared;
}

Result<void> EditorDocument::audioPreview(std::string_view assetName)
{
    if (assets_ == nullptr) {
        return makeUnexpected(documentError(StatusCode::InvalidState,
                                            "nenhum projeto aberto"));
    }
    auto sound = soundFor(assetName);
    if (sound.isError()) {
        return makeUnexpected(sound.error());
    }
    auto played = audioMixer_.playSound(*sound.value(),
                                        eng::audio::AudioMixer::kMasterBus,
                                        1.f, false);
    if (played.isError()) {
        return makeUnexpected(played.error());
    }
    audioMixer_.tick();
    return {};
}

}  // namespace eng::editor
