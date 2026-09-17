#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include "eng/editor/EditorHost.hpp"

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
    const jsize length = env->GetStringLength(value);
    if (length < 0 || static_cast<std::size_t>(length) >= capacity) {
        out[0] = '\0';
        return false;
    }
    env->GetStringUTFRegion(value, 0, length, out);
    out[length] = '\0';
    return true;
}

[[nodiscard]] jstring stringToJni(JNIEnv* env, const std::string& text)
{
    return env->NewStringUTF(text.c_str());
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
    // ANativeWindow_fromSurface ADQUIRE — ownership vai ao host até
    // surfaceDestroyed (ADR-040).
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return;
    }
    host->surfaceCreated(window, eng::rhi::NativeWindowKind::Android, 0, 0);
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
    return record(handle, host->document().newProject(nameBuf)) ? JNI_TRUE
                                                               : JNI_FALSE;
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
    return record(handle, host->document().openProject(eng::fs::Path{pathBuf}))
               ? JNI_TRUE
               : JNI_FALSE;
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
    const auto hit = host->document().viewportTap(x, y);
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
    auto imported = browser->import(tempBuf, catBuf, nameBuf);
    return record(handle, imported) ? JNI_TRUE : JNI_FALSE;
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
    return record(handle, browser->rename(catBuf, nameBuf, newNameBuf))
               ? JNI_TRUE
               : JNI_FALSE;
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
    return record(handle, browser->remove(catBuf, nameBuf)) ? JNI_TRUE
                                                            : JNI_FALSE;
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

}  // extern "C"
