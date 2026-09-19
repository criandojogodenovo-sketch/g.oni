#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "eng/editor/EditorHost.hpp"
#include "eng/editor/TextureCache.hpp"
#include "eng/image/Image.hpp"

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
    // Limite em BYTES MUTF-8 — é o que GetStringUTFRegion ESCREVE.
    // GetStringLength devolve unidades UTF-16 (até 3 bytes/unidade em
    // MUTF-8): usar uma para limitar a outra era overflow de stack
    // (bug C-1 da auditoria final FASES 4–10; strings CJK de nome no
    // editor digitavam além do buffer de 512 bytes).
    const jsize utfLength = env->GetStringUTFLength(value);
    if (utfLength < 0 || static_cast<std::size_t>(utfLength) >= capacity) {
        out[0] = '\0';
        return false;
    }
    env->GetStringUTFRegion(value, 0, utfLength, out);
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
    const jsize utfLength = env->GetStringUTFLength(value);
    if (utfLength <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(utfLength), '\0');
    env->GetStringUTFRegion(value, 0, utfLength, out.data());
    return out;
}

}  // namespace

extern "C" {

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
        return;
    }
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
    auto* browser = host->document().assets();
    if (browser == nullptr) {
        return JNI_FALSE;
    }
    // RECOVERY P0: o nome FINAL (com a extensão preservada do original) é
    // o arquivo que existe de fato — a validação de conteúdo tem de ler
    // EXATAMENTE ele (antes lia `nameBuf` sem extensão e o import de
    // texturas com nome > que a extensão falhava com "arquivo não existe").
    std::string finalName;
    auto imported = browser->import(tempBuf, catBuf, nameBuf, &finalName);
    if (record(handle, imported)) {
        // VALIDAÇÃO de imagem no import (evolução P0-2): textura corrompida
        // é rejeitada AQUI com erro preciso, não no primeiro render.
        if (std::strcmp(catBuf, "textures") == 0) {
            auto bytes = browser->read(catBuf, finalName);
            if (bytes.isError()) {
                return record(handle, bytes) ? JNI_TRUE : JNI_FALSE;
            }
            auto decoded = eng::image::decode(std::span{bytes.value()});
            if (decoded.isError()) {
                // Remove o arquivo importado inválido (não deixa lixo).
                (void)browser->remove(catBuf, finalName);
                return record(handle, decoded) ? JNI_TRUE : JNI_FALSE;
            }
        }
        // Textura (re)importada: o cache pode ter uma versão antiga.
        host->invalidateTextureCache();
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
