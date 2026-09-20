#pragma once

/// eng::audio — vozes, buses, mixer software e backend abstraído
/// (FASE 9, missão §6.8–§6.10).
///
/// Conceitos (§6.8): Sound (buffer decodificado — SFX), Music (leitura
/// progressiva do arquivo — streaming real), Voice (instância tocando),
/// AudioBus (ganho de grupo), fonte = Sound|Music.
///
/// Lifetime (§6.10): handles de voz são VALORES geracionais (obsoletos =
/// no-op seguro); vozes terminadas são coletadas no tick() do jogo; o
/// mixer NÃO retém o Sound (shared_ptr do chamador); Music fecha o cursor
/// no fim/stop.
///
/// Threads (§D7): mix() roda na THREAD DE ÁUDIO (callback do backend);
/// todo o resto roda na thread do jogo. Um único mutex protege a lista de
/// vozes — janelas curtas (mix por frame, sem alocação no caminho quente
/// após a voz existir). Padrão pull (ADR-047).

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "eng/audio/Wav.hpp"
#include "eng/core/Result.hpp"
#include "eng/fs/FileSystem.hpp"
#include "eng/fs/Path.hpp"
#include "eng/serial/JsonValue.hpp"

namespace eng::audio {

// =============================================================================
// Bus (§6.8)
// =============================================================================

struct AudioBus {
    std::string name{"master"};
    float gain{1.f};
};

// =============================================================================
// Sound — buffer decodificado
// =============================================================================

class Sound final {
public:
    Sound() = default;

    /// Decodifica WAV completo (SFX — curto).
    [[nodiscard]] static eng::core::Result<Sound> fromWav(
        const WavData& data);

    [[nodiscard]] std::shared_ptr<const WavData> data() const noexcept
    {
        return data_;
    }
    [[nodiscard]] std::uint32_t frames() const noexcept;

private:
    std::shared_ptr<const WavData> data_;
};

// =============================================================================
// Mixer + vozes
// =============================================================================

/// Handle de voz: índice + geração (obsoleto = no-op — §6.10).
struct VoiceHandle {
    std::uint64_t value{0};
    [[nodiscard]] bool isValid() const noexcept { return value != 0; }
};

struct MixerStats {
    std::uint64_t voicesPlayed{0};
    std::uint64_t voicesFinished{0};
    std::uint64_t framesMixed{0};
    std::uint64_t underruns{0}; ///< pull sem tick (música sem dados)
};

class AudioMixer final {
public:
    explicit AudioMixer(std::uint32_t sampleRate = 48000,
                        std::uint16_t channels = 2);

    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // --- buses (§6.8) ----------------------------------------------------------

    std::uint32_t createBus(std::string_view name, float gain);
    void setBusGain(std::uint32_t busId, float gain);

    // --- vozes (thread do JOGO) ---------------------------------------------------

    /// Toca um Sound (loop opcional). O mixer retém o shared_ptr — o
    /// chamador pode soltar o dele.
    [[nodiscard]] eng::core::Result<VoiceHandle> playSound(
        const Sound& sound, std::uint32_t busId = 0, float volume = 1.f,
        bool loop = false);

    /// Toca música com LEITURA PROGRESSIVA (streaming §6.10): o arquivo é
    /// lido uma vez (bytes), decodificado sob demanda por cursor.
    [[nodiscard]] eng::core::Result<VoiceHandle> playMusic(
        const std::shared_ptr<eng::fs::FileSystem>& fs,
        const eng::fs::Path& path, std::uint32_t busId = 0,
        float volume = 1.f, bool loop = false);

    void stop(VoiceHandle handle);
    void pause(VoiceHandle handle);
    void resume(VoiceHandle handle);
    void setVolume(VoiceHandle handle, float volume);
    [[nodiscard]] bool isPlaying(VoiceHandle handle) const;
    [[nodiscard]] bool isPaused(VoiceHandle handle) const;

    /// Lifecycle do app (§6.10): pausa/retoma TUDO (onPause/onResume).
    void pauseAll() noexcept;
    void resumeAll() noexcept;
    void stopAll() noexcept;

    /// Manutenção da thread do JOGO: recolhe vozes encerradas, avança o
    /// decode sob demanda do streaming.
    void tick();

    // --- mix (THREAD DE ÁUDIO — AAudio) ----------------------------------------------

    /// Soma `frames` frames no out (f32 interleaved; NÃO zera: adiciona).
    void mix(float* out, std::uint32_t frames);

    [[nodiscard]] std::uint32_t sampleRate() const noexcept
    {
        return sampleRate_;
    }
    [[nodiscard]] std::uint16_t channels() const noexcept
    {
        return channels_;
    }
    [[nodiscard]] std::size_t liveVoices() const noexcept;
    [[nodiscard]] const MixerStats& stats() const noexcept
    {
        return stats_;
    }

    static constexpr std::uint32_t kMasterBus = 0;

private:
    struct Voice {
        std::uint64_t id{0};       // handle value
        std::uint32_t generation{1};
        bool active{false};
        bool paused{false};
        bool loop{false};
        float volume{1.f};
        std::uint32_t busId{0};

        // Fonte A: Sound (buffer pronto).
        std::shared_ptr<const WavData> buffer;
        std::size_t cursorFrames{0};

        // Fonte B: Music (bytes WAV + cursor de decode sob demanda).
        std::vector<std::byte> streamBytes;
        std::size_t streamDataOffset{0};  ///< início do chunk data
        std::size_t streamDataSize{0};
        std::uint32_t streamRate{0};
        std::uint16_t streamChannels{0};
        std::uint16_t streamBits{0};
        std::size_t streamCursor{0};      ///< bytes consumidos do data
        std::vector<float> decodeWindow;  ///< janela atual decodificada
        std::size_t windowCursor{0};
    };

    void decodeNextWindow(Voice& voice);
    [[nodiscard]] float busGain(std::uint32_t busId) const noexcept;
    /// Busca por handle (ativos apenas); obsoleto → nullptr.
    [[nodiscard]] static Voice* findVoice(
        std::vector<std::unique_ptr<Voice>>& voices, VoiceHandle handle);
    [[nodiscard]] static const Voice* findVoice(
        const std::vector<std::unique_ptr<Voice>>& voices,
        VoiceHandle handle);

    std::uint32_t sampleRate_;
    std::uint16_t channels_;
    mutable std::mutex mutex_;
    std::vector<AudioBus> buses_;
    std::vector<std::unique_ptr<Voice>> voices_;
    std::uint64_t nextVoiceId_{1};
    MixerStats stats_{};
};

// =============================================================================
// Backend (§6.9) — pull
// =============================================================================

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    /// Conecta o mixer ao dispositivo e inicia o pull.
    [[nodiscard]] virtual eng::core::Result<void> start(AudioMixer& mixer) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    /// P3.3 — descrição estável do device/stream EFETIVAMENTE aberto para
    /// diagnóstico persistido (marcos STARTUP_AUDIO do host). Vazio quando
    /// não aplicável. Não usar em contexto de sinal.
    [[nodiscard]] virtual std::string describeDevice() const
    {
        return {};
    }
};

/// Backend de TESTES/CI: nenhum dispositivo — o teste puxa mix() à mão.
/// (Contadores de operação; usado também quando não há áudio no host.)
class NullAudioBackend final : public IAudioBackend {
public:
    eng::core::Result<void> start(AudioMixer& mixer) override;
    void stop() override;
    bool isRunning() const noexcept override { return running_; }
    std::string_view name() const noexcept override { return "null"; }
    std::string describeDevice() const override;

    std::uint64_t startCount{0};
    std::uint64_t stopCount{0};

private:
    bool running_{false};
};

/// Fábrica do backend padrão da plataforma (AAUDIO no Android via dlopen —
/// AAudioBackend.cpp; null no Linux/testes). Dono é o chamador (host).
[[nodiscard]] std::unique_ptr<IAudioBackend> createDefaultBackend();

}  // namespace eng::audio
