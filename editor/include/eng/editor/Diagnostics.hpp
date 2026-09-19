#ifndef ENG_EDITOR_DIAGNOSTICS_HPP
#define ENG_EDITOR_DIAGNOSTICS_HPP

/// \file Diagnostics.hpp
/// \brief P3.1 — Diagnóstico de startup persistente + crash handler nativo.
///
/// O PROBLEMA REAL (missão P3.1): no Realme C33 o app fecha 2–3s após o
/// splash, SEM logcat disponível. O diagnóstico precisa SOBREVIVER à morte
/// do processo e ser extraível sem depender do usuário.
///
/// Decisões:
/// - Cada estágio é gravado IMEDIATAMENTE (append + fflush + fsync) em
///   app-private storage (filesDir/goni_startup.log) — se o processo
///   morrer, o último estágio concluído permanece no disco;
/// - O mesmo evento é espelhado no logcat ([GONI][STARTUP] ...) para
///   diagnóstico via adb no emulador;
/// - Crash handler NATIVO (SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE) grava
///   goni_crash.log (sinal, endereço, último estágio, backend) e RE-ENTREGA
///   ao handler anterior (debuggerd/ART/tombstone) — o crash NÃO é
///   mascarado; a evidência nativa oficial continua sendo gerada;
/// - Sem init() (testes Linux que não usam diagnóstico): tudo é no-op —
///   zero poluição de diretórios de teste.
///
/// Contrato de thread: init/mark/hasPreviousCrashReport são chamados da
/// UI thread (contrato single-threaded ADR-035); o crash handler lê os
/// globais de estágio best-effort (leitura possivelmente tornada é aceitável
/// em crash).

#include <cstddef>

namespace eng::editor::diag {

/// Inicializa o diagnóstico persistente (dir = filesDir no Android; dir de
/// teste no Linux). Idempotente: a segunda chamada é no-op. Abre
/// <dir>/goni_startup.log em append e instala o crash handler escrevendo
/// <dir>/goni_crash.log.
void init(const char* dir);

/// Marca um estágio de startup. `status`: "ok" | "failed" | "begin".
/// `detail`: informação extra opcional (backend, projeto, erro).
/// Persiste na hora (flush + fsync) e espelha no logcat.
void mark(const char* stage, const char* status = "ok",
          const char* detail = nullptr);

/// P3.2 — callback de espelhamento: invocada após o header de sessão (no
/// init) e após CADA estágio persistido — o detentor deve copiar os logs
/// para uma localização acessível ao usuário (ex.: Downloads/GONI via
/// MediaStore). A cópia pública reflete cada estágio concluído ANTES do
/// próximo começar: se o processo morrer, a evidência do último estágio
/// permanece visível.
///
/// Contratos:
/// - chamada na MESMA thread de mark/init (UI thread — ADR-035);
/// - NUNCA chamada de dentro do crash handler (signal-safety: o handler
///   continua gravando SOMENTE no arquivo privado via write(2) — o export
///   do goni_crash.log acontece na execução SEGUINTE);
/// - o callback NÃO pode chamar mark()/init() (recursão proibida);
/// - exceções não atravessam (função C): o detentor engole tudo.
using MirrorCallback = void (*)(void* userdata);

/// Registra (ou limpa com nullptr) o callback de espelhamento.
/// Sem callback: tudo continua como no P3.1 (arquivo privado + logcat).
void setMirrorCallback(MirrorCallback callback, void* userdata);

/// Dispara o callback registrado AGORA (no-op sem callback). Usado pela
/// Activity para forçar um export e pelos testes Linux.
void requestMirror();

/// Último estágio marcado com sucesso ("-" quando nenhum).
/// É usado pelo crash handler (goni_crash.log) e por dumpState.
const char* lastStage() noexcept;

/// Instala handlers de crash (idempotente). Sem init() prévio: no-op.
/// O handler grava goni_crash.log e encadeia o handler ANTERIOR
/// (tombstone/debuggerd preservados — nada é mascarado).
void installCrashHandler();

/// true se a execução ANTERIOR deixou um crash REAL — linha "[crash]"
/// escrita pelo signal handler em goni_crash.log. A nota benigna
/// "[handler] instalado" NÃO conta (P3.2: falso positivo do P3.1 que
/// faria o export automático e o diálogo dispararem em toda execução).
bool hasPreviousCrashReport();

/// Caminho EFETIVO do log de startup ("-" quando não inicializado).
const char* startupLogPath() noexcept;
/// Caminho EFETIVO do log de crash ("-" quando não inicializado).
const char* crashLogPath() noexcept;

}  // namespace eng::editor::diag

#endif  // ENG_EDITOR_DIAGNOSTICS_HPP
