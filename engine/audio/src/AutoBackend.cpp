#include "eng/audio/Audio.hpp"

/// AutoAudioBackend — CADEIA de seleção automática de backend (P4.1,
/// T3/D6). O defeito D6 device-verificado: o AAudio do Unisoc T612
/// (Realme C33) recusa abrir (builder=null → NullBackend gracioso) e o
/// autor ficava SEM SOM sem alternativa. A cadeia:
///
///   1. AAudio  (caminho preferido — MMAP/Legacy do framework);
///   2. OpenSL ES (caminho Legacy direto — AudioTrack; abre quando o
///      MMAP/binder do AAudio recusa);
///   3. erro composto (os DOIS motivos — o host instala o NullBackend
///     gracioso e o HUD do editor mostra o estado honesto).
///
/// NUNCA fallback silencioso: o backend vencedor é anunciado no marco
/// AUDIO_BACKEND_SELECTED (hook → diagnóstico persistido) e exposto ao
/// autor pelo HUD ("Áudio: opensl — AAudio recusado pelo device").
///
/// Linux/testes: sem device — createDefaultBackend() devolve o
/// NullAudioBackend (pull manual dos testes; AudioTests intocados).

#include <string>

#include "eng/log/Macros.hpp"

namespace eng::audio {

namespace {

ENG_LOG_CATEGORY("audio.auto");

/// Estágio do RESULTADO da seleção (o hook do host persiste 1:1).
constexpr char kStageSelected[] = "AUDIO_BACKEND_SELECTED";

}  // namespace

AutoAudioBackend::~AutoAudioBackend()
{
    stop();
}

eng::core::Result<void> AutoAudioBackend::start(AudioMixer& mixer)
{
    if (active_ != nullptr && active_->isRunning()) {
        return {};  // idempotente
    }
    active_.reset();

#if defined(__ANDROID__)
    std::string aaudioError{"backend ausente"};
    std::string openslError{"backend ausente"};
    // ---- 1) AAudio ------------------------------------------------------
    {
        auto candidate = createAAudioBackend();
        if (candidate != nullptr) {
            auto started = candidate->start(mixer);
            if (started.ok()) {
                reportBackendStage(kStageSelected, "ok", "aaudio");
                ENG_INFO("backend de áudio: aaudio");
                active_ = std::move(candidate);
                return {};
            }
            aaudioError = started.error().message;
            ENG_WARN("AAudio recusado ({}), tentando OpenSL ES",
                     aaudioError);
        }
    }
    // ---- 2) OpenSL ES ---------------------------------------------------
    {
        auto candidate = createOpenSlEsBackend();
        if (candidate != nullptr) {
            auto started = candidate->start(mixer);
            if (started.ok()) {
                reportBackendStage(kStageSelected, "ok",
                                   "opensl (aaudio recusado)");
                ENG_INFO(
                    "backend de áudio: opensl (fallback — AAudio recusou)");
                active_ = std::move(candidate);
                return {};
            }
            openslError = started.error().message;
            ENG_WARN("OpenSL ES também recusado ({})", openslError);
        }
    }
    // ---- 3) recusa composta (o host decide o NullBackend) ---------------
    reportBackendStage(kStageSelected, "failed",
                       "aaudio e opensl recusaram");
    return eng::core::makeUnexpected(eng::core::Error{
        eng::core::StatusCode::Unknown,
        "AAudio: " + aaudioError + " | OpenSL ES: " + openslError});
#else
    // Linux/testes: NullAudioBackend direto (pull manual — contratos dos
    // testes de áudio permanecem os do P3.5).
    auto nullBackend = std::make_unique<NullAudioBackend>();
    auto started = nullBackend->start(mixer);
    if (started.isError()) {
        return started;
    }
    reportBackendStage(kStageSelected, "ok", "null (sem device no host)");
    active_ = std::move(nullBackend);
    return {};
#endif
}

void AutoAudioBackend::stop()
{
    if (active_ != nullptr) {
        active_->stop();
        active_.reset();
    }
}

bool AutoAudioBackend::isRunning() const noexcept
{
    return active_ != nullptr && active_->isRunning();
}

std::string_view AutoAudioBackend::name() const noexcept
{
    return active_ != nullptr ? active_->name() : "auto";
}

std::string AutoAudioBackend::describeDevice() const
{
    return active_ != nullptr ? active_->describeDevice() : std::string{};
}

bool AutoAudioBackend::hasFirstCallbackFired() const noexcept
{
    return active_ != nullptr && active_->hasFirstCallbackFired();
}

std::unique_ptr<IAudioBackend> createDefaultBackend()
{
#if defined(__ANDROID__)
    return std::make_unique<AutoAudioBackend>();
#else
    return std::make_unique<NullAudioBackend>();
#endif
}

}  // namespace eng::audio
