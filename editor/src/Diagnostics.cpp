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

#ifdef __ANDROID__
#include <android/log.h>
#include <ucontext.h>
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
            (void)::write(g_crashFd, line,
                          static_cast<std::size_t>(n));  // best-effort
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
        (void)::write(fd, text, std::strlen(text));
        (void)::fsync(fd);
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
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "GONI", "[STARTUP] %s %s %s",
                        stage, st, dt);
#endif
    ENG_INFO("[STARTUP] {} {} {}", stage, st, dt);
}

const char* lastStage() noexcept {
    return tracer().lastStage;
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

bool hasPreviousCrashReport() {
    const TracerState& t = tracer();
    if (!t.initialized) {
        return false;
    }
    struct stat st{};
    if (::stat(t.crashPath, &st) != 0 || st.st_size <= 0) {
        return false;
    }
    return true;
}

}  // namespace eng::editor::diag
