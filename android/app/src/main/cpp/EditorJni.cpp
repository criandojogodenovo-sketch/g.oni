#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "eng/editor/EditorHost.hpp"
#include "eng/editor/NiRuntime.hpp"   // P4.1: NiScriptStats (stats() do runtime)
#include "eng/editor/TextureCache.hpp"
#include "eng/editor/Diagnostics.hpp"

/// EditorJni.cpp — fronteira JNI do EDITOR (FASE 8, missão §5).
///
/// Mesmas regras do GoniJni.cpp (FASE 7, missão §II):
/// - API 1:1 com EditorJni.kt; o handle é o único objeto C++ que cruza;
/// - strings Java são copiadas para buffers LOCAIS neste TU — nenhum
///   std::string/std::vector/tipo RHI é exposto;
/// - entidades cruzam como jlong (index+1|generation — VALOR, não ponteiro;
///   0 = "nenhuma");
/// - listas atravessam como snapshot TSV em UMA chamada (uma linha por
///   item, campos por \t) — zero arrays C++ atravessando a fronteira;
/// - erros: lastError por handle (consultável pela UI) + logcat [GONI].
///
/// Sincronização: todas as chamadas chegam da UI thread (Choreographer —
/// ADR-039); sem trancos (contrato single-threaded, ADR-035).

namespace {

using eng::editor::EditorDocument;
using eng::editor::EditorHost;

constexpr std::size_t kMaxStringArg = 512;

/// Último erro por handle (UI thread only — contrato acima).
std::unordered_map<jlong, std::string>& lastErrors()
{
    static std::unordered_map<jlong, std::string> errors;
    return errors;
}

void clearError(jlong handle) { lastErrors().erase(handle); }

template <typename ResultT>
bool record(jlong handle, const ResultT& result)
{
    if (result.isError()) {
        lastErrors()[handle] = std::string(result.error().codeName()) + ": " +
                               result.error().message;
        return false;
    }
    clearError(handle);
    return true;
}

[[nodiscard]] EditorHost* fromHandle(jlong handle)
{
    return reinterpret_cast<EditorHost*>(static_cast<std::uintptr_t>(handle));
}

/// jstring → buffer local NUL-terminado (limite defensivo da fronteira).
[[nodiscard]] bool copyJString(JNIEnv* env, jstring value, char* out,
                               std::size_t capacity)
{
    if (value == nullptr) {
        out[0] = '\0';
        return true;
    }
    // P3.1 (bug real achado pela FASE 4): GetStringUTFRegion espera o
    // comprimento em UNIDADES UTF-16 — passar BYTES MUTF-8 ( GetString-
    // UTFLength) lançava StringIndexOutOfBoundsException em qualquer
    // string com acento ("UI construída", projeto "Ação"...). O LIMITE
    // continua em bytes (é o que cabe no buffer); a REGIÃO usa unidades.
    const jsize utfLength = env->GetStringUTFLength(value);  // bytes MUTF-8
    const jsize units = env->GetStringLength(value);          // unidades UTF-16
    if (utfLength < 0 || units < 0 ||
        static_cast<std::size_t>(utfLength) >= capacity) {
        out[0] = '\0';
        return false;
    }
    env->GetStringUTFRegion(value, 0, units, out);
    out[utfLength] = '\0';
    return true;
}

[[nodiscard]] jstring stringToJni(JNIEnv* env, const std::string& text)
{
    return env->NewStringUTF(text.c_str());
}

/// jstring → std::string SEM limite de tamanho (evolução P0-7: fontes
/// .nis têm vários KB — o buffer fixo de 512B do copyJString não serve
/// para CONTEÚDO de script; nomes continuam no caminho limitado).
[[nodiscard]] std::string jniToString(JNIEnv* env, jstring value)
{
    if (value == nullptr) {
        return {};
    }
    const jsize utfLength = env->GetStringUTFLength(value);  // bytes
    const jsize units = env->GetStringLength(value);          // unidades
    if (utfLength <= 0 || units < 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(utfLength), '\0');
    env->GetStringUTFRegion(value, 0, units, out.data());
    return out;
}

// ---------------------------------------------------------------------------
// P3.2 — espelho de diagnóstico em armazenamento acessível ao usuário.
//
// O Realme C33 do usuário não tem run-as/Adb/logcat disponíveis: o próprio
// G.ONI precisa copiar goni_startup.log/goni_crash.log para Download/GONI.
// A via pública de escrita no Android 10+ é o MediaStore (ContentResolver) —
// só existe no lado Java. O C++ notifica o Kotlin a cada estágio persistido
// (diag::setMirrorCallback) e o Kotlin reescreve a cópia pública.
//
// A cópia do goni_crash.log acontece na execução SEGUINTE (o signal
// handler continua gravando apenas no arquivo privado — nada de operações
// complexas em contexto de sinal).
// ---------------------------------------------------------------------------

JavaVM* g_diagVm = nullptr;             ///< VM (attach defensivo no trampoline)
jclass g_diagMirrorClass = nullptr;     ///< global ref: com.goni.runtime.DiagnosticsMirror
jmethodID g_diagMirrorMethod = nullptr; ///< DiagnosticsMirror.onNativeDiagnosticsChanged()V

/// Trampoline C→Java do espelho (P3.5: chamado pela THREAD DE DESPACHO do
/// diag — marks de threads nativas nunca tocam a VM). Contratos
/// (Diagnostics.hpp): SEMPRE na thread de despacho, NUNCA em signal
/// handler, sem propagar exceções.
///
/// Attach: P3.2 fazia attach/detach POR CHAMADA; P3.5 usa um RAII
/// thread_local — a thread de despacho (única chamadora) paga o attach
/// UMA vez e o detach acontece no fim da vida da thread (contrato T0:
/// AttachCurrentThread/DetachCurrentThread por thread).
void diagMirrorTrampoline(void* /*userdata*/)
{
    if (g_diagVm == nullptr || g_diagMirrorClass == nullptr ||
        g_diagMirrorMethod == nullptr) {
        return;
    }
    struct ScopedAttach {
        JNIEnv* env{nullptr};
        bool attached{false};
        explicit ScopedAttach(JavaVM* vm)
        {
            const jint st =
                vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (st == JNI_EDETACHED) {
                attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
            } else if (st != JNI_OK) {
                env = nullptr;
            }
        }
        ~ScopedAttach()
        {
            if (attached) {
                g_diagVm->DetachCurrentThread();
            }
        }
    };
    static thread_local ScopedAttach attach{g_diagVm};
    if (attach.env == nullptr) {
        return;
    }
    attach.env->CallStaticVoidMethod(g_diagMirrorClass, g_diagMirrorMethod);
    // O espelho é best-effort: uma falha de export NUNCA derruba o app.
    if (attach.env->ExceptionCheck()) {
        attach.env->ExceptionClear();
    }
}

/// Registra o trampoline de espelho no diag (uma vez por processo).
/// Chamado ANTES de diag::init p/ que o header de sessão e os dois primeiros
/// marks já sejam copiados publicamente.
void installDiagMirror(JNIEnv* env)
{
    if (g_diagMirrorClass != nullptr) {
        return;  // já registrado (Activity recriada etc.)
    }
    if (env->GetJavaVM(&g_diagVm) != JNI_OK) {
        return;
    }
    const jclass local = env->FindClass("com/goni/runtime/DiagnosticsMirror");
    if (local == nullptr) {
        env->ExceptionClear();
        return;  // classe ausente: sem espelho, diagnóstico privado continua
    }
    g_diagMirrorClass = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (g_diagMirrorClass == nullptr) {
        return;
    }
    g_diagMirrorMethod = env->GetStaticMethodID(
        g_diagMirrorClass, "onNativeDiagnosticsChanged", "()V");
    if (g_diagMirrorMethod == nullptr) {
        env->ExceptionClear();
        env->DeleteGlobalRef(g_diagMirrorClass);
        g_diagMirrorClass = nullptr;
        return;
    }
    eng::editor::diag::setMirrorCallback(&diagMirrorTrampoline, nullptr);
}

}  // namespace

extern "C" {

// =============================================================================
// Diagnóstico de startup P3.1 (FASES 4/5/6) — fronteira mínima
// =============================================================================

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeStartupInit(JNIEnv* env,
                                                  jobject /*thiz*/,
                                                  jstring dir)
{
    // P3.2: espelho registrado ANTES do init — o header de sessão e os
    // dois marks abaixo já disparam a cópia pública (Download/GONI).
    installDiagMirror(env);
    char dirBuf[512];
    if (!copyJString(env, dir, dirBuf, sizeof(dirBuf))) {
        dirBuf[0] = '\0';
    }
    eng::editor::diag::init(dirBuf);
    // Biblioteca carregada = processo vivo até aqui (o arquivo só existe
    // a partir do init — por isso o estágio é marcado DEPOIS de abrir).
    eng::editor::diag::mark("STARTUP_NATIVE_LIBRARY", "ok", "libgoni.so loaded");
    eng::editor::diag::mark("STARTUP_JNI", "ok", dirBuf);
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeStartupMark(JNIEnv* env,
                                                  jobject /*thiz*/,
                                                  jstring stage,
                                                  jstring status,
                                                  jstring detail)
{
    char stageBuf[64];
    char statusBuf[32];
    char detailBuf[256];
    if (!copyJString(env, stage, stageBuf, sizeof(stageBuf))) {
        return;
    }
    if (!copyJString(env, status, statusBuf, sizeof(statusBuf))) {
        statusBuf[0] = '\0';
    }
    if (!copyJString(env, detail, detailBuf, sizeof(detailBuf))) {
        detailBuf[0] = '\0';
    }
    eng::editor::diag::mark(stageBuf,
                            statusBuf[0] == '\0' ? "ok" : statusBuf,
                            detailBuf);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeStartupHasCrashReport(JNIEnv* /*env*/,
                                                            jobject /*thiz*/)
{
    return eng::editor::diag::hasPreviousCrashReport() ? JNI_TRUE : JNI_FALSE;
}

// --- P3.5 (T3): watchdog de hang da main thread -----------------------------
//
// arm() DEVE ser chamado NA main thread (captura pthread_self). O pinger
// (Kotlin, 1 s) faz Handler.post → heartbeat() e depois evaluate(); sem
// resposta no limiar → pthread_kill(main, SIGUSR1) → dump forense
// completo no goni_crash.log (o processo CONTINUA vivo).

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeWatchdogArm(JNIEnv* /*env*/,
                                                  jobject /*thiz*/)
{
    eng::editor::diag::watchdog::arm();
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeWatchdogHeartbeat(JNIEnv* /*env*/,
                                                        jobject /*thiz*/)
{
    eng::editor::diag::watchdog::heartbeat();
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeWatchdogEvaluate(JNIEnv* /*env*/,
                                                       jobject /*thiz*/)
{
    return eng::editor::diag::watchdog::evaluate() ? JNI_TRUE : JNI_FALSE;
}

// =============================================================================
// Host / surface / lifecycle (contrato FASE 7 — ADR-039/040)
// =============================================================================

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorCreate(JNIEnv* env, jobject /*thiz*/,
                                                  jstring backend,
                                                  jstring workspaceRoot)
{
    char backendBuf[32];
    char rootBuf[kMaxStringArg];
    if (!copyJString(env, backend, backendBuf, sizeof(backendBuf)) ||
        !copyJString(env, workspaceRoot, rootBuf, sizeof(rootBuf))) {
        return 0; // argumento inválido: host não nasce (sem crash)
    }
    auto created = EditorHost::create(backendBuf[0] == '\0' ? "auto" : backendBuf,
                                      rootBuf);
    if (created.isError()) {
        return 0; // erro já logado (logcat)
    }
    return static_cast<jlong>(
        reinterpret_cast<std::uintptr_t>(created.value()));
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorDestroy(JNIEnv* /*env*/,
                                                   jobject /*thiz*/,
                                                   jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        clearError(handle);
        delete host;
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSurfaceCreated(JNIEnv* env,
                                                            jobject /*thiz*/,
                                                            jlong handle,
                                                            jobject surface)
{
    // P3.3 — granular: a fronteira exata onde o C33 morre. Cada chamada de
    // sistema daqui em diante fica cercada por um estágio persistido.
    eng::editor::diag::mark("STARTUP_SURFACE", "begin", "jni");
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    // ANativeWindow_fromSurface ADQUIRE. O host mantém a SUA própria
    // referência (acquireWindow em surfaceCreated), portanto a da fronteira
    // JNI é liberada em seguida (bug C-2 da auditoria final — sem isso cada
    // ciclo de surface vazava +1 referência até a morte do processo).
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        eng::editor::diag::mark("STARTUP_SURFACE", "failed",
                                "ANativeWindow_fromSurface = null");
        return;
    }
    eng::editor::diag::mark("STARTUP_SURFACE", "window", "adquirida");
    host->surfaceCreated(window, eng::rhi::NativeWindowKind::Android, 0, 0);
    ANativeWindow_release(window);  // hand-off concluído: o host tem a própria
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSurfaceChanged(JNIEnv* /*env*/,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jint width,
                                                           jint height)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->surfaceChanged(static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height));
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSurfaceDestroyed(JNIEnv* /*env*/,
                                                             jobject /*thiz*/,
                                                             jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->surfaceDestroyed(); // renderer ANTES da janela (ADR-040)
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorOnPause(JNIEnv* /*env*/,
                                                   jobject /*thiz*/,
                                                   jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->onPause();
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorOnResume(JNIEnv* /*env*/,
                                                     jobject /*thiz*/,
                                                     jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->onResume();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorRenderFrame(JNIEnv* /*env*/,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jfloat deltaSeconds)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return host->renderFrame(deltaSeconds) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetBackend(JNIEnv* env, jobject /*thiz*/,
                                                       jlong handle,
                                                       jstring backend)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    char backendBuf[32];
    if (copyJString(env, backend, backendBuf, sizeof(backendBuf))) {
        host->setBackend(backendBuf);
    }
}

// =============================================================================
// Projeto (§8.1)
// =============================================================================

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorNewProject(JNIEnv* env, jobject /*thiz*/,
                                                        jlong handle,
                                                        jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    if (record(handle, host->document().newProject(nameBuf))) {
        // Projeto novo = assets novos: texturas em cache são do projeto anterior.
        host->invalidateTextureCache();
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorOpenProject(JNIEnv* env, jobject /*thiz*/,
                                                        jlong handle,
                                                        jstring relPath)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char pathBuf[kMaxStringArg];
    if (!copyJString(env, relPath, pathBuf, sizeof(pathBuf))) {
        return JNI_FALSE;
    }
    if (record(handle, host->document().openProject(eng::fs::Path{pathBuf}))) {
        // Projeto aberto: texturas do projeto anterior não valem mais.
        host->invalidateTextureCache();
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSaveProject(JNIEnv* /*env*/,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().saveProject()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorProjectName(JNIEnv* env, jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    return stringToJni(env, host->document().projectName());
}

// P4.2 (B-A): NOME DA PASTA real do projeto no disco — settings renomeia
// config.name sem renomear a pasta; export zip e dialogs de cena precisam
// do nome que EXISTE (o config apontava export para pasta inexistente).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorProjectFolder(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr || !host->document().hasProject()) {
        return nullptr;
    }
    return stringToJni(env,
                       host->document().projectRoot().filename().str());
}

// P4.2 (B-A): zip do projeto (C++ — testável no Linux; Kotlin só copia o
// arquivo pronto para o SAF). Entradas embrulhadas na pasta do projeto.
JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorExportProjectZip(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring zipRelPath)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char zipBuf[kMaxStringArg];
    if (!copyJString(env, zipRelPath, zipBuf, sizeof(zipBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().exportProjectZip(zipBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

// P4.2 (B-A): extrai o zip NO workspace (anti-traversal no C++) e devolve
// o nome da pasta criada. A Activity abre o projeto em seguida (open
// explícito — erro volta como toast, nunca cena vazia silenciosa).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorImportProjectZip(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring zipRelPath,
    jstring preferredName)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char zipBuf[kMaxStringArg];
    char prefBuf[kMaxStringArg];
    if (!copyJString(env, zipRelPath, zipBuf, sizeof(zipBuf)) ||
        !copyJString(env, preferredName, prefBuf, sizeof(prefBuf))) {
        return nullptr;
    }
    auto imported = host->document().importProjectZip(zipBuf, prefBuf);
    if (record(handle, imported)) {
        return stringToJni(env, imported.value());
    }
    return nullptr;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorHasProject(JNIEnv* /*env*/,
                                                       jobject /*thiz*/,
                                                       jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return (host != nullptr && host->document().hasProject()) ? JNI_TRUE
                                                             : JNI_FALSE;
}

// --- startup (P3 §0 — bug Android "AlreadyExists") ---------------------------
//
// A POLÍTICA vive no documento C++ (testável no Linux); a Activity chama
// UM ponto. Devolve o nome do projeto criado/aberto, ou null + lastError.

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorEnsureProject(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto ensured = host->ensureStartupProject();
    if (record(handle, ensured)) {
        return stringToJni(env, ensured.value());
    }
    return nullptr;
}

/// Projetos do workspace (TSV de nomes — mesmo filtro do seletor).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorListProjects(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto listed = host->document().listProjects();
    if (record(handle, listed)) {
        std::string tsv;
        for (const auto& name : listed.value()) {
            tsv += name;
            tsv += '\n';
        }
        if (!tsv.empty()) {
            tsv.pop_back();
        }
        return stringToJni(env, tsv);
    }
    return nullptr;
}

/// Estado completo do host no logcat [GONI] (diagnóstico P3 §0).
JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorDumpState(JNIEnv* env,
                                                     jobject /*thiz*/,
                                                     jlong handle,
                                                     jstring origin)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    char originBuf[64];
    if (!copyJString(env, origin, originBuf, sizeof(originBuf))) {
        originBuf[0] = '\0';
    }
    host->dumpState(originBuf);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetProjectName(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().setProjectName(nameBuf)) ? JNI_TRUE
                                                                   : JNI_FALSE;
}

// =============================================================================
// Cena (§8.2)
// =============================================================================

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorNewScene(JNIEnv* /*env*/,
                                                     jobject /*thiz*/,
                                                     jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().newScene()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSaveScene(JNIEnv* env, jobject /*thiz*/,
                                                      jlong handle,
                                                      jstring relPath)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char pathBuf[kMaxStringArg];
    if (!copyJString(env, relPath, pathBuf, sizeof(pathBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().saveScene(pathBuf)) ? JNI_TRUE
                                                              : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLoadScene(JNIEnv* env, jobject /*thiz*/,
                                                      jlong handle,
                                                      jstring relPath)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char pathBuf[kMaxStringArg];
    if (!copyJString(env, relPath, pathBuf, sizeof(pathBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().loadScene(pathBuf)) ? JNI_TRUE
                                                               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSceneDirty(JNIEnv* /*env*/,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return (host != nullptr && host->document().sceneDirty()) ? JNI_TRUE
                                                              : JNI_FALSE;
}

// =============================================================================
// Entidades / hierarquia (§8.2/§8.3)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorHierarchy(JNIEnv* env, jobject /*thiz*/,
                                                      jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& node : host->document().hierarchySnapshot()) {
        char line[640];
        std::snprintf(line, sizeof(line), "%d\t%s\t%llu\n", node.depth,
                      node.name.c_str(),
                      static_cast<unsigned long long>(
                          EditorDocument::packEntity(node.entity)));
        tsv += line;
    }
    if (!tsv.empty()) {
        tsv.pop_back(); // '\n' final ausente
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorCreateEntity(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jstring name,
                                                          jlong parentPacked)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return 0;
    }
    auto created = host->document().createEntity(
        nameBuf, EditorDocument::unpackEntity(
                     static_cast<std::uint64_t>(parentPacked)));
    if (!record(handle, created)) {
        return 0;
    }
    return static_cast<jlong>(EditorDocument::packEntity(created.value()));
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorDeleteEntity(JNIEnv* /*env*/,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().deleteEntity(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed))))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorRenameEntity(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jlong packed,
                                                         jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().renameEntity(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorDuplicateEntity(JNIEnv* /*env*/,
                                                            jobject /*thiz*/,
                                                            jlong handle,
                                                            jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    auto duplicated = host->document().duplicateEntity(
        EditorDocument::unpackEntity(static_cast<std::uint64_t>(packed)));
    if (!record(handle, duplicated)) {
        return 0;
    }
    return static_cast<jlong>(EditorDocument::packEntity(duplicated.value()));
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorReparentEntity(JNIEnv* /*env*/,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jlong packed,
                                                           jlong parentPacked)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().reparentEntity(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(parentPacked))))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jfloatArray JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGetTransform(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto transform = host->document().transform(
        EditorDocument::unpackEntity(static_cast<std::uint64_t>(packed)));
    if (!record(handle, transform)) {
        return nullptr;
    }
    jfloatArray out = env->NewFloatArray(9);
    if (out == nullptr) {
        return nullptr;
    }
    const jfloat values[9] = {
        transform.value().position.x, transform.value().position.y,
        transform.value().position.z, transform.value().rotationDegrees.x,
        transform.value().rotationDegrees.y, transform.value().rotationDegrees.z,
        transform.value().scale.x,    transform.value().scale.y,
        transform.value().scale.z,
    };
    env->SetFloatArrayRegion(out, 0, 9, values);
    return out;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetTransform(JNIEnv* /*env*/,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jlong packed,
                                                          jfloat px, jfloat py,
                                                          jfloat pz, jfloat rx,
                                                          jfloat ry, jfloat rz,
                                                          jfloat sx, jfloat sy,
                                                          jfloat sz)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    eng::editor::TransformDesc desc;
    desc.position = eng::math::Vec3{px, py, pz};
    desc.rotationDegrees = eng::math::Vec3{rx, ry, rz};
    desc.scale = eng::math::Vec3{sx, sy, sz};
    return record(handle, host->document().setTransform(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              desc))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSelection(JNIEnv* /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    const auto selection = host->document().selection();
    return selection.has_value()
               ? static_cast<jlong>(EditorDocument::packEntity(*selection))
               : 0;
}

// =============================================================================
// Seleção direta / ferramentas / gizmo (P1)
// =============================================================================

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSelect(JNIEnv* /*env*/,
                                                   jobject /*thiz*/,
                                                   jlong handle, jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().select(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed))))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSelectionRevision(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr
               ? static_cast<jlong>(host->document().selectionRevision())
               : 0;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetTool(JNIEnv* /*env*/,
                                                    jobject /*thiz*/,
                                                    jlong handle, jint tool)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    using eng::editor::EditorTool;
    switch (tool) {
    case 1: host->document().setTool(EditorTool::Move); break;
    case 2: host->document().setTool(EditorTool::Rotate); break;
    case 3: host->document().setTool(EditorTool::Scale); break;
    default: host->document().setTool(EditorTool::Select); break;
    }
}

/// P4.1 (T1/D3/D4) — densidade do device (dp → px da surface). A
/// Activity instala displayMetrics.density no startup e na troca de
/// configuração; os alvos de toque do gizmo passam a respeitar 48 dp.
JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetUiScale(JNIEnv* /*env*/,
                                                       jobject /*thiz*/,
                                                       jlong handle,
                                                       jfloat scale)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    host->document().viewport().setUiScale(static_cast<float>(scale));
}

/// P4.1 (T2/D5) — estatística do runtime de scripts (VISÍVEL ao autor):
/// TSV "found\tcompiled\tfailed\tinstances\tticks\tfaults\terro\tfault".
/// A UI mostra no Play o que antes só existia no logcat.
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptStats(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    const eng::editor::NiScriptStats& s =
        host->document().runtimeScripts().stats();
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "%u\t%u\t%u\t%u\t%llu\t%u\t%s\t%s",
                  s.scriptsFound, s.scriptsCompiled, s.scriptsFailed,
                  s.instances,
                  static_cast<unsigned long long>(s.ticks), s.faults,
                  s.firstCompileError.c_str(),
                  s.lastFaultMessage.c_str());
    return env->NewStringUTF(buf);
}

/// P4.1 (T3/D6) — estado do áudio para o HUD (honesto): "off" |
/// "running:<backend> <device>" | "null:<motivo>". A UI do Play mostra
/// em vez de ficar calada quando o device recusa o som.
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAudioStatus(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    const std::string status = host->audioStatusLine();
    return env->NewStringUTF(status.c_str());
}

JNIEXPORT jint JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGetTool(JNIEnv* /*env*/,
                                                    jobject /*thiz*/,
                                                    jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    using eng::editor::EditorTool;
    switch (host->document().tool()) {
    case EditorTool::Move: return 1;
    case EditorTool::Rotate: return 2;
    case EditorTool::Scale: return 3;
    default: return 0;
    }
}

JNIEXPORT jint JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGizmoDragBegin(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jfloat x, jfloat y)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    // Bounds do gizmo precisam das dimensões REAIS das texturas (tamanho
    // desenhado — mesmo estado do render/hit-test).
    using eng::editor::GizmoHandle;
    switch (host->document().gizmoDragBegin(x, y, &host->textureCache())) {
    case GizmoHandle::MoveCenter: return 1;
    case GizmoHandle::MoveAxisX: return 2;
    case GizmoHandle::MoveAxisY: return 3;
    case GizmoHandle::RotateRing: return 4;
    case GizmoHandle::ScaleNE: return 5;
    case GizmoHandle::ScaleNW: return 6;
    case GizmoHandle::ScaleSE: return 7;
    case GizmoHandle::ScaleSW: return 8;
    case GizmoHandle::ScaleEdgeE: return 9;   // P4.1 (D4): arestas
    case GizmoHandle::ScaleEdgeW: return 10;
    case GizmoHandle::ScaleEdgeN: return 11;
    case GizmoHandle::ScaleEdgeS: return 12;
    default: return 0;
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGizmoDragTo(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jfloat x, jfloat y)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().gizmoDragTo(x, y)) ? JNI_TRUE
                                                             : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGizmoDragEnd(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().gizmoDragEnd();
    }
}

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorCreateSprite(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return 0;
    }
    auto sprite = host->document().createSprite(nameBuf);
    if (!record(handle, sprite)) {
        return 0;
    }
    return static_cast<jlong>(EditorDocument::packEntity(sprite.value()));
}

// =============================================================================
// Componentes / Inspector (§8.4)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorComponentCatalog(JNIEnv* env,
                                                              jobject /*thiz*/,
                                                              jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& name : eng::editor::Inspector::catalog()) {
        tsv += name;
        tsv += '\t';
        tsv += eng::editor::Inspector::isRemovable(name) ? "1" : "0";
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorEntityComponents(JNIEnv* env,
                                                              jobject /*thiz*/,
                                                              jlong handle,
                                                              jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    const auto entity =
        EditorDocument::unpackEntity(static_cast<std::uint64_t>(packed));
    const auto* scene = host->document().sceneInFocus();
    if (scene == nullptr) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& name : eng::editor::Inspector::componentsOf(*scene, entity)) {
        tsv += name;
        tsv += '\t';
        tsv += eng::editor::Inspector::isRemovable(name) ? "1" : "0";
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorComponentFields(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jlong packed,
                                                             jstring component)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char compBuf[kMaxStringArg];
    if (!copyJString(env, component, compBuf, sizeof(compBuf))) {
        return nullptr;
    }
    const auto entity =
        EditorDocument::unpackEntity(static_cast<std::uint64_t>(packed));
    std::string tsv;
    for (const auto& field : host->document().inspectorFields(entity, compBuf)) {
        tsv += field.path;
        tsv += '\t';
        tsv += field.typeName;
        tsv += '\t';
        tsv += field.value;
        tsv += '\t';
        // Kind + options (evolução P0-6, ADR-052): o host Kotlin renderiza
        // Switch/dropdown/color-picker/textura em vez de EditText livre.
        tsv += field.kind.empty() ? "text" : field.kind;
        tsv += '\t';
        tsv += field.options;
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetComponentField(JNIEnv* env,
                                                               jobject /*thiz*/,
                                                               jlong handle,
                                                               jlong packed,
                                                               jstring component,
                                                               jstring fieldPath,
                                                               jstring value)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char compBuf[kMaxStringArg];
    char pathBuf[kMaxStringArg];
    char valueBuf[kMaxStringArg];
    if (!copyJString(env, component, compBuf, sizeof(compBuf)) ||
        !copyJString(env, fieldPath, pathBuf, sizeof(pathBuf)) ||
        !copyJString(env, value, valueBuf, sizeof(valueBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().setInspectorField(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              compBuf, pathBuf, valueBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAddComponent(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jlong packed,
                                                         jstring component)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char compBuf[kMaxStringArg];
    if (!copyJString(env, component, compBuf, sizeof(compBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().addComponent(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              compBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorRemoveComponent(JNIEnv* env,
                                                            jobject /*thiz*/,
                                                            jlong handle,
                                                            jlong packed,
                                                            jstring component)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char compBuf[kMaxStringArg];
    if (!copyJString(env, component, compBuf, sizeof(compBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().removeComponent(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              compBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

// =============================================================================
// Viewport (§8.6) / Play-Stop (§8.7)
// =============================================================================

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorViewportTap(JNIEnv* /*env*/,
                                                        jobject /*thiz*/,
                                                        jlong handle,
                                                        jfloat x, jfloat y)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return 0;
    }
    // RECOVERY P0: o hit-test precisa das dimensões das texturas (tamanho
    // DESENHADO do sprite) — o cache do host resolve com o mesmo estado
    // das texturas renderizadas.
    const auto hit = host->document().viewportTap(x, y, &host->textureCache());
    return hit.has_value()
               ? static_cast<jlong>(EditorDocument::packEntity(*hit))
               : 0;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorViewportPan(JNIEnv* /*env*/,
                                                       jobject /*thiz*/,
                                                       jlong handle,
                                                       jfloat dx, jfloat dy)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().viewportPan(dx, dy);
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorViewportZoom(JNIEnv* /*env*/,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jfloat factor,
                                                         jfloat focusX,
                                                         jfloat focusY)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().viewportZoom(factor, focusX, focusY);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMoveEntity(JNIEnv* /*env*/,
                                                       jobject /*thiz*/,
                                                       jlong handle,
                                                       jlong packed,
                                                       jfloat dx, jfloat dy)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().moveEntityScreen(
                              EditorDocument::unpackEntity(
                                  static_cast<std::uint64_t>(packed)),
                              dx, dy))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPlay(JNIEnv* /*env*/, jobject /*thiz*/,
                                                 jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().play()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorStop(JNIEnv* /*env*/, jobject /*thiz*/,
                                                 jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().stop();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorIsPlaying(JNIEnv* /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return (host != nullptr && host->document().isPlaying()) ? JNI_TRUE
                                                             : JNI_FALSE;
}

// P4.2 (T5 — Modo Jogo): PAUSE do runtime (tick não avança; render
// continua). Estado vive no documento (fonte única — o HUD reflete).
JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetPaused(JNIEnv* /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle,
                                                      jboolean paused)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().setPaused(paused == JNI_TRUE);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorIsPaused(JNIEnv* /*env*/,
                                                     jobject /*thiz*/,
                                                     jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return (host != nullptr && host->document().isPaused()) ? JNI_TRUE
                                                            : JNI_FALSE;
}

// =============================================================================
// Assets (§8.5)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetCategories(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& category : eng::editor::AssetBrowser::categories()) {
        tsv += category;
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetList(JNIEnv* env,
                                                      jobject /*thiz*/,
                                                      jlong handle,
                                                      jstring category)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char catBuf[kMaxStringArg];
    if (!copyJString(env, category, catBuf, sizeof(catBuf))) {
        return nullptr;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return nullptr;
    }
    auto listed = browser->list(catBuf);
    if (!record(handle, listed)) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& entry : listed.value()) {
        tsv += entry.name;
        tsv += '\t';
        tsv += entry.id;
        tsv += '\t';
        tsv += entry.registered ? "1" : "0";
        tsv += '\t';
        tsv += entry.sourcePath;
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetImport(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jlong handle,
                                                        jstring tempRelPath,
                                                        jstring category,
                                                        jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char tempBuf[kMaxStringArg];
    char catBuf[kMaxStringArg];
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, tempRelPath, tempBuf, sizeof(tempBuf)) ||
        !copyJString(env, category, catBuf, sizeof(catBuf)) ||
        !copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    if (host->document().assets() == nullptr) {
        return JNI_FALSE;
    }
    // P4.2 (B-E): a VALIDAÇÃO DE CONTEÚDO vive no documento agora
    // (EditorDocument::importAsset): texturas (probe de decode) E áudio
    // (probe RIFF/WAVE PCM — o não-WAV era aceito e estourava depois no
    // preview com "ParseError: wav: não é RIFF/WAVE"). Falha de
    // validação remove o arquivo e volta como lastError (toast).
    auto imported = host->document().importAsset(tempBuf, catBuf, nameBuf);
    if (record(handle, imported)) {
        // Textura (re)importada: o cache pode ter uma versão antiga.
        if (std::strcmp(catBuf, "textures") == 0) {
            host->invalidateTextureCache();
        }
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetRename(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jstring category,
                                                         jstring name,
                                                         jstring newName)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char catBuf[kMaxStringArg];
    char nameBuf[kMaxStringArg];
    char newNameBuf[kMaxStringArg];
    if (!copyJString(env, category, catBuf, sizeof(catBuf)) ||
        !copyJString(env, name, nameBuf, sizeof(nameBuf)) ||
        !copyJString(env, newName, newNameBuf, sizeof(newNameBuf))) {
        return JNI_FALSE;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return JNI_FALSE;
    }
    if (record(handle, browser->rename(catBuf, nameBuf, newNameBuf))) {
        host->invalidateTextureCache();
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetDelete(JNIEnv* env,
                                                        jobject /*thiz*/,
                                                        jlong handle,
                                                        jstring category,
                                                        jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char catBuf[kMaxStringArg];
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, category, catBuf, sizeof(catBuf)) ||
        !copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return JNI_FALSE;
    }
    if (record(handle, browser->remove(catBuf, nameBuf))) {
        host->invalidateTextureCache();
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetMove(JNIEnv* env,
                                                      jobject /*thiz*/,
                                                      jlong handle,
                                                      jstring fromCategory,
                                                      jstring name,
                                                      jstring toCategory)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char fromBuf[kMaxStringArg];
    char nameBuf[kMaxStringArg];
    char toBuf[kMaxStringArg];
    if (!copyJString(env, fromCategory, fromBuf, sizeof(fromBuf)) ||
        !copyJString(env, name, nameBuf, sizeof(nameBuf)) ||
        !copyJString(env, toCategory, toBuf, sizeof(toBuf))) {
        return JNI_FALSE;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, browser->move(fromBuf, nameBuf, toBuf)) ? JNI_TRUE
                                                                  : JNI_FALSE;
}

// =============================================================================
// Input do JOGO em Play (FASE 9 §6.1/§6.4 — separado dos gestos do editor)
// =============================================================================

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGameTouch(JNIEnv* /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle, jint phase,
                                                      jint pointerId, jfloat x,
                                                      jfloat y, jfloat pressure)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().gameTouch(static_cast<int>(phase),
                                    static_cast<std::uint32_t>(pointerId), x,
                                    y, pressure);
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetGameViewportSize(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle, jint width, jint height)
{
    // Bug C-4 da auditoria final: sem este sizing, o InputSystem do jogo em
    // Play ficava em 1x1 — zonas de toque (frações da tela) nunca
    // disparavam no dispositivo. O runtime demo (GoniActivity) já o fazia.
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().setGameViewportSize(static_cast<float>(width),
                                              static_cast<float>(height));
    }
}

// =============================================================================
// Erro da última operação (diálogos/toasts da UI)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLastError(JNIEnv* env, jobject /*thiz*/,
                                                      jlong handle)
{
    const auto& errors = lastErrors();
    const auto it = errors.find(handle);
    if (it == errors.end()) {
        return nullptr;
    }
    return stringToJni(env, it->second);
}

// =============================================================================
// Imagens/texturas (evolução P0 — metadados p/ o Asset Browser e picker)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAssetImageInfo(JNIEnv* env,
                                                            jobject /*thiz*/,
                                                            jlong handle,
                                                            jstring category,
                                                            jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char catBuf[kMaxStringArg];
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, category, catBuf, sizeof(catBuf)) ||
        !copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return nullptr;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return nullptr;
    }
    const auto info = host->textureCache().imageInfo(*browser, nameBuf);
    if (!info.valid) {
        return nullptr;  // não é imagem válida (ou decode falhou)
    }
    return stringToJni(env, std::to_string(info.width) + "x" +
                                 std::to_string(info.height) +
                                 (info.alpha ? " rgba" : " rgb"));
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorListTextures(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return nullptr;
    }
    auto listed = browser->list("textures");
    if (listed.isError()) {
        return nullptr;
    }
    std::string tsv{};
    for (const auto& entry : listed.value()) {
        if (!tsv.empty()) {
            tsv.push_back('\n');
        }
        tsv += entry.name;
    }
    return stringToJni(env, tsv);
}

// =============================================================================
// Scripts NI-Script como assets do projeto (evolução P0-7, ADR-053)
// =============================================================================

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptList(JNIEnv* env,
                                                       jobject /*thiz*/,
                                                       jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto listed = host->document().scriptList();
    if (listed.isError()) {
        return stringToJni(env, "");
    }
    std::string tsv;
    for (const auto& name : listed.value()) {
        if (!tsv.empty()) {
            tsv.push_back('\n');
        }
        tsv += name;
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptRead(JNIEnv* env,
                                                       jobject /*thiz*/,
                                                       jlong handle,
                                                       jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return nullptr;
    }
    auto content = host->document().scriptRead(nameBuf);
    if (content.isError()) {
        (void)record(handle, content);
        return nullptr;
    }
    return stringToJni(env, content.value());
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptWrite(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jstring name,
                                                         jstring content)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    // Conteúdo SEM limite de 512B — fontes .nis são multi-KB (P0-7).
    const std::string body = jniToString(env, content);
    return record(handle, host->document().scriptWrite(nameBuf, body))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptCreate(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().scriptCreate(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptDelete(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().scriptDelete(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

/// TSV: linha 1 = "1" (compilou) ou "0" (falhou); linhas seguintes =
/// "line\tcol\tmessage" (todos os diagnósticos coletados).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptCompile(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jstring source)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    const std::string src = jniToString(env, source);
    auto check = host->document().scriptCompile(src);
    if (check.isError()) {
        return stringToJni(env, "0\n1\t1\t" + check.error().message);
    }
    std::string tsv = check.value().ok ? "1" : "0";
    for (const auto& diag : check.value().diags) {
        tsv += '\n';
        tsv += std::to_string(diag.line);
        tsv += '\t';
        tsv += std::to_string(diag.col);
        tsv += '\t';
        tsv += diag.message;
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorScriptAssign(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jlong packed,
                                                          jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle,
                  host->document().scriptAssign(
                      EditorDocument::unpackEntity(
                          static_cast<std::uint64_t>(packed)),
                      nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

// =============================================================================
// P2 — componentes authoráveis + animação + áudio
// =============================================================================

/// TSV: typeName \t dependencyHint (catálogo ADDÁVEL à entidade — sem os
/// que ela já possui e sem os built-ins obrigatórios).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAddableComponents(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jlong packed)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    const auto metas = host->document().addableComponents(
        EditorDocument::unpackEntity(static_cast<std::uint64_t>(packed)));
    std::string tsv;
    for (const auto& meta : metas) {
        tsv += meta.name;
        tsv += '\t';
        tsv += meta.dependency;
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

/// TSV: name \t clip \t duration \t frames \t keys \t loop.
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationList(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto listed = host->document().animationList();
    if (listed.isError()) {
        record(handle, listed);
        return nullptr;
    }
    std::string tsv;
    for (const auto& anim : listed.value()) {
        tsv += anim.name;
        tsv += '\t';
        tsv += anim.clip;
        tsv += '\t';
        tsv += std::to_string(anim.duration);
        tsv += '\t';
        tsv += std::to_string(anim.frames);
        tsv += '\t';
        tsv += std::to_string(anim.keys);
        tsv += '\t';
        tsv += anim.loop ? "1" : "0";
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationRead(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return nullptr;
    }
    auto content = host->document().animationRead(nameBuf);
    if (content.isError()) {
        record(handle, content);
        return nullptr;
    }
    return stringToJni(env, content.value());
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationWrite(JNIEnv* env,
                                                            jobject /*thiz*/,
                                                            jlong handle,
                                                            jstring name,
                                                            jstring json)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    const std::string body = jniToString(env, json);
    return record(handle, host->document().animationWrite(nameBuf, body))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationCreate(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().animationCreate(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationDelete(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().animationDelete(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationAssign(JNIEnv* env,
                                                             jobject /*thiz*/,
                                                             jlong handle,
                                                             jlong packed,
                                                             jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle,
                  host->document().animationAssign(
                      EditorDocument::unpackEntity(
                          static_cast<std::uint64_t>(packed)),
                      nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

/** Tempo do frame adicionado; -1 em falha (lastError tem a causa). */
JNIEXPORT jfloat JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationAddFrame(JNIEnv* env,
                                                               jobject /*thiz*/,
                                                               jlong handle,
                                                               jstring name,
                                                               jstring texture)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return -1.f;
    }
    char nameBuf[kMaxStringArg];
    char texBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf)) ||
        !copyJString(env, texture, texBuf, sizeof(texBuf))) {
        return -1.f;
    }
    auto when = host->document().animationAddFrame(nameBuf, texBuf);
    if (when.isError()) {
        record(handle, when);
        return -1.f;
    }
    return when.value();
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAnimationSetMeta(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring name,
    jboolean loop, jfloat fps)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle,
                  host->document().animationSetMeta(
                      nameBuf, loop == JNI_TRUE, static_cast<float>(fps)))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPreviewStart(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jlong packed,
                                                         jstring clip)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char clipBuf[kMaxStringArg];
    if (!copyJString(env, clip, clipBuf, sizeof(clipBuf))) {
        return JNI_FALSE;
    }
    return record(handle,
                  host->document().previewStart(
                      EditorDocument::unpackEntity(
                          static_cast<std::uint64_t>(packed)),
                      clipBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPreviewStop(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().previewStop();
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPreviewing(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().previewing() ? JNI_TRUE
                                                            : JNI_FALSE;
}

/// Toca um asset WAV AGORA (preview manual — Edit incluso).
JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAudioPreview(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().audioPreview(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

/// P4.3 (N1): para o preview de áudio (toggle da UI — idempotente).
JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAudioPreviewStop(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return;
    }
    host->document().audioPreviewStop();
}

/// P4.3 (N1): há voice de preview VIVA? (fonte de verdade do botão).
JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorAudioPreviewPlaying(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().audioPreviewPlaying()
               ? JNI_TRUE
               : JNI_FALSE;
}

// --- P4.3 (Bloco 2): Ticks/Camadas — ADR-051 autorável ---------------------

/// Camadas em TSV: name\ttimescale\tupdate\tphysics\trender (ordem da registry).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLayerList(JNIEnv* env,
                                                      jobject /*thiz*/,
                                                      jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto layers = host->document().layerList();
    if (layers.isError()) {
        record(handle, layers);
        return nullptr;
    }
    std::string tsv;
    char line[512];
    for (const auto& layer : layers.value()) {
        std::snprintf(line, sizeof(line), "%s\t%.6g\t%d\t%d\t%d\n",
                      layer.name.c_str(),
                      static_cast<double>(layer.timeScale),
                      layer.update ? 1 : 0, layer.physics ? 1 : 0,
                      layer.render ? 1 : 0);
        tsv += line;
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLayerAdd(JNIEnv* env,
                                                     jobject /*thiz*/,
                                                     jlong handle,
                                                     jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().addLayer(nameBuf)) ? JNI_TRUE
                                                              : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLayerSetTimeScale(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring name, jfloat ts)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().setLayerTimeScale(nameBuf, ts))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorLayerSetParticipation(
    JNIEnv* env, jobject /*thiz*/, jlong handle, jstring name, jboolean update,
    jboolean physics, jboolean render)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle,
                  host->document().setLayerParticipation(
                      nameBuf, update == JNI_TRUE, physics == JNI_TRUE,
                      render == JNI_TRUE))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPhysicsDt(JNIEnv* /*env*/,
                                                      jobject /*thiz*/,
                                                      jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr ? host->document().physicsFixedDt() : 0.f;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorPhysicsSetDt(JNIEnv* /*env*/,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jfloat dt)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return record(handle, host->document().setPhysicsFixedDt(dt)) ? JNI_TRUE
                                                                  : JNI_FALSE;
}

/// Nomes dos assets de ÁUDIO (linhas \n) — picker do Inspector (kind audio).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorListAudio(JNIEnv* env,
                                                       jobject /*thiz*/,
                                                       jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return stringToJni(env, "");
    }
    auto listed = browser->list("audio");
    if (listed.isError()) {
        record(handle, listed);
        return stringToJni(env, "");
    }
    std::string lines;
    for (const auto& entry : listed.value()) {
        lines += entry.name;
        lines += '\n';
    }
    if (!lines.empty()) {
        lines.pop_back();
    }
    return stringToJni(env, lines);
}

// --- P3 §3: materiais (assets/materials/<nome>.mat.json) ----------------------

/// TSV: name \t shader \t tintR \t tintG \t tintB \t tintA.
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMaterialList(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto listed = host->document().materialList();
    if (!record(handle, listed)) {
        return nullptr;
    }
    std::string tsv;
    for (const auto& summary : listed.value()) {
        tsv += summary.name;
        tsv += '\t';
        tsv += summary.shader;
        tsv += '\t';
        tsv += std::to_string(summary.tintR);
        tsv += '\t';
        tsv += std::to_string(summary.tintG);
        tsv += '\t';
        tsv += std::to_string(summary.tintB);
        tsv += '\t';
        tsv += std::to_string(summary.tintA);
        tsv += '\n';
    }
    if (!tsv.empty()) {
        tsv.pop_back();
    }
    return stringToJni(env, tsv);
}

/// Conteúdo cru do material (JSON).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMaterialRead(JNIEnv* env,
                                                         jobject /*thiz*/,
                                                         jlong handle,
                                                         jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return nullptr;
    }
    auto content = host->document().materialRead(nameBuf);
    if (!record(handle, content)) {
        return nullptr;
    }
    return stringToJni(env, content.value());
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMaterialWrite(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle,
                                                          jstring name,
                                                          jstring json)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    // Conteúdo JSON pode ter KBs — via jniToString (sem buffer fixo).
    const std::string jsonText = jniToString(env, json);
    return record(handle, host->document().materialWrite(nameBuf, jsonText))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMaterialCreate(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().materialCreate(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorMaterialDelete(JNIEnv* env,
                                                           jobject /*thiz*/,
                                                           jlong handle,
                                                           jstring name)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    char nameBuf[kMaxStringArg];
    if (!copyJString(env, name, nameBuf, sizeof(nameBuf))) {
        return JNI_FALSE;
    }
    return record(handle, host->document().materialDelete(nameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
}

/// Nomes dos materiais (linhas \n) — picker do Inspector (kind material).
JNIEXPORT jstring JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorListMaterials(JNIEnv* env,
                                                          jobject /*thiz*/,
                                                          jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return nullptr;
    }
    auto names = host->document().materialNames();
    if (!record(handle, names)) {
        return nullptr;
    }
    std::string lines;
    for (const auto& name : names.value()) {
        lines += name;
        lines += '\n';
    }
    if (!lines.empty()) {
        lines.pop_back();
    }
    return stringToJni(env, lines);
}

}  // extern "C"

// --- P4.5: snap do gizmo / fit do viewport / undo-redo ---------------------

JNIEXPORT void JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorSetSnap(JNIEnv* /*env*/,
                                                    jobject /*thiz*/,
                                                    jlong handle,
                                                    jboolean translate,
                                                    jboolean rotate)
{
    EditorHost* host = fromHandle(handle);
    if (host != nullptr) {
        host->document().setSnapTranslate(translate == JNI_TRUE);
        host->document().setSnapRotate(rotate == JNI_TRUE);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGetSnapTranslate(JNIEnv* /*env*/,
                                                             jobject /*thiz*/,
                                                             jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().snapTranslate() ? JNI_TRUE
                                                               : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorGetSnapRotate(JNIEnv* /*env*/,
                                                          jobject /*thiz*/,
                                                          jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().snapRotate() ? JNI_TRUE
                                                            : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorViewportFit(JNIEnv* /*env*/,
                                                        jobject /*thiz*/,
                                                        jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    host->document().viewportFit(&host->textureCache());
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorCanUndo(JNIEnv* /*env*/,
                                                    jobject /*thiz*/,
                                                    jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().canUndo() ? JNI_TRUE
                                                         : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorCanRedo(JNIEnv* /*env*/,
                                                    jobject /*thiz*/,
                                                    jlong handle)
{
    EditorHost* host = fromHandle(handle);
    return host != nullptr && host->document().canRedo() ? JNI_TRUE
                                                         : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorUndo(JNIEnv* /*env*/,
                                                 jobject /*thiz*/,
                                                 jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return host->document().undo().ok() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_EditorJni_nativeEditorRedo(JNIEnv* /*env*/,
                                                 jobject /*thiz*/,
                                                 jlong handle)
{
    EditorHost* host = fromHandle(handle);
    if (host == nullptr) {
        return JNI_FALSE;
    }
    return host->document().redo().ok() ? JNI_TRUE : JNI_FALSE;
}
