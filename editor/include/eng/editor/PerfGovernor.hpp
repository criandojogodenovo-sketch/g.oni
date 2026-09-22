#pragma once

/// eng::editor::PerfGovernor — P4.7.0 Bloco 6: o CÉREBRO de performance.
///
/// Observa o tempo de frame (EMA) + o nível térmico (ADPF — Android
/// Dynamic Performance Framework) e dirige uma ESCADA de presets
/// (High → Med → Low) com HISTERESE: entra no preset seguinte só após N
/// frames ruins CONSECUTIVOS, e só VOLTA após M frames bons (M >> N) —
/// desce e sobe SEM oscilar (critério do round 7). O nível térmico
/// severo ESCALA na hora (heat é urgente; recuperação térmica é lenta).
///
/// Puro (sem JNI, sem GL, sem cena): testável no Linux — o CI prova o
/// desce-sobe sem oscilação; o device prova com térmico real (round 7).
///
/// Levers do preset (aplicados pelos consumidores — B6 aplica os que
/// existem hoje; post/bloom/luz-textura são da P4.7.1):
/// - renderScale: escala de resolução da vista (1.0 / 0.85 / 0.7)
/// - lightTextureRes: resolução da textura de luz (512 / 256 / 128)
/// - post: post-stack ligado (true / false / false)
/// - bloomHalfRes: bloom em meia resolução (true / true / true)
/// - maxLights: luzes por camada (8 / 4 / 2)

#include <cstdint>

namespace eng::editor {

/// Nível térmico do ADPF (android/thermal.h — AThermalStatus). O valor
/// no Linux (sem ADPF) é Unknown — o governor roda só com frame time.
enum class ThermalLevel : std::int8_t {
    Unknown = -1,
    None = 0,
    Light = 1,
    Moderate = 2,
    Severe = 3,
    Critical = 4,
    Emergency = 5,
    Shutdown = 6,
};

/// Preset da escada (índice 0 = topo de qualidade, 2 = economia).
struct PerfPreset {
    float renderScale{1.f};        ///< escala de resolução da vista
    std::uint32_t lightTextureRes{512};
    bool post{true};               ///< post-stack ligado
    bool bloomHalfRes{true};       ///< bloom em meia resolução
    std::uint32_t maxLights{8};    ///< luzes por camada
};

class PerfGovernor final {
public:
    /// Janela do EMA de frame time (frames — ~0.5 s a 60 fps).
    static constexpr std::uint32_t kEmaWindow = 30;
    /// HISTERESE: frames ruins consecutivos para DESCER um degrau;
    /// frames bons consecutivos para SUBIR (kGoodFrames > kBadFrames —
    /// voltar é mais difícil que cair: anti-oscilação).
    static constexpr std::uint32_t kBadFrames = 30;
    static constexpr std::uint32_t kGoodFrames = 120;
    /// Alvos de frame time: orçamento de 60 fps e de 30 fps (ms).
    static constexpr float kTargetMs = 16.7f;
    static constexpr float kBadMs = 26.0f;   // pior que ~38 fps
    static constexpr float kGoodMs = 14.5f;  // melhor que ~69 fps (folga)

    /// Registra um frame. `frameMs` não-finito/<=0 é ignorado (pausa,
    /// surface morta — nunca conta como bom nem ruim).
    void onFrame(float frameMs, ThermalLevel thermal);

    [[nodiscard]] std::uint32_t presetIndex() const noexcept
    {
        return preset_;
    }
    [[nodiscard]] PerfPreset preset() const noexcept;
    [[nodiscard]] float emaMs() const noexcept { return emaMs_; }
    [[nodiscard]] std::uint32_t badStreak() const noexcept
    {
        return badStreak_;
    }
    [[nodiscard]] std::uint32_t goodStreak() const noexcept
    {
        return goodStreak_;
    }
    /// Térmico observado no último frame (Unknown até o host informar).
    [[nodiscard]] ThermalLevel thermal() const noexcept { return thermal_; }

    void reset() noexcept;

private:
    /// Desce um degrau (qualidade menor). Térmico >= Severe escala DIRETO
    /// ao preset mais econômico (heat urgente).
    void stepDown() noexcept;
    /// Sobe um degrau (nunca acima do índice 0).
    void stepUp() noexcept;

    float emaMs_{0.f};
    bool emaSeeded_{false};
    ThermalLevel thermal_{ThermalLevel::Unknown};
    std::uint32_t preset_{0};   // High → Med → Low (índice da escada)
    std::uint32_t badStreak_{0};
    std::uint32_t goodStreak_{0};
};

} // namespace eng::editor
