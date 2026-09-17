#include "eng/audio/Audio.hpp"

/// AAudioBackend — saída REAL no Android (FASE 9, §6.9; ADR-047).
///
/// Padrão dos backends gráficos (ADR-037/038): dlopen("libaaudio.so") em
/// runtime + dlsym — SEM link edit e SEM include de headers Android no
/// engine (o include existe apenas sob __ANDROID__ neste TU). Dispositivo
/// < API 26: libaaudio.so não existe → start() falha com erro preciso (o
/// chamador decide — null backend como fallback EXPLÍCITO).
///
/// Callback de áudio roda em THREAD PRÓPRIA do AAudio: o mixer foi
/// projetado para pull thread-safe (mutex único — §D7/ADR-047).
///
/// O handle NÃO é dlclose'd (padrão do engine: bibliotecas de sistema
/// retêm estado/threads — ver ADR-037).

#ifdef __ANDROID__

#include <aaudio/AAudio.h>
#include <dlfcn.h>

#include <cstring>
#include <memory>

namespace eng::audio {

namespace {

// Funções resolvidas por dlsym (assinaturas do header AAudio).
using CreateStreamBuilderFn = AAudioStreamBuilder* (*)();
using BuilderSetIntFn = void (*)(AAudioStreamBuilder*, int32_t);
using BuilderSetFormatFn = void (*)(AAudioStreamBuilder*, aaudio_format_t);
using BuilderSetCallbackFn = void (*)(AAudioStreamBuilder*,
                                      AAudioStream_dataCallback, void*);
using OpenStreamFn = aaudio_result_t (*)(AAudioStreamBuilder*,
                                         AAudioStream**);
using StreamControlFn = aaudio_result_t (*)(AAudioStream*);
using StreamCloseFn = aaudio_result_t (*)(AAudioStream*);
using BuilderDeleteFn = void (*)(AAudioStreamBuilder*);

struct AAudioApi {
    void* library{nullptr};
    CreateStreamBuilderFn createStreamBuilder{nullptr};
    BuilderSetIntFn setSampleRate{nullptr};
    BuilderSetIntFn setChannelCount{nullptr};
    BuilderSetFormatFn setFormat{nullptr};
    BuilderSetCallbackFn setDataCallback{nullptr};
    OpenStreamFn openStream{nullptr};
    StreamControlFn requestStart{nullptr};
    StreamControlFn requestStop{nullptr};
    StreamCloseFn closeStream{nullptr};
    BuilderDeleteFn deleteBuilder{nullptr};
};

[[nodiscard]] bool loadAAudioApi(AAudioApi& api)
{
    api.library = dlopen("libaaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (api.library == nullptr) {
        return false;
    }
    api.createStreamBuilder = reinterpret_cast<CreateStreamBuilderFn>(
        dlsym(api.library, "AAudio_createStreamBuilder"));
    api.setSampleRate = reinterpret_cast<BuilderSetIntFn>(
        dlsym(api.library, "AAudioStreamBuilder_setSampleRate"));
    api.setChannelCount = reinterpret_cast<BuilderSetIntFn>(
        dlsym(api.library, "AAudioStreamBuilder_setChannelCount"));
    api.setFormat = reinterpret_cast<BuilderSetFormatFn>(
        dlsym(api.library, "AAudioStreamBuilder_setFormat"));
    api.setDataCallback = reinterpret_cast<BuilderSetCallbackFn>(
        dlsym(api.library, "AAudioStreamBuilder_setDataCallback"));
    api.openStream = reinterpret_cast<OpenStreamFn>(
        dlsym(api.library, "AAudioStreamBuilder_openStream"));
    api.requestStart = reinterpret_cast<StreamControlFn>(
        dlsym(api.library, "AAudioStream_requestStart"));
    api.requestStop = reinterpret_cast<StreamControlFn>(
        dlsym(api.library, "AAudioStream_requestStop"));
    api.closeStream = reinterpret_cast<StreamCloseFn>(
        dlsym(api.library, "AAudioStream_close"));
    api.deleteBuilder = reinterpret_cast<BuilderDeleteFn>(
        dlsym(api.library, "AAudioStreamBuilder_delete"));
    return api.createStreamBuilder != nullptr &&
           api.setSampleRate != nullptr && api.setChannelCount != nullptr &&
           api.setFormat != nullptr && api.setDataCallback != nullptr &&
           api.openStream != nullptr && api.requestStart != nullptr &&
           api.requestStop != nullptr && api.closeStream != nullptr &&
           api.deleteBuilder != nullptr;
}

AudioMixer* gMixer = nullptr;

/// Pull do AAudio (thread de áudio): soma no buffer de saída.
aaudio_data_callback_result_t streamCallback(
    AAudioStream* /*stream*/, void* /*userData*/, void* audioData,
    int32_t numFrames)
{
    if (gMixer == nullptr || audioData == nullptr || numFrames <= 0) {
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    auto* out = static_cast<float*>(audioData);
    std::memset(out, 0, static_cast<std::size_t>(numFrames) *
                            gMixer->channels() * sizeof(float));
    gMixer->mix(out, static_cast<std::uint32_t>(numFrames));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

class AAudioBackend final : public IAudioBackend {
public:
    ~AAudioBackend() override { stop(); }

    eng::core::Result<void> start(AudioMixer& mixer) override
    {
        if (running_) {
            return {};
        }
        if (!loadAAudioApi(api_)) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::NotSupported,
                "AAudio indisponível (libaaudio.so ausente — API < 26?)"});
        }
        gMixer = &mixer;

        AAudioStreamBuilder* builder = api_.createStreamBuilder();
        if (builder == nullptr) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::Unknown,
                "AAudio_createStreamBuilder devolveu null"});
        }
        api_.setSampleRate(
            builder, static_cast<std::int32_t>(mixer.sampleRate()));
        api_.setChannelCount(
            builder, static_cast<std::int32_t>(mixer.channels()));
        api_.setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
        api_.setDataCallback(builder, &streamCallback, nullptr);

        AAudioStream* stream = nullptr;
        const aaudio_result_t opened = api_.openStream(builder, &stream);
        api_.deleteBuilder(builder);
        if (opened != AAUDIO_OK) {
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::Unknown,
                "AAudio openStream falhou (código " +
                    std::to_string(opened) + ")"});
        }
        const aaudio_result_t started = api_.requestStart(stream);
        if (started != AAUDIO_OK) {
            api_.closeStream(stream);
            return eng::core::makeUnexpected(eng::core::Error{
                eng::core::StatusCode::Unknown,
                "AAudio requestStart falhou (código " +
                    std::to_string(started) + ")"});
        }
        stream_ = stream;
        running_ = true;
        return {};
    }

    void stop() override
    {
        if (!running_ || stream_ == nullptr) {
            return;
        }
        (void)api_.requestStop(stream_);
        (void)api_.closeStream(stream_);
        stream_ = nullptr;
        running_ = false;
        gMixer = nullptr;
    }

    bool isRunning() const noexcept override { return running_; }
    std::string_view name() const noexcept override { return "AAudio"; }

private:
    AAudioApi api_{};
    AAudioStream* stream_{nullptr};
    bool running_{false};
};

}  // namespace

std::unique_ptr<IAudioBackend> createDefaultBackend()
{
    return std::make_unique<AAudioBackend>();
}

}  // namespace eng::audio

#else  // Linux/testes: sem device de áudio — null (pull manual do teste).

#include <memory>

namespace eng::audio {

std::unique_ptr<IAudioBackend> createDefaultBackend()
{
    return std::make_unique<NullAudioBackend>();
}

}  // namespace eng::audio

#endif
