#pragma once

/// eng::editor::EditorDocument — estado e comandos do editor
/// (FASE 8, missão §8.7–§8.9).
///
/// Modelo de estado (missão §8.9) — SEM globals:
///   ProjectState  = ProjectFile + dirty
///   SceneState    = Scene em EDIÇÃO + dirty
///   SelectionState= entidade selecionada (ou nada)
///   ViewportState = Viewport (câmera 2D)
///   InspectorState= leitura pura via Inspector (sem estado próprio)
///   RUNTIME       = Scene clone existindo APENAS em Play (§8.7)
///
/// SEPARAÇÃO EDITOR × RUNTIME (missão §8.7, ADR-044): o estado do editor é
/// a cena editada; o runtime é um CLONE por serialização (round-trip já
/// testado na FASE 3) que nasce no play() e é DESCARTADO no stop(). Em
/// Play, comandos de edição são REJEITADOS (erro explícito) — o viewport
/// renderiza o clone; mutação de entidades em Play (arraste — ferramenta
/// de debug) altera o CLONE e nunca vaza para a edição.
///
/// Todos os comandos devolvem Result (missão: erros com contexto). I/O
/// sempre via eng::fs (paths RELATIVOS ao root — §8.1; absolutos são
/// rejeitados pelo próprio ProjectFile/Path).

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/ecs/Ecs.hpp"
#include "eng/editor/AssetBrowser.hpp"
#include "eng/editor/Inspector.hpp"
#include "eng/editor/Viewport.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/fs/Path.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/project/ProjectFile.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::editor {

/// TRS do editor com rotação em GRAUS Euler XYZ (convenção do usuário; a
/// cena guarda Quat — conversão interna testada).
struct TransformDesc {
    eng::math::Vec3 position{};
    eng::math::Vec3 rotationDegrees{};
    eng::math::Vec3 scale{1.f, 1.f, 1.f};
};

class EditorDocument final {
public:
    /// Documento sem projeto (cena vazia pronta para edição). O fs é
    /// EMPRESTADO (o host é o dono — EditorHost no Android, teste no
    /// Linux): NativeFileSystem no app, MemoryFileSystem nos testes
    /// (missão §8.1: reuso de eng::project/fs, nada de paths absolutos
    /// DENTRO do projeto). O documento não sobrevive ao fs.
    [[nodiscard]] static eng::core::Result<std::unique_ptr<EditorDocument>>
    create(eng::fs::FileSystem& fs, const eng::fs::Path& workspaceRoot);

    ~EditorDocument();
    EditorDocument(const EditorDocument&) = delete;
    EditorDocument& operator=(const EditorDocument&) = delete;

    // --- projeto (§8.1) ------------------------------------------------------

    /// Cria <workspaceRoot>/<name> com project.goni.json, assets/<cats>/ e
    /// scenes/ e o ABRE. Erro se já existe.
    [[nodiscard]] eng::core::Result<void> newProject(std::string_view name);

    /// Abre projeto existente (project.goni.json parseado — ADR-032).
    [[nodiscard]] eng::core::Result<void> openProject(
        const eng::fs::Path& projectRoot);

    /// Persiste config + registry de assets.
    [[nodiscard]] eng::core::Result<void> saveProject();

    [[nodiscard]] bool hasProject() const noexcept { return project_.has_value(); }
    [[nodiscard]] std::string projectName() const;
    /// Project Settings (§8.1): renomeia (config + disco + dirty).
    [[nodiscard]] eng::core::Result<void> setProjectName(std::string_view name);
    [[nodiscard]] eng::fs::Path projectRoot() const;

    // --- cena (§8.2) ----------------------------------------------------------

    /// Cena nova (a atual é descartada — sem perda silenciosa: chamador
    /// decide salvar antes; sceneDirty reflete).
    eng::core::Result<void> newScene();
    /// Salva em <scenesRoot>/<path>. Sujo→limpo.
    [[nodiscard]] eng::core::Result<void> saveScene(std::string_view scenePath);
    /// Carrega (LIMPA a cena atual e preenche — SceneSerializer::load em
    /// cena vazia; ADR-033).
    [[nodiscard]] eng::core::Result<void> loadScene(std::string_view scenePath);

    [[nodiscard]] bool sceneDirty() const noexcept { return sceneDirty_; }
    [[nodiscard]] bool projectDirty() const noexcept { return projectDirty_; }

    // --- entidades (§8.2/§8.3) — REJEITADOS em Play ---------------------------

    [[nodiscard]] eng::core::Result<eng::ecs::Entity> createEntity(
        std::string_view name, eng::ecs::Entity parent);
    [[nodiscard]] eng::core::Result<void> deleteEntity(eng::ecs::Entity entity);
    [[nodiscard]] eng::core::Result<void> renameEntity(eng::ecs::Entity entity,
                                                       std::string_view name);
    /// Duplica o nó E SUBÁRVORE (componentes via encode/decode do catálogo).
    [[nodiscard]] eng::core::Result<eng::ecs::Entity> duplicateEntity(
        eng::ecs::Entity entity);
    /// parent == kNoEntity → vira raiz. Ciclo → erro propagado.
    [[nodiscard]] eng::core::Result<void> reparentEntity(
        eng::ecs::Entity entity, eng::ecs::Entity parent);

    [[nodiscard]] eng::core::Result<TransformDesc> transform(
        eng::ecs::Entity entity) const;
    [[nodiscard]] eng::core::Result<void> setTransform(eng::ecs::Entity entity,
                                                       const TransformDesc& desc);

    // --- seleção --------------------------------------------------------------

    [[nodiscard]] eng::core::Result<void> select(eng::ecs::Entity entity);
    void deselect() noexcept;
    [[nodiscard]] bool isSelected(eng::ecs::Entity entity) const noexcept;
    [[nodiscard]] std::optional<eng::ecs::Entity> selection() const noexcept
    {
        return selection_;
    }

    // --- componentes (§8.4) — Inspector + guarda de modo ----------------------

    [[nodiscard]] std::vector<Inspector::Field> inspectorFields(
        eng::ecs::Entity entity, std::string_view component) const;
    [[nodiscard]] eng::core::Result<void> setInspectorField(
        eng::ecs::Entity entity, std::string_view component,
        std::string_view fieldPath, std::string_view value);
    [[nodiscard]] eng::core::Result<void> addComponent(
        eng::ecs::Entity entity, std::string_view component);
    [[nodiscard]] eng::core::Result<void> removeComponent(
        eng::ecs::Entity entity, std::string_view component);

    // --- play/stop (§8.7, ADR-044) ---------------------------------------------

    [[nodiscard]] eng::core::Result<void> play();
    void stop() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept { return mode_ == Mode::Play; }
    /// Avanço do runtime por frame (Choreographer). FASE 8: mantém o
    /// contrato do loop; o conteúdo dos sistemas cresce nas FASES 9/10.
    void tick(float deltaSeconds) noexcept;

    // --- viewport (§8.6) --------------------------------------------------------

    [[nodiscard]] Viewport& viewport() noexcept { return viewport_; }
    [[nodiscard]] const Viewport& viewport() const noexcept { return viewport_; }

    /// Tap → seleção (hit-test top-most). Sem hit → deselect.
    [[nodiscard]] std::optional<eng::ecs::Entity> viewportTap(
        float screenX, float screenY);
    void viewportPan(float screenDx, float screenDy) noexcept;
    void viewportZoom(float factor, float focusX, float focusY) noexcept;
    /// Arraste: move a entidade (delta de TELA → mundo). Edit: move na
    /// EDIÇÃO (dirty); Play: move no CLONE (debug — não vaza).
    [[nodiscard]] eng::core::Result<void> moveEntityScreen(
        eng::ecs::Entity entity, float screenDx, float screenDy);

    /// Snapshot da hierarquia (§8.3) — caminhada depth-first estável.
    struct HierarchyNode {
        eng::ecs::Entity entity{};
        std::string name;
        int depth = 0;
    };
    [[nodiscard]] std::vector<HierarchyNode> hierarchySnapshot() const;

    /// Cena em FOCO (render/consulta): edição em Edit, CLONE em Play.
    [[nodiscard]] eng::scene::Scene* sceneInFocus() noexcept;
    [[nodiscard]] const eng::scene::Scene* sceneInFocus() const noexcept;

    // --- assets (§8.5) -----------------------------------------------------------

    [[nodiscard]] AssetBrowser* assets() noexcept { return assets_.get(); }
    [[nodiscard]] const AssetBrowser* assets() const noexcept
    {
        return assets_.get();
    }

    /// Nome de exibição (Name component; "Entity" quando ausente).
    [[nodiscard]] static std::string nameOf(const eng::scene::Scene& scene,
                                            eng::ecs::Entity entity);

    /// Empacota/desempacota Entity para tráfico JNI (index+1|generation;
    /// 0 = "nenhuma" — valor, não ponteiro; auditoria §4).
    [[nodiscard]] static std::uint64_t packEntity(eng::ecs::Entity entity) noexcept;
    [[nodiscard]] static eng::ecs::Entity unpackEntity(
        std::uint64_t packed) noexcept;

private:
    EditorDocument() = default;

    /// Garantia de modo: TODA escrita de edição passa por aqui.
    [[nodiscard]] eng::core::Result<void> requireEditMode() const;

    eng::fs::FileSystem* fs_ = nullptr;  ///< emprestado
    eng::fs::Path workspaceRoot_{};
    std::optional<eng::project::ProjectFile> project_{};
    bool projectDirty_{false};

    std::optional<eng::scene::Scene> scene_{}; ///< cena em EDIÇÃO (§8.7)
    bool sceneDirty_{false};
    std::optional<eng::scene::Scene> runtimeScene_{}; ///< só em Play (clone)

    enum class Mode : std::uint8_t { Edit, Play };
    Mode mode_{Mode::Edit};

    std::optional<eng::ecs::Entity> selection_{};

    Viewport viewport_{};
    std::unique_ptr<AssetBrowser> assets_{};
};

} // namespace eng::editor
