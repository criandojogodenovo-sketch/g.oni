/// Diagnostics.cpp — P3.1: implementação (ver Diagnostics.hpp para o porquê).
///
/// Escrita de arquivo: cada linha é fprintf + fflush + fsync — o custo por
/// estágio é irrelevante (dezenas de eventos por processo) e o benefício é
/// total: evidência sobrevive à morte súbita.

#include "eng/editor/Diagnostics.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "eng/log/Macros.hpp"

#include <ucontext.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace eng::editor::diag {
namespace {

ENG_LOG_CATEGORY("editor.diag");

constexpr std::size_t kMaxPath = 288;
constexpr std::size_t kMaxStage = 64;
constexpr std::size_t kMaxDetail = 160;

/// Estado global do tracer (UI thread por contrato ADR-035).
struct TracerState {
    std::mutex mutex{};
    bool initialized{false};
    char dir[kMaxPath]{};
    char startupPath[kMaxPath]{};
    char crashPath[kMaxPath]{};
    /// último estágio (lido pelo crash handler — best-effort)
    char lastStage[kMaxStage]{'-'};
    char lastDetail[kMaxDetail]{};
    std::FILE* startupFile{nullptr};  ///< append stream (flush+fsync por linha)
};

TracerState& tracer() {
    static TracerState state;
    return state;
}

/// P3.2 — estado do espelho de exportação. UI thread only (mesmo contrato
/// de mark/init — ADR-035). NUNCA tocado pelo crash handler.
MirrorCallback g_mirrorCallback = nullptr;
void* g_mirrorUserdata = nullptr;

/// Notifica o espelho — chamada APÓS o dado estar persistido no arquivo
/// privado (o detentor relê o arquivo do disco) e SEM nenhum lock do tracer
/// segurado (o caminho Kotlin→MediaStore não pode reentrar em diag).
void notifyMirror() {
    if (g_mirrorCallback != nullptr) {
        g_mirrorCallback(g_mirrorUserdata);
    }
}

// ---------------------------------------------------------------------------
// Parte signal-safe do crash handler: SOMENTE write() em fd pré-aberto e
// buffers estáticos. snprintf/backtrace não são async-signal-safe formais,
// mas é a prática-padrão em handlers do Android (ver debuggerd); aceitamos
// o trade-off documentado em troca de evidência que sobrevive.
// ---------------------------------------------------------------------------

int g_crashFd = -1;  ///< fd de goni_crash.log (aberto no install)
char g_crashPath[288]{};  ///< path p/ re-abrir o fd DENTRO do handler

struct CrashGlobals {
    char lastStage[kMaxStage]{'-'};
    char lastDetail[kMaxDetail]{};
    char backend[kMaxDetail]{};  ///< preenchido pelos marks de RHI
};
CrashGlobals& crashGlobals() {
    static CrashGlobals g;
    return g;
}

// ---------------------------------------------------------------------------
// P3.3 — identificação de MÓDULO + backtrace dentro do próprio handler.
//
// Por que /proc/self/maps e NÃO dladdr(3): dladdr usa os locks do dynamic
// linker — um crash DURANTE dlopen (janela real de investigação: o backend
// AAudio abre libaaudio.so sob dlopen) deadlockaria dentro do handler em
// vez de gravar a evidência. open(2)/read(2)/close(2) são async-signal-safe;
// o parse abaixo usa apenas buffers estáticos, sem alocação e sem locks.
//
// O backtrace é um frame-pointer walk (x29 no arm64, rbp no x86_64):
// leituras cruas, validadas contra o snapshot de mapeamentos LIDO antes de
// cada desreferência (apenas páginas mapeadas legíveis). O binário do app é
// compilado com frame pointers (padrão do NDK para arm64; confirmado no
// disassembly de libgoni.so do build P3.2).
// ---------------------------------------------------------------------------

/// Uma linha de /proc/self/maps (tudo que o handler precisa dela).
struct MapEntry {
    std::uintptr_t start{0};
    std::uintptr_t end{0};
    bool readable{false};
    char path[120]{};  ///< truncado se maior (suficiente p/ identificar)
};

constexpr std::size_t kMaxMapEntries = 512;
MapEntry g_mapEntries[kMaxMapEntries]{};
std::size_t g_mapEntryCount = 0;

/// hex sem 0x (formato de /proc/self/maps) — avança p.
bool parseHexField(const char*& p, std::uintptr_t& value) {
    value = 0;
    bool any = false;
    while (*p != '\0') {
        const char c = *p;
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else {
            break;
        }
        value = (value << 4) | static_cast<std::uintptr_t>(digit);
        any = true;
        ++p;
    }
    return any;
}

/// Parse de UMA linha de maps (já NUL-terminada). Formato:
/// start-end perms offset dev inode path...
bool parseMapsLine(const char* line, MapEntry& out) {
    const char* p = line;
    if (!parseHexField(p, out.start) || *p != '-') {
        return false;
    }
    ++p;
    if (!parseHexField(p, out.end) || out.end <= out.start) {
        return false;
    }
    while (*p == ' ') {
        ++p;
    }
    // perms: rwx[spl]
    out.readable = p[0] == 'r';
    // pula até o 6º campo (path): perms, offset, dev, inode já contam 4.
    int spaces = 0;
    while (*p != '\0' && spaces < 5) {
        if (*p == ' ') {
            ++spaces;
            while (*p == ' ') {
                ++p;
            }
        } else {
            ++p;
        }
    }
    if (spaces < 5) {
        out.path[0] = '\0';  // linha sem path (anon) — ainda válida
    }
    std::size_t i = 0;
    while (p[i] != '\0' && p[i] != '\n' && i + 1 < sizeof out.path) {
        out.path[i] = p[i];
        ++i;
    }
    out.path[i] = '\0';
    return true;
}

/// Carrega o snapshot de mapeamentos (chamado DENTRO do handler ou por
/// describeAddress — em ambos os casos best-effort, sem alocação).
void loadMapsSnapshot() {
    g_mapEntryCount = 0;
    const int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    char buf[4096];
    char carry[256];
    std::size_t carryLen = 0;
    std::size_t totalRead = 0;
    while (g_mapEntryCount < kMaxMapEntries && totalRead < (1u << 20)) {
        const auto got = ::read(fd, buf, sizeof buf);
        if (got <= 0) {
            break;
        }
        totalRead += static_cast<std::size_t>(got);
        std::size_t begin = 0;
        std::size_t pos = 0;
        while (pos < static_cast<std::size_t>(got) &&
               g_mapEntryCount < kMaxMapEntries) {
            if (buf[pos] == '\n') {
                // linha = carry + buf[begin, pos)
                if (carryLen + (pos - begin) + 1 <= sizeof carry) {
                    std::memcpy(carry + carryLen, buf + begin, pos - begin);
                    carry[carryLen + (pos - begin)] = '\0';
                    if (parseMapsLine(carry,
                                      g_mapEntries[g_mapEntryCount])) {
                        ++g_mapEntryCount;
                    }
                }
                carryLen = 0;
                begin = pos + 1;
            }
            ++pos;
        }
        // resto parcial → carry
        const std::size_t rest = static_cast<std::size_t>(got) - begin;
        if (rest > 0 && carryLen + rest < sizeof carry) {
            std::memcpy(carry + carryLen, buf + begin, rest);
            carryLen += rest;
        } else {
            carryLen = 0;  // linha gigante: descarta (não mapeia libs)
        }
    }
    ::close(fd);
}

const MapEntry* findMapFor(std::uintptr_t address) {
    for (std::size_t i = 0; i < g_mapEntryCount; ++i) {
        if (address >= g_mapEntries[i].start &&
            address < g_mapEntries[i].end) {
            return &g_mapEntries[i];
        }
    }
    return nullptr;
}

/// Escreve uma linha "[tag] 0xADDR module=<path|?> base=0xB off=0xD" no fd.
void writeModuleLine(int fd, const char* tag, std::uintptr_t address) {
    const MapEntry* m = findMapFor(address);
    char line[288];
    int n;
    if (m != nullptr) {
        n = std::snprintf(line, sizeof line,
                          "[%s] 0x%llx module=%s base=0x%llx off=0x%llx\n",
                          tag, static_cast<unsigned long long>(address),
                          m->path[0] != '\0' ? m->path : "anon",
                          static_cast<unsigned long long>(m->start),
                          static_cast<unsigned long long>(address - m->start));
    } else {
        n = std::snprintf(line, sizeof line,
                          "[%s] 0x%llx module=? (fora de todo mapeamento "
                          "carregado)\n",
                          tag, static_cast<unsigned long long>(address));
    }
    if (n > 0) {
        const auto written =
            ::write(fd, line, static_cast<std::size_t>(n));
        (void)written;
    }
}

/// Frame pointer do contexto interrompido (x29/rbp) + PC.
struct FaultFrame {
    std::uintptr_t pc{0};
    std::uintptr_t fp{0};
};
FaultFrame faultFrameOf(const void* context) {
    FaultFrame f;
    if (context == nullptr) {
        return f;
    }
    const auto* uc = static_cast<const ucontext_t*>(context);
#if defined(__aarch64__)
    f.pc = static_cast<std::uintptr_t>(uc->uc_mcontext.pc);
    f.fp = static_cast<std::uintptr_t>(uc->uc_mcontext.regs[29]);
#elif defined(__x86_64__)
    f.pc = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    f.fp = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
#else
    (void)uc;
#endif
    return f;
}

/// Backtrace por frame-pointer walk — cada frame validado contra o snapshot
/// de maps (só desreferência FP em página mapeada legível). Escreve no máx.
/// 24 frames; para no primeiro frame inválido.
void writeBacktrace(int fd, std::uintptr_t pc, std::uintptr_t fp) {
    writeModuleLine(fd, "bt.pc", pc);
    std::uintptr_t frame = fp;
    for (int i = 0; i < 24 && frame != 0; ++i) {
        if ((frame & 0xf) != 0 || frame < 0x1000 ||
            frame >= 0x800000000000ULL) {
            break;  // não alinhado/fora de usuário: cadeia quebrada
        }
        const MapEntry* m = findMapFor(frame);
        if (m == nullptr || !m->readable) {
            break;  // FP não aponta p/ memória legível: para AQUI
        }
        // [frame] = próximo FP; [frame+8] = endereço de retorno.
        const auto* slots =
            reinterpret_cast<const std::uintptr_t*>(frame);
        const std::uintptr_t next = slots[0];
        const std::uintptr_t ret = slots[1];
        char line[288];
        const MapEntry* rm = findMapFor(ret);
        int n;
        if (rm != nullptr) {
            n = std::snprintf(line, sizeof line,
                              "[bt] %d 0x%llx %s+0x%llx\n", i,
                              static_cast<unsigned long long>(ret),
                              rm->path[0] != '\0' ? rm->path : "anon",
                              static_cast<unsigned long long>(
                                  ret - rm->start));
        } else {
            n = std::snprintf(line, sizeof line, "[bt] %d 0x%llx ?\n", i,
                              static_cast<unsigned long long>(ret));
        }
        if (n > 0) {
            const auto written =
                ::write(fd, line, static_cast<std::size_t>(n));
            (void)written;
        }
        if (next <= frame) {
            break;  // cadeia deve SUBIR na pilha
        }
        frame = next;
    }
}

struct sigaction_restore {
    bool installed = false;
    struct sigaction previous{};
};

sigaction_restore g_handlers[6];  // SIGSEGV..SIGFPE (tabela abaixo)
constexpr int kHandledSignals[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE};
constexpr int kHandledCount = 5;

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    default: return "SIG?";
    }
}

void faultAddressString(const siginfo_t* info, const void* context,
                        char* out, std::size_t cap) {
    std::snprintf(out, cap, "%p",
                 info != nullptr ? info->si_addr : nullptr);
    // PC do faulting frame (quando disponível) — evidência extra no arquivo.
#if defined(__x86_64__)
    if (const auto* uc = static_cast<const ucontext_t*>(context); uc != nullptr) {
        std::snprintf(out, cap, "%p (pc=%p)", info != nullptr ? info->si_addr : nullptr,
                      reinterpret_cast<void*>(uc->uc_mcontext.gregs[REG_RIP]));
    }
#elif defined(__aarch64__)
    if (const auto* uc = static_cast<const ucontext_t*>(context); uc != nullptr) {
        std::snprintf(out, cap, "%p (pc=%p)", info != nullptr ? info->si_addr : nullptr,
                      reinterpret_cast<void*>(uc->uc_mcontext.pc));
    }
#else
    (void)context;
#endif
}

/// Handler: grava o relatório e ENCADEIA o handler anterior — o crash
/// NÃO é mascarado (tombstone/debuggerd seguem funcionando).
void crashHandler(int sig, siginfo_t* info, void* context) {
    // Self-healing: bibliotecas de terceiro podem ter FECHADO o fd do
    // install (reciclagem de descritores). open(2) é async-signal-safe.
    if (g_crashFd < 0 && g_crashPath[0] != '\0') {
        g_crashFd = ::open(g_crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    }
    if (g_crashFd >= 0) {
        char addr[64];
        faultAddressString(info, context, addr, sizeof addr);
        const CrashGlobals& g = crashGlobals();
        char line[kMaxStage + kMaxDetail * 2 + 192];
        int n = std::snprintf(
            line, sizeof line,
            "[crash] signal=%s(%d) code=%d addr=%s stage=%s detail=%s\n",
            signalName(sig), sig,
            info != nullptr ? info->si_code : 0, addr,
            g.lastStage, g.lastDetail);
        if (n > 0) {
            // best-effort: o retorno é consumido (GCC 13 do CI pune
            // (void)::write com warn_unused_result → -Werror)
            const auto written =
                ::write(g_crashFd, line, static_cast<std::size_t>(n));
            (void)written;
        }
        // P3.3 — evidência decisiva: MÓDULO do pc, do alvo do acesso e
        // backtrace (frame-pointer). Tudo async-signal-safe: /proc/self/maps
        // via open/read (sem locks do linker — dladdr deadlockaria se o
        // crash ocorreu DURANTE dlopen), buffers estáticos, write(2).
        loadMapsSnapshot();
        const FaultFrame frame = faultFrameOf(context);
        if (frame.pc != 0) {
            writeModuleLine(g_crashFd, "pc", frame.pc);
        }
        if (info != nullptr &&
            reinterpret_cast<std::uintptr_t>(info->si_addr) != 0) {
            writeModuleLine(
                g_crashFd, "fault.addr",
                reinterpret_cast<std::uintptr_t>(info->si_addr));
        }
        if (frame.fp != 0) {
            writeBacktrace(g_crashFd, frame.pc, frame.fp);
        }
        // Header do tombstone-own: momento do crash (relógio pode estar
        // indisponível em crash — usamos apenas como metadado).
        (void)::fsync(g_crashFd);
    }
    // Encadeamento: handler anterior (ART/debuggerd no Android; default
    // no Linux = re-entrega com o disposition restaurado).
    const int idx = sig == SIGSEGV ? 0 : sig == SIGABRT ? 1 : sig == SIGBUS ? 2
                             : sig == SIGILL          ? 3
                                                      : 4;
    if (g_handlers[idx].installed) {
        const struct sigaction& prev = g_handlers[idx].previous;
        const bool siginfo = (prev.sa_flags & SA_SIGINFO) != 0;
        const auto action = siginfo
            ? reinterpret_cast<std::uintptr_t>(prev.sa_sigaction)
            : reinterpret_cast<std::uintptr_t>(prev.sa_handler);
        const auto dfl = reinterpret_cast<std::uintptr_t>(SIG_DFL);
        const auto ign = reinterpret_cast<std::uintptr_t>(SIG_IGN);
        const auto err = reinterpret_cast<std::uintptr_t>(SIG_ERR);
        if (action != 0 && action != dfl && action != ign && action != err) {
            if (siginfo) {
                prev.sa_sigaction(sig, info, context);
            } else {
                prev.sa_handler(sig);
            }
            return;
        }
    }
    // Sem handler anterior útil: restaura o default e re-entrega — o
    // processo morre COM o sinal original (nada mascarado).
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void appendFileLine(const char* path, const char* text) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    if (int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644); fd >= 0) {
        const auto written = ::write(fd, text, std::strlen(text));
        const auto synced = ::fsync(fd);
        (void)written;
        (void)synced;
        ::close(fd);
    }
}

}  // namespace

// =============================================================================
// API
// =============================================================================

void init(const char* dir) {
    TracerState& t = tracer();
    const std::lock_guard lock{t.mutex};
    if (t.initialized || dir == nullptr || dir[0] == '\0') {
        return;
    }
    std::snprintf(t.dir, sizeof t.dir, "%s", dir);
    std::snprintf(t.startupPath, sizeof t.startupPath, "%s/goni_startup.log",
                  dir);
    std::snprintf(t.crashPath, sizeof t.crashPath, "%s/goni_crash.log", dir);
    t.startupFile = std::fopen(t.startupPath, "a");
    if (t.startupFile == nullptr) {
        ENG_WARN("diag: não abriu {} (sem diagnóstico persistente)",
                 t.startupPath);
        return;  // sem arquivo — marks continuam no logcat apenas
    }
    t.initialized = true;
    installCrashHandler();

    // Header de sessão (separa execuções no arquivo cumulativo).
    char header[96];
    const long now = static_cast<long>(::time(nullptr));
    std::snprintf(header, sizeof header,
                  "[session] begin pid=%ld time=%ld\n",
                  static_cast<long>(::getpid()), now);
    (void)std::fputs(header, t.startupFile);
    std::fflush(t.startupFile);
    (void)::fsync(::fileno(t.startupFile));
    ENG_INFO("diag: startup tracing em {}", t.startupPath);

    // P3.2: cópia pública da sessão ANTES de qualquer estágio — se o
    // processo morrer já no primeiro mark, o usuário tem o header visível.
    notifyMirror();
}

void mark(const char* stage, const char* status, const char* detail) {
    if (stage == nullptr || stage[0] == '\0') {
        return;
    }
    const char* st = status != nullptr ? status : "ok";
    const char* dt = detail != nullptr ? detail : "";

    // Atualiza os globais ANTES de qualquer I/O: se o processo morrer
    // dentro do mark, o crash handler ainda vê o estágio que começou.
    {
        TracerState& t = tracer();
        const std::lock_guard lock{t.mutex};
        std::snprintf(t.lastStage, sizeof t.lastStage, "%s", stage);
        std::snprintf(t.lastDetail, sizeof t.lastDetail, "%s", dt);
    }
    CrashGlobals& g = crashGlobals();  // cópia signal-reader-friendly
    std::snprintf(g.lastStage, sizeof g.lastStage, "%s", stage);
    std::snprintf(g.lastDetail, sizeof g.lastDetail, "%s", dt);

    // Formato estável de UMA linha (grep-ável; ts em ms).
    const long ms = static_cast<long>(::time(nullptr)) * 1000;
    char line[kMaxStage + kMaxDetail + 128];
    int n = std::snprintf(line, sizeof line, "[%ld] %s %s %s\n", ms, stage,
                          st, dt);
    if (n <= 0) {
        return;
    }
    {
        TracerState& t = tracer();
        const std::lock_guard lock{t.mutex};
        if (t.startupFile != nullptr) {
            (void)std::fputs(line, t.startupFile);
            std::fflush(t.startupFile);
            (void)::fsync(::fileno(t.startupFile));
        }
    }
    // P3.2: espelho público DEPOIS de persistir (fora do lock do tracer) —
    // a cópia acessível ao usuário reflete o estágio que ACABOU de ser
    // gravado, antes do próximo começar. Se o processo morrer em seguida,
    // o último estágio concluído permanece visível em Downloads/GONI.
    notifyMirror();
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "GONI", "[STARTUP] %s %s %s",
                        stage, st, dt);
#endif
    ENG_INFO("[STARTUP] {} {} {}", stage, st, dt);
}

const char* lastStage() noexcept {
    return tracer().lastStage;
}

void setMirrorCallback(MirrorCallback callback, void* userdata) {
    g_mirrorCallback = callback;
    g_mirrorUserdata = callback != nullptr ? userdata : nullptr;
}

void requestMirror() {
    notifyMirror();
}

void installCrashHandler() {
    const TracerState& t = tracer();
    if (!t.initialized) {
        return;  // sem dir não há arquivo — não instala (no-op)
    }
    // O append em goni_crash.log precisa ser viável DE DENTRO do handler:
    // fd pré-aberto, somente write() (sem fopen dentro do signal).
    std::snprintf(g_crashPath, sizeof g_crashPath, "%s", t.crashPath);
    if (g_crashFd < 0) {
        g_crashFd = ::open(t.crashPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (g_crashFd < 0) {
            ENG_WARN("diag: crash log não abriu ({})", t.crashPath);
            return;
        }
        char note[128];
        const long now = static_cast<long>(::time(nullptr));
        std::snprintf(note, sizeof note,
                      "[handler] crash handler instalado time=%ld\n", now);
        appendFileLine(t.crashPath, note);
    }
    // (RE)instalação INCONDICIONAL dos sigaction: qualquer terceiro pode
    // ter sobrescrito os handlers desde o último install (frameworks de
    // teste, ART, libs gráficas). Chamar de novo é barato e restaura a
    // cadeia — o guard de idempotência protege apenas o fd/nota.
    struct sigaction sa{};
    sa.sa_sigaction = &crashHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int i = 0; i < kHandledCount; ++i) {
        if (::sigaction(kHandledSignals[i], &sa,
                        &g_handlers[i].previous) == 0) {
            g_handlers[i].installed = true;
        }
    }
}

const char* startupLogPath() noexcept {
    const TracerState& t = tracer();
    return t.initialized ? t.startupPath : "-";
}

const char* crashLogPath() noexcept {
    const TracerState& t = tracer();
    return t.initialized ? t.crashPath : "-";
}

bool describeAddress(std::uintptr_t address, char* out, std::size_t cap) {
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = '\0';
    loadMapsSnapshot();
    const MapEntry* m = findMapFor(address);
    if (m == nullptr) {
        return false;
    }
    std::snprintf(out, cap, "%s+0x%llx", m->path[0] != '\0' ? m->path : "anon",
                  static_cast<unsigned long long>(address - m->start));
    return true;
}

bool hasPreviousCrashReport() {
    const TracerState& t = tracer();
    if (!t.initialized) {
        return false;
    }
    // P3.2: o arquivo contém a nota benigna "[handler] crash handler
    // instalado" desde o primeiro init (P3.1) — tamanho > 0 NÃO significa
    // crash. Um crash real é uma linha "[crash] signal=..." escrita pelo
    // signal handler. Sem este filtro, o export automático e o diálogo
    // "crash anterior" disparariam em TODA execução após a primeira
    // instalação (falso positivo — verificado no emulador).
    std::FILE* f = std::fopen(t.crashPath, "r");
    if (f == nullptr) {
        return false;
    }
    bool found = false;
    char line[256];
    while (std::fgets(line, sizeof line, f) != nullptr) {
        if (std::strncmp(line, "[crash]", 7) == 0) {
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

}  // namespace eng::editor::diag
