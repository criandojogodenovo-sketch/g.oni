/// Testes de eng::audio (FASE 9, missão §6.11): load (WAV), play/stop/
/// pause/resume/volume/loop, cleanup, streaming de Music, buses, pull
/// concorrente (stress thread-safe) e NullBackend.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "eng/audio/Audio.hpp"
#include "eng/audio/Wav.hpp"
#include "eng/fs/MemoryFileSystem.hpp"

namespace {

using namespace eng::audio;

/// Constrói um WAV PCM16 em bytes (header + N amostras interleaved).
std::vector<std::byte> makeWav16(std::uint32_t sampleRate,
                                 std::uint16_t channels,
                                 const std::vector<std::int16_t>& samples,
                                 bool withListChunk = false)
{
    std::vector<std::byte> out;
    const auto push = [&out](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::byte*>(data);
        out.insert(out.end(), bytes, bytes + size);
    };
    const char riff[] = "RIFF";
    const char wave[] = "WAVE";
    push(riff, 4);
    // Corpo do LIST: "INFO" + filler de 10 bytes (14 total, par).
    const std::uint32_t listBodySize = withListChunk ? 14 : 0;
    const std::uint32_t dataSize =
        static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffSize =
        4 + (8 + 16) + (8 + listBodySize) + (8 + dataSize);
    push(&riffSize, 4);
    push(wave, 4);
    if (withListChunk) {
        const char list[] = "LIST";
        push(list, 4);
        push(&listBodySize, 4);
        const char info[] = "INFO";
        push(info, 4);
        const char filler[] = "IGTR ABC ";
        push(filler, 10);
    }
    const char fmt[] = "fmt ";
    push(fmt, 4);
    const std::uint32_t fmtSize = 16;
    push(&fmtSize, 4);
    const std::uint16_t format = 1;
    const std::uint16_t blockAlign = channels * 2;
    const std::uint32_t byteRate = sampleRate * blockAlign;
    const std::uint16_t bits = 16;
    push(&format, 2);
    push(&channels, 2);
    push(&sampleRate, 4);
    push(&byteRate, 4);
    push(&blockAlign, 2);
    push(&bits, 2);
    const char data[] = "data";
    push(data, 4);
    push(&dataSize, 4);
    for (const std::int16_t sample : samples) {
        push(&sample, 2);
    }
    return out;
}

}  // namespace

// =============================================================================
// WAV (§6.11: load)
// =============================================================================

TEST_CASE("audio: WAV PCM16 parse com valores exatos", "[audio]")
{
    const std::vector<std::int16_t> samples{0, 16384, -16384, 32767, -32768};
    const auto bytes = makeWav16(44100, 1, samples);
    auto parsed = Wav::parse(bytes);
    REQUIRE(parsed.ok());
    CHECK(parsed.value().sampleRate == 44100);
    CHECK(parsed.value().channels == 1);
    REQUIRE(parsed.value().samples.size() == samples.size());
    CHECK_THAT(parsed.value().samples[1],
               Catch::Matchers::WithinAbs(16384.f / 32768.f, 1e-6f));
    CHECK_THAT(parsed.value().samples[2],
               Catch::Matchers::WithinAbs(-0.5f, 1e-6f));
    CHECK_THAT(parsed.value().samples[4],
               Catch::Matchers::WithinAbs(-1.f, 1e-6f));
}

TEST_CASE("audio: WAV com chunk LIST é pulSado; estéreo ok", "[audio]")
{
    const std::vector<std::int16_t> samples{100, -100, 200, -200};
    const auto bytes = makeWav16(48000, 2, samples, /*withListChunk=*/true);
    auto parsed = Wav::parse(bytes);
    REQUIRE(parsed.ok());
    CHECK(parsed.value().channels == 2);
    CHECK(parsed.value().samples.size() == 4);
}

TEST_CASE("audio: WAV rejeita lixo com erro preciso", "[audio]")
{
    CHECK(Wav::parse({}).isError());
    std::vector<std::byte> shortFile(20, std::byte{0});
    CHECK(Wav::parse(shortFile).isError());
    std::vector<std::byte> notRiff(100, std::byte{0});
    CHECK(Wav::parse(notRiff).isError());
}

// =============================================================================
// Sound/Voice/mixer (§6.11: play/stop/pause/resume/volume/loop/cleanup)
// =============================================================================

TEST_CASE("audio: play de Sound mixa com volume e loop", "[audio]")
{
    WavData data;
    data.sampleRate = 48000;
    data.channels = 1;
    data.samples = {0.5f, 0.5f, 0.5f, 0.5f}; // 4 frames
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 1);
    auto voice = mixer.playSound(sound.value(), 0, /*volume=*/0.5f,
                                 /*loop=*/false);
    REQUIRE(voice.ok());
    CHECK(mixer.isPlaying(voice.value()));

    // Mixa 4 frames → soma 0.5*0.5 por amostra.
    std::vector<float> out(8, 0.f);
    mixer.mix(out.data(), 4);
    CHECK_THAT(out[0], Catch::Matchers::WithinAbs(0.25f, 1e-5f));
    CHECK_THAT(out[3], Catch::Matchers::WithinAbs(0.25f, 1e-5f));

    // Sem loop: voz termina e é coletada no tick.
    std::vector<float> out2(4, 0.f);
    mixer.mix(out2.data(), 4);
    CHECK_FALSE(mixer.isPlaying(voice.value()));
    mixer.tick();
    CHECK(mixer.liveVoices() == 0);
    CHECK(mixer.stats().voicesFinished == 1);
}

TEST_CASE("audio: loop de Sound reinicia o buffer", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = {1.f, -1.f};
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 1);
    auto voice = mixer.playSound(sound.value(), 0, 1.f, /*loop=*/true);
    REQUIRE(voice.ok());
    std::vector<float> out(6, 0.f);
    mixer.mix(out.data(), 6);
    // Padrão 1,-1,1,-1,1,-1 se o loop reiniciou.
    CHECK_THAT(out[4], Catch::Matchers::WithinAbs(1.f, 1e-5f));
    CHECK_THAT(out[5], Catch::Matchers::WithinAbs(-1.f, 1e-5f));
    CHECK(mixer.isPlaying(voice.value()));
}

TEST_CASE("audio: stop/pause/resume e handle obsoleto é no-op", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = std::vector<float>(100, 0.5f);
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 1);
    auto voice = mixer.playSound(sound.value());
    REQUIRE(voice.ok());

    mixer.pause(voice.value());
    CHECK(mixer.isPaused(voice.value()));
    CHECK_FALSE(mixer.isPlaying(voice.value()));
    std::vector<float> out(8, 0.f);
    mixer.mix(out.data(), 8); // pausado: NÃO mixa
    CHECK_THAT(out[0], Catch::Matchers::WithinAbs(0.f, 1e-6f));

    mixer.resume(voice.value());
    mixer.mix(out.data(), 8);
    CHECK_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 1e-5f));

    mixer.stop(voice.value());
    CHECK_FALSE(mixer.isPlaying(voice.value()));

    // Handle obsoleto: todas as ops são no-op seguras (§6.10).
    mixer.stop(voice.value());
    mixer.pause(voice.value());
    mixer.setVolume(voice.value(), 2.f);
    CHECK_FALSE(mixer.isPlaying(voice.value()));
}

TEST_CASE("audio: volume por voz e ganho por bus multiplicam", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = {1.f, 1.f}; // 2 frames — 1 antes, 1 depois da troca
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 1);
    const auto sfx = mixer.createBus("sfx", 0.25f);
    auto voice = mixer.playSound(sound.value(), sfx, /*volume=*/0.5f);
    REQUIRE(voice.ok());
    std::vector<float> out(1, 0.f);
    mixer.mix(out.data(), 1);
    CHECK_THAT(out[0], Catch::Matchers::WithinAbs(0.125f, 1e-5f)); // .5*.25

    // Bus muda em voo.
    mixer.setBusGain(sfx, 1.f);
    std::vector<float> out2(1, 0.f);
    mixer.mix(out2.data(), 1);
    CHECK_THAT(out2[0], Catch::Matchers::WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("audio: mono é duplicado para estéreo", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = {0.5f};
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 2); // SAÍDA estéreo
    auto voice = mixer.playSound(sound.value());
    REQUIRE(voice.ok());
    std::vector<float> out(2, 0.f);
    mixer.mix(out.data(), 1);
    CHECK_THAT(out[0], Catch::Matchers::WithinAbs(0.5f, 1e-5f));
    CHECK_THAT(out[1], Catch::Matchers::WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("audio: pauseAll/resumeAll/stopAll (lifecycle §6.10)", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = std::vector<float>(50, 0.5f);
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 1);
    auto a = mixer.playSound(sound.value());
    auto b = mixer.playSound(sound.value());
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(mixer.liveVoices() == 2);

    mixer.pauseAll();
    CHECK(mixer.isPaused(a.value()));
    CHECK(mixer.isPaused(b.value()));
    mixer.resumeAll();
    CHECK(mixer.isPlaying(a.value()));
    mixer.stopAll();
    CHECK(mixer.liveVoices() == 0);
}

// =============================================================================
// Music (streaming real — §6.10)
// =============================================================================

TEST_CASE("audio: música toca por janelas (streaming) e termina", "[audio]")
{
    // 1 segundo de "áudio" PCM16 mono, 8 kHz → 8000 amostras.
    std::vector<std::int16_t> samples(8000);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = 16384;
    }
    auto fs = std::make_shared<eng::fs::MemoryFileSystem>();
    REQUIRE(fs->mkdirs(eng::fs::Path{"music"}).ok());
    REQUIRE(fs->writeAllBytes(eng::fs::Path{"music/x.wav"},
                              makeWav16(8000, 1, samples))
                .ok());

    AudioMixer mixer(8000, 1);
    auto voice = mixer.playMusic(fs, eng::fs::Path{"music/x.wav"});
    REQUIRE(voice.ok());
    CHECK(mixer.isPlaying(voice.value()));

    // Consome 7900 dos 8000 frames (ainda tocando)...
    std::vector<float> out(7900, 0.f);
    mixer.mix(out.data(), 7900);
    CHECK(mixer.isPlaying(voice.value()));
    // ...e termina no pull seguinte (janela cruzou o fim do arquivo).
    std::vector<float> out2(200, 0.f);
    mixer.mix(out2.data(), 200);
    CHECK_FALSE(mixer.isPlaying(voice.value())); // terminou
    // Conteúdo correto: 16384/32768 = 0.5.
    CHECK_THAT(out[100], Catch::Matchers::WithinAbs(0.5f, 1e-4f));
    CHECK_THAT(out2[10], Catch::Matchers::WithinAbs(0.5f, 1e-4f));
}

TEST_CASE("audio: música em loop recarrega o início", "[audio]")
{
    std::vector<std::int16_t> samples{20000, -20000};
    auto fs = std::make_shared<eng::fs::MemoryFileSystem>();
    REQUIRE(fs->writeAllBytes(eng::fs::Path{"loop.wav"},
                              makeWav16(8000, 1, samples))
                .ok());

    AudioMixer mixer(8000, 1);
    auto voice = mixer.playMusic(fs, eng::fs::Path{"loop.wav"}, 0, 1.f,
                                 /*loop=*/true);
    REQUIRE(voice.ok());
    std::vector<float> out(5, 0.f);
    mixer.mix(out.data(), 5); // 2 + 2 + 1 → reiniciou
    CHECK_THAT(out[4], Catch::Matchers::WithinAbs(20000.f / 32768.f, 1e-4f));
    CHECK(mixer.isPlaying(voice.value()));
}

// =============================================================================
// Stress concorrente (pull × play/stop — §D7)
// =============================================================================

TEST_CASE("audio: pull concorrente com play/stop (thread-safety)", "[audio]")
{
    WavData data;
    data.channels = 1;
    data.samples = std::vector<float>(256, 0.25f);
    auto sound = Sound::fromWav(data);
    REQUIRE(sound.ok());

    AudioMixer mixer(48000, 2);
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> mixedFrames{0};

    // "Thread de áudio": puxa mix continuamente.
    std::thread audioThread([&] {
        std::vector<float> buffer(512, 0.f);
        while (!stop.load()) {
            std::fill(buffer.begin(), buffer.end(), 0.f);
            mixer.mix(buffer.data(), 256);
            mixedFrames += 256;
        }
    });

    // Garante que a thread de áudio puxou AO MENOS UMA janela antes da
    // rajada (scheduling: em -O3 a rajada termina antes do primeiro pull).
    for (int spin = 0; spin < 2000 && mixedFrames.load() == 0; ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(mixedFrames.load() > 0);

    // "Thread do jogo": cria/pausa/para vozes em rajada.
    for (int i = 0; i < 400; ++i) {
        auto voice = mixer.playSound(sound.value());
        if (voice.ok()) {
            mixer.pause(voice.value());
            mixer.resume(voice.value());
            if (i % 2 == 0) {
                mixer.stop(voice.value());
            }
        }
        mixer.tick();
    }
    stop.store(true);
    audioThread.join();

    CHECK(mixedFrames.load() > 0);
    // Nenhum crash/UB — o ASan/TSan do preset valida a corrida.
}

// =============================================================================
// NullBackend (§6.9 — contadores)
// =============================================================================

TEST_CASE("audio: NullBackend conta start/stop", "[audio]")
{
    AudioMixer mixer;
    auto backend = eng::audio::createDefaultBackend();
    REQUIRE(backend != nullptr);
    CHECK(backend->name() == std::string_view{"null"});
    auto started = backend->start(mixer);
    REQUIRE(started.ok());
    CHECK(backend->isRunning());
    backend->stop();
    CHECK_FALSE(backend->isRunning());
}
