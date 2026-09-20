#ifndef ENG_AUDIO_AUDIOADAPT_HPP
#define ENG_AUDIO_AUDIOADAPT_HPP

/// \file AudioAdapt.hpp
/// \brief P3.4 — camada de adaptação mixer→device (crash Realme C33).
///
/// O PROBLEMA (missão P3.4): os valores passados ao
/// AAudioStreamBuilder são SUGESTÕES — o HAL do dispositivo abre o
/// stream com canais/formato/taxa PRÓPRIOS. O callback P3.3 presumia o
/// layout pedido e RECUSAVA divergências; um stream que abrisse com
/// layout inesperado derrubava o áudio do app inteiro. A correção
/// arquitetural é ADAPTAR: conversão de formato, mapeamento de canais
/// e reamostragem acontecem ENTRE o mixer (f32, layout da engine) e o
/// buffer do device (layout EFETIVO que o HAL abriu).
///
/// Este TU não depende do NDK (testável no Linux; o AAudioBackend é
/// quem converte aaudio_format_t → DeviceSampleFormat na fronteira).
/// Tudo aqui é usado na THREAD DE ÁUDIO (callback): sem alocação no
/// caminho quente, sem locks, sem I/O.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace eng::audio {

// =============================================================================
// Formato do device
// =============================================================================

/// Formato de amostra do DISPOSITIVO suportado pelo conversor. Os
/// valores SÃO as constantes aaudio_format_t do NDK (a fronteira do
/// AAudioBackend converte sem tradução — ver AAudioBackend.cpp).
enum class DeviceSampleFormat : std::int32_t {
    Int16 = 1,  ///< AAUDIO_FORMAT_PCM_I16
    Float = 2,  ///< AAUDIO_FORMAT_PCM_FLOAT
    Int32 = 4,  ///< AAUDIO_FORMAT_PCM_I32
};

/// Bytes por amostra (por canal) do formato; 0 = inválido.
[[nodiscard]] constexpr std::uint32_t bytesPerSample(
    DeviceSampleFormat format) noexcept
{
    switch (format) {
    case DeviceSampleFormat::Int16: return 2;
    case DeviceSampleFormat::Float: return 4;
    case DeviceSampleFormat::Int32: return 4;
    }
    return 0;
}

/// Converte `frames` frames intercalados float (`srcChannels`) para o
/// layout EFETIVO do device (`dstChannels` + `format`), escrevendo em
/// `dst`. Sem alocação; defensivo contra nullptr (escreve silêncio).
///
/// Política de canais (documentada e testada):
/// - dstCh == srcCh: cópia direta;
/// - dstCh >  srcCh: canais extras DUPLICAM o último canal de origem
///   (mono→estéreo idêntico);
/// - dstCh <  srcCh: os primeiros dstCh-1 passam direto; o ÚLTIMO
///   canal de destino recebe a SOMA clampada dos canais restantes
///   (downmix 2→1 = L+R).
/// Amostras são clampadas em [-1, 1] antes da quantização inteira.
void convertFrames(const float* src, void* dst, std::uint32_t frames,
                   std::uint32_t srcChannels, std::uint32_t dstChannels,
                   DeviceSampleFormat format) noexcept;

// =============================================================================
// Resampler linear de fase exata (causa 1 completa: a TAXA também é
// sugestão)
// =============================================================================

/// Resampler linear com fase EXATA (posições em aritmética inteira de
/// 64 bits — sem drift acumulado). Um por stream; usado SOMENTE na
/// thread de áudio (o push/produce do callback).
///
/// Modelo push/pull com HISTÓRICO INTERNO: quando o device consome a
/// uma taxa MAIOR que o mixer (razão < 1), um lote de saídas pode não
/// precisar de nenhum frame novo — a interpolação continua do
/// histórico retido. Inversamente (razão > 1), o lote puxa vários
/// frames novos. O chamador nunca raciocina sobre seams.
///
/// Contrato (o chamador é o callback do backend, taxa divergente):
/// 1. `configure(inRate, outRate, channels, maxOutFrames)` na thread
///    do host, antes do start do stream;
/// 2. por callback: misture `framesToPush(numFrames)` frames do mixer
///    em um stage do chamador e `push()`-os (0 é válido e esperado em
///    razões densas);
/// 3. `produce(numFrames, dst)` gera EXATOS `numFrames` frames
///    interpolados em `dst` (intercalado, `channels`).
///
/// A posição da saída n é n*inRate/outRate em inteiro de 64 bits —
/// exata em qualquer horizonte de execução.
class LinearResampler final {
public:
    /// Razão máxima admitida entre as taxas (fora disso o backend
    /// recusa o stream — device inconsistente).
    static constexpr std::uint32_t kMaxRateRatio = 8;
    /// Máximo de canais suportados.
    static constexpr std::uint32_t kMaxChannels = 8;

    /// Configura taxas/canais e dimensiona o histórico interno.
    /// `maxOutFrames` = tamanho máximo de lote de saída que o
    /// chamador vai pedir (o callback faz chunking nesse valor).
    /// false quando algo for inválido (taxa 0/absurda, canais 0 ou
    /// > kMaxChannels, lote 0, ou razão fora de [1/8, 8]).
    [[nodiscard]] bool configure(std::uint32_t inRate,
                                 std::uint32_t outRate,
                                 std::uint32_t channels,
                                 std::uint32_t maxOutFrames) noexcept;

    /// true quando as taxas são iguais (o backend nem interpola).
    [[nodiscard]] bool isPassthrough() const noexcept
    {
        return num_ == den_;
    }

    /// Frames NOVOS do mixer que o chamador deve misturar + push ANTES
    /// de produzir `outFrames` saídas (0 = o histórico já cobre).
    [[nodiscard]] std::uint32_t framesToPush(
        std::uint32_t outFrames) const noexcept;

    /// Teto de framesToPush para `maxOutFrames` — para dimensionar o
    /// stage do chamador UMA vez (thread do host).
    [[nodiscard]] std::uint32_t maxPushFrames(
        std::uint32_t maxOutFrames) const noexcept;

    /// Anexa `srcCount` frames misturados (intercalado `channels`) ao
    /// histórico interno. Sem alocação no caminho quente (compacão
    /// por memmove quando o anel enche).
    void push(const float* src, std::uint32_t srcCount) noexcept;

    /// Produz EXATOS `outFrames` frames em `dst` (intercalado).
    /// Sem alocação; defensivo (falta de histórico → segura a última
    /// amostra, nunca lê fora).
    void produce(std::uint32_t outFrames, float* dst) noexcept;

    /// Saídas produzidas / frames empurrados desde o configure
    /// (diagnóstico de teste).
    [[nodiscard]] std::uint64_t produced() const noexcept
    {
        return totalOut_;
    }
    [[nodiscard]] std::uint64_t pushed() const noexcept
    {
        return pushed_;
    }

private:
    /// Posição (frame de entrada) da saída `n`.
    [[nodiscard]] std::uint64_t positionOf(std::uint64_t n) const noexcept
    {
        return (n * num_) / den_;
    }

    /// razão = num_/den_ frames de entrada por frame de saída
    /// (reduzidos por gcd; 1/1 = passthrough).
    std::uint64_t num_{1};
    std::uint64_t den_{1};
    std::uint32_t channels_{0};
    std::uint64_t totalOut_{0};  ///< saídas produzidas desde o configure
    std::uint64_t pushed_{0};    ///< frames de entrada recebidos
    std::uint64_t base_{0};      ///< frame absoluto armazenado em buf_[0]
    std::vector<float> buf_{};   ///< histórico linear com compacão
    std::uint32_t capacity_{0};  ///< capacidade em FRAMES de buf_
};

// =============================================================================
// Portão de drenagem do callback (causa 3)
// =============================================================================

/// Portão que garante que o callback de áudio NÃO toca o mixer após o
/// dono (thread do host) decidir parar:
///
/// 1. o dono `close()` o portão (não-bloqueante);
/// 2. o dono pede a parada do stream (requestStop) e espera o estado
///    assentar;
/// 3. o dono `waitDrained(timeout)` — nenhum callback nosso executa;
/// 4. só ENTÃO closeStream/destruição do mixer.
///
/// Protocolo tryEnter com dupla checagem: quem entra ANTES do close
/// é visto pelo waitDrained; quem entra DEPOIS do close vê a bandeira
/// e recusa. A memória é seq_cst (frio: uma entrada por bloco de
/// áudio).
class CallbackGate final {
public:
    /// RAII: sai do portão na destruição.
    class Scope final {
    public:
        explicit Scope(CallbackGate& gate) noexcept : gate_{&gate} {}
        ~Scope() { gate_->exit(); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        CallbackGate* gate_;
    };

    /// [thread de áudio] Entra; false = portão fechado: o chamador
    /// NÃO deve tocar em mais nada (retorna STOP ao AAudio).
    [[nodiscard]] bool tryEnter() noexcept
    {
        if (closed_.load(std::memory_order_seq_cst)) {
            return false;
        }
        entrants_.fetch_add(1, std::memory_order_seq_cst);
        if (closed_.load(std::memory_order_seq_cst)) {
            entrants_.fetch_sub(1, std::memory_order_seq_cst);
            return false;
        }
        return true;
    }

    /// [thread de áudio] Sai (preferir Scope).
    void exit() noexcept
    {
        entrants_.fetch_sub(1, std::memory_order_seq_cst);
    }

    /// [dono] Fecha: novos entrantes recusados. NÃO bloqueia.
    void close() noexcept
    {
        closed_.store(true, std::memory_order_seq_cst);
    }

    /// [dono] Espera todos os entrantes saírem (passo de 1 ms).
    /// false = estourou o tempo (device patológico — o closeStream
    /// do AAudio ainda faz o join final da thread interna).
    [[nodiscard]] bool waitDrained(std::int64_t timeoutMs) noexcept
    {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds{timeoutMs};
        while (entrants_.load(std::memory_order_seq_cst) > 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        return true;
    }

    /// [dono] Reabre para um novo start (zera a contagem — chamado
    /// ANTES de qualquer callback existir).
    void reopen() noexcept
    {
        closed_.store(false, std::memory_order_seq_cst);
        entrants_.store(0, std::memory_order_seq_cst);
    }

    [[nodiscard]] bool isClosed() const noexcept
    {
        return closed_.load(std::memory_order_seq_cst);
    }

private:
    std::atomic<bool> closed_{true};
    std::atomic<std::int32_t> entrants_{0};
};

}  // namespace eng::audio

#endif  // ENG_AUDIO_AUDIOADAPT_HPP
