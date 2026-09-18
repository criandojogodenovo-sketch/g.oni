#pragma once

/// eng::editor::NiRuntime — runtime de scripts do editor (FASE 11).
///
/// Posse e ciclo (ADR-044): vive DENTRO do EditorDocument e aponta para o
/// CLONE de Play. play(): compila os NiScriptComponent do clone, cria
/// instâncias (ordem determinística de varredura do World), roda @init e
/// `up start`. tick(): `up update` por instância (orçamento por chamada
/// — design §6.2). stop(): `up destroy` best-effort e DESCARTE (o
/// bindings aponta para o world do clone — nunca sobrevive ao stop).
///
/// Host: delta do frame corrente + ações do runtimeInput_ + spawn/find/
/// despawn no clone. Bindings: açúcar de nó (position/rotation/scale/
/// name) + componentes do catálogo — REFLETIDOS por offset (mesmo
/// mecanismo do Inspector, auditoria D2).

#include <memory>
#include <vector>

#include "eng/niscript/NiScript.hpp"
#include "eng/niscript/NiVm.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::editor {

class NiRuntime final {
public:
    NiRuntime();            // HostImpl é interno do .cpp (Pimpl leve)
    ~NiRuntime();
    NiRuntime(const NiRuntime&) = delete;
    NiRuntime& operator=(const NiRuntime&) = delete;

    /// Prepara o runtime sobre o CLONE (chamado no play(), depois do
    /// clone existir). Compila todos os scripts (erros: log + script
    /// desabilitado), cria instâncias e roda @init na ordem de criação.
    void start(eng::scene::Scene& runtimeScene);

    /// `up start` em todas as instâncias (após todos @init — ordem
    /// determinística documentada em docs/ni-script/07).
    void fireStart();

    /// `up update` em todas as instâncias.
    /// `deltaSeconds` alimenta delta(); o orçamento é por EVENTO
    /// (kDefaultBudget — design §6.2).
    void tick(float deltaSeconds);

    /// `up destroy` best-effort + descarte de TODO o estado.
    void shutdown() noexcept;

    [[nodiscard]] bool empty() const noexcept { return set_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return set_.size(); }

    /// Faults registrados do último tick (diagnóstico — null quando ok).
    [[nodiscard]] std::vector<const eng::ni::NiScriptState*> instances()
        const;

    /// Liga a fonte de AÇÕES (o EditorDocument conecta o runtimeInput_
    /// do clone). `phase`: 0=down, 1=pressed, 2=released.
    void setActionQuery(bool (*query)(std::string_view, int, void*),
                        void* user) noexcept;

private:
    struct HostImpl;

    [[nodiscard]] bool queryAction_(std::string_view action, int phase) const;
    [[nodiscard]] eng::ni::NiExecContext::Params params() const;

    eng::ni::NiNativeTable natives_;
    eng::ni::NiBindingTable bindings_;
    eng::ni::NiInstanceSet set_;
    eng::ni::NiVm vm_;
    std::unique_ptr<HostImpl> host_;
    eng::scene::Scene* scene_ = nullptr;
    float delta_ = 0.f;
    bool (*actionQuery_)(std::string_view, int, void*) = nullptr;
    void* actionQueryUser_ = nullptr;
};

} // namespace eng::editor
