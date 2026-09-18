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
#include "eng/animation/Animation.hpp"
#include "eng/ecs/Ecs.hpp"
#include "eng/editor/AssetBrowser.hpp"
#include "eng/editor/Inspector.hpp"
#include "eng/editor/Viewport.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/fs/Path.hpp"
#include "eng/input/Input.hpp"
#include "eng/math/Vec3.hpp"
#include "eng/niscript/NiVm.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/project/ProjectFile.hpp"
#include "eng/scene/Scene.hpp"
#include "eng/tick/Camera.hpp"
#include "eng/tick/Tick.hpp"

namespace eng::editor {

class NiRuntime; // NiRuntime.hpp (frente — impl em NiRuntime.cpp)

/// Registra os componentes de gameplay (physics/animation/particles) no
/// catálogo do serializer — efeito colateral da inicialização estática de
/// ComponentRegistration.cpp; a chamada garante que o TU entre no link
/// (libs estáticas descartam objetos não referenciados).
void ensureEditorComponentsRegistered() noexcept;

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
    /// Avanço do runtime por frame. Play: input (§6.1) + TICK SCHEDULER
    /// (evolução P0-5, ADR-051 — física com timestep fixo, animação,
    /// partículas, scripts e câmera agendados por (fase, ordem)) sobre o
    /// CLONE. Edit: parado (gestos não vazam — §6.4). Depois do frame a
    /// câmera de jogo ativa (se houver) toma o viewport (ADR-051).
    void tick(float deltaSeconds) noexcept;

    /// Agendador de ticks do runtime (P0-5). Vazio em Edit; construído no
    /// play(). Diagnóstico/testes.
    [[nodiscard]] const eng::tick::TickScheduler* runtimeScheduler()
        const noexcept
    {
        return scheduler_.get();
    }

    /// Câmera de jogo ativa do último frame (P0-5) — o viewport a usa em
    /// Play quando a cena tem câmera ativa (ADR-051).
    [[nodiscard]] bool hasGameCamera() const noexcept
    {
        return gameCameraActive_;
    }

    /// Física do runtime (contatos do último passo — gameplay/debug).
    [[nodiscard]] const eng::physics::PhysicsWorld& runtimePhysics() const
        noexcept
    {
        return physicsWorld_;
    }
    /// Runtime de scripts NI-Script do clone (FASE 11 — vazio em Edit).
    [[nodiscard]] const NiRuntime& runtimeScripts() const noexcept
    {
        return *niRuntime_;
    }
    /// Banco de animações do runtime (clips por nome — API C++ §9).
    eng::animation::AnimationBank& runtimeAnimations() noexcept
    {
        return runtimeAnimations_;
    }

    /// INPUT DO JOGO (FASE 9, §6.4 — separado dos gestos do editor): os
    /// toques do viewport em Play alimentam ESTE sistema; bindings são
    /// configuráveis por asset (input.json — ActionBindings::fromJson).
    [[nodiscard]] eng::input::InputSystem& runtimeInput() noexcept
    {
        return runtimeInput_;
    }
    void setRuntimeBindings(eng::input::ActionBindings bindings)
    {
        runtimeInput_.setBindings(std::move(bindings));
    }
    /// Toque do jogo (em Play). Fase canônica: 0=Down,1=Move,2=Up,3=Cancel.
    void gameTouch(int canonicalPhase, std::uint32_t pointerId, float x,
                   float y, float pressure);
    void setGameViewportSize(float width, float height) noexcept;

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

    // --- scripts NI-Script (evolução P0-7, ADR-053) ------------------------------
    //
    // O asset .nis é a FONTE DE EDIÇÃO (assets/scripts/*.nis); a cena
    // carrega uma CÓPIA estável em NiScriptComponent.source (ADR-043 — o
    // runtime/serialização nunca dependem de arquivos externos à cena).
    // scriptAssign copia do asset para o componente; o workflow completo
    // é: criar → editar → compilar (validação) → anexar à entidade.

    /// Diagnóstico de compilação .nis (1-based).
    struct ScriptDiag {
        std::uint32_t line = 0;
        std::uint32_t col = 0;
        std::string message;
    };

    /// Resultado de uma checagem de compilação.
    struct ScriptCheck {
        bool ok = false;                ///< compilou até bytecode?
        std::vector<ScriptDiag> diags; ///< TODOS os erros coletados
    };

    /// Lista os scripts do projeto (assets/scripts), por nome de arquivo.
    /// Pré-requisito: projeto aberto.
    [[nodiscard]] eng::core::Result<std::vector<std::string>> scriptList()
        const;

    /// Lê o conteúdo de um script .nis. Erros precisos.
    [[nodiscard]] eng::core::Result<std::string> scriptRead(
        std::string_view name) const;

    /// Escreve o conteúdo do script (substitui bytes; cataloga no
    /// registry quando é arquivo novo). Valida nome/anti-traversal.
    [[nodiscard]] eng::core::Result<void> scriptWrite(
        std::string_view name, std::string_view content);

    /// Cria um script NOVO com o template canônico (força .nis; recusa
    /// nome vazio/duplicado).
    [[nodiscard]] eng::core::Result<void> scriptCreate(std::string_view name);

    /// Apaga um script (arquivo + registry).
    [[nodiscard]] eng::core::Result<void> scriptDelete(std::string_view name);

    /// Valida a fonte .nis SEM executar: compila com a MESMA tabela de
    /// nativos do runtime de Play (&BL + host padrão — o que o jogo vê).
    /// O Result falha apenas em erros INTERNOS; o veredito está em `ok`.
    [[nodiscard]] eng::core::Result<ScriptCheck> scriptCompile(
        std::string_view source) const;

    /// Anexa o script à entidade: NiScriptComponent.source = conteúdo do
    /// asset (adiciona o componente quando ausente). Só em Edit.
    [[nodiscard]] eng::core::Result<void> scriptAssign(
        eng::ecs::Entity entity, std::string_view name);

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

    /// Espelha a câmera de jogo ativa no viewport (P0-5, ADR-051): copia
    /// posX/posY/zoom para `gameCamera_` e entrega ao viewport, ou devolve
    /// a câmera do editor quando a cena não tem câmera ativa.
    void syncGameCamera(const eng::tick::ActiveCamera& active) noexcept;

    eng::input::InputSystem runtimeInput_{}; ///< input do JOGO (§6.4)
    eng::physics::PhysicsWorld physicsWorld_{};      ///< §7.1–§7.6
    eng::physics::TimestepAccumulator physicsAccumulator_{1.f / 60.f};
    eng::animation::AnimationBank runtimeAnimations_{}; ///< §7.7–§7.11
    std::unique_ptr<class NiRuntime> niRuntime_;     ///< §FASE 11 (clone)
    std::unique_ptr<eng::tick::TickScheduler> scheduler_{}; ///< P0-5 (Play)
    Viewport::Camera2D gameCamera_{};      ///< cache da câmera ativa (P0-5)
    bool gameCameraActive_ = false;        ///< último refresh achou câmera?
    Viewport viewport_{};
    std::unique_ptr<AssetBrowser> assets_{};
};

} // namespace eng::editor
