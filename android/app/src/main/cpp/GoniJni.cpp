/// GoniJni.cpp — a ÚNICA fronteira JNI do G.ONI (FASE 7, missão §II).
///
/// Regras aplicadas (missão §II.5/§II.6):
/// - API mínima espelhando GoniRuntime.kt 1:1;
/// - o handle é o único objeto C++ que cruza a fronteira (Long/intptr_t);
/// - strings Java são copiadas para um buffer local NESTE arquivo — nenhum
///   std::string/std::vector/tipo RHI é exposto;
/// - Surface → ANativeWindow* aqui (android/native_window_jni.h — NDK);
///   o runtime recebe APENAS o ponteiro opaco.
///
/// Sincronização: todas as chamadas chegam da UI thread (Activity +
/// Choreographer — ADR-039); sem trancos necessários (contrato
/// single-threaded do runtime/Renderer — ADR-035).

#include <jni.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <cstdint>
#include <cstring>

#include "eng/android/AndroidRuntime.hpp"

namespace {

using eng::android::AndroidRuntime;

/// Tamanho máximo aceito para o argumento backend (defensivo — a API JNI
/// é fronteira não-confiável por natureza).
constexpr std::size_t kMaxBackendArg = 32;

[[nodiscard]] AndroidRuntime* fromHandle(jlong handle) {
    return reinterpret_cast<AndroidRuntime*>(static_cast<std::uintptr_t>(handle));
}

/// Converte jstring -> buffer local (não expõe nada C++ ao Java).
[[nodiscard]] bool copyBackendString(JNIEnv* env, jstring value, char* out,
                                      std::size_t capacity) {
    if (value == nullptr) {
        out[0] = '\0';
        return true;
    }
    // Limite em BYTES MUTF-8 — é o que GetStringUTFRegion ESCREVE.
    // GetStringLength devolve unidades UTF-16 (até 3 bytes/unidade em
    // MUTF-8): usar uma para limitar a outra era overflow de stack
    // (bug C-1 da auditoria final FASES 4–10).
    const jsize utfLength = env->GetStringUTFLength(value);
    if (utfLength <= 0 || static_cast<std::size_t>(utfLength) >= capacity) {
        out[0] = '\0';
        return false;
    }
    env->GetStringUTFRegion(value, 0, utfLength, out);
    out[utfLength] = '\0';
    return true;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_goni_runtime_GoniRuntime_nativeCreate(JNIEnv* env, jobject /*thiz*/,
                                                jstring backend) {
    char requested[32];
    if (!copyBackendString(env, backend, requested, sizeof(requested))) {
        return 0;  // argumento inválido: runtime não nasce (sem crash)
    }
    auto created = AndroidRuntime::create(requested);
    if (!created) {
        return 0;  // erro já logado pelo runtime (logcat)
    }
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(created.value()));
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeDestroy(JNIEnv* /*env*/, jobject /*thiz*/,
                                                 jlong handle) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime != nullptr) {
        delete runtime;  // renderer→janela, tudo liberado (ADR-040)
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeSurfaceCreated(JNIEnv* env, jobject /*thiz*/,
                                                        jlong handle, jobject surface) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime == nullptr) {
        return;
    }
    // ANativeWindow_fromSurface ADQUIRE a referência. O runtime mantém
    // a SUA própria referência interna (acquireWindow em surfaceCreated),
    // portanto a referência da fronteira JNI é liberada em seguida — sem
    // isso cada ciclo de surface vazava +1 (bug C-2 da auditoria final,
    // invisível aos testes Linux onde acquire/release são no-ops).
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window == nullptr) {
        return;  // surface inválida: runtime permanece NO_SURFACE
    }
    // Tamanho desconhecido neste callback (missão §IV/§VI): o renderer é
    // criado no primeiro nativeSurfaceChanged com o tamanho real.
    runtime->surfaceCreated(window, eng::rhi::NativeWindowKind::Android, 0, 0);
    ANativeWindow_release(window);  // hand-off concluído: o runtime tem a própria
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeSurfaceChanged(JNIEnv* /*env*/, jobject /*thiz*/,
                                                       jlong handle, jint width,
                                                       jint height) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime == nullptr) {
        return;
    }
    runtime->surfaceChanged(static_cast<std::uint32_t>(width),
                            static_cast<std::uint32_t>(height));
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeSurfaceDestroyed(JNIEnv* /*env*/, jobject /*thiz*/,
                                                          jlong handle) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime == nullptr) {
        return;
    }
    // Runtime destrói o renderer ANTES de liberar a janela (ADR-040) — o
    // release acontece dentro do runtime (ownership definiu isso).
    runtime->surfaceDestroyed();
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeOnPause(JNIEnv* /*env*/, jobject /*thiz*/,
                                                jlong handle) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime != nullptr) {
        runtime->onPause();
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeOnResume(JNIEnv* /*env*/, jobject /*thiz*/,
                                                  jlong handle) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime != nullptr) {
        runtime->onResume();
    }
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeSetBackend(JNIEnv* env, jobject /*thiz*/,
                                                   jlong handle, jstring backend) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime == nullptr) {
        return;
    }
    char requested[32];
    if (copyBackendString(env, backend, requested, sizeof(requested))) {
        runtime->setBackend(requested);
    }
}

JNIEXPORT jboolean JNICALL
Java_com_goni_runtime_GoniRuntime_nativeRenderFrame(JNIEnv* /*env*/, jobject /*thiz*/,
                                                     jlong handle) {
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime == nullptr) {
        return JNI_FALSE;
    }
    return runtime->renderFrame() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_goni_runtime_GoniRuntime_nativeOnTouch(JNIEnv* /*env*/,
                                                jobject /*thiz*/, jlong handle,
                                                jint phase, jint pointerId,
                                                jfloat x, jfloat y,
                                                jfloat pressure)
{
    AndroidRuntime* runtime = fromHandle(handle);
    if (runtime != nullptr) {
        runtime->onTouchEvent(static_cast<int>(phase),
                              static_cast<std::uint32_t>(pointerId), x, y,
                              pressure);
    }
}

}  // extern "C"
