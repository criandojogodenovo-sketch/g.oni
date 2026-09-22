// =============================================================================
// editor/tests/jni_shim/jni.h — SHIM DE TESTE (P4.5.2). NUNCA embarcado
// no APK (o Android usa o jni.h real do NDK; este ficheiro só existe para
// compilar EditorJni.cpp DENTRO do executável de testes no Linux).
//
// Porquê: o contrato de vinculação JNI (cada `external fun` de
// EditorJni.kt ⇄ um símbolo C limpo no binário nativo) tem de ser
// verificável SEM JDK/NDK no CI Linux. Este shim declara o subconjunto
// mínimo de tipos/constantes/métodos usados por EditorJni.cpp — as
// funções nunca são CHAMADAS nos testes; o contrato é provado por
// dlsym(RTLD_DEFAULT, "Java_com_goni_runtime_EditorJni_…") com o
// executável linkado com ENABLE_EXPORTS (+rdynamic), o mesmo mecanismo
// de resolução do ART no device.
//
// Regra de fronteira (missão §II.4): jni.h REAL só em GoniJni.cpp/
// EditorJni.cpp — nenhum header eng::* inclui este shim.
// =============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

// --- tipos primitivos (assinaturas idênticas ao jni.h do NDK) ----------------
using jboolean = std::uint8_t;
using jint = std::int32_t;
using jlong = std::int64_t;
using jfloat = float;
using jsize = jint;
using jobject = void*;
using jclass = jobject;
using jstring = jobject;
using jfloatArray = jobject;
using jmethodID = jobject;

// --- constantes usadas pela fronteira ----------------------------------------
#define JNI_TRUE 1
#define JNI_FALSE 0
#define JNI_OK 0
#define JNI_ERR (-1)
#define JNI_EDETACHED (-2)
#define JNI_VERSION_1_6 0x00010006

// JNIEXPORT no NDK = visibility default — o contrato depende disto:
// com ENABLE_EXPORTS, apenas funções marcadas com JNIEXPORT (e com
// extern "C" — nome não manglado) aparecem na tabela dinâmica.
#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL

struct JavaVM;

struct JNIEnv {
    // Subconjunto EXATO usado por EditorJni.cpp (grep 'env->' no TU).
    // Corpos vazios: nunca executados — só satisfazem compilação/link.
    void CallStaticVoidMethod(jclass /*clazz*/, jmethodID /*method*/, ...) {}
    void DeleteGlobalRef(jobject /*obj*/) {}
    void DeleteLocalRef(jobject /*obj*/) {}
    jboolean ExceptionCheck() { return JNI_FALSE; }
    void ExceptionClear() {}
    jclass FindClass(const char* /*name*/) { return nullptr; }
    jint GetJavaVM(JavaVM** /*vm*/) { return JNI_ERR; }
    jmethodID GetStaticMethodID(jclass /*clazz*/, const char* /*name*/,
                                const char* /*sig*/)
    {
        return nullptr;
    }
    jsize GetStringLength(jstring /*str*/) { return 0; }
    jsize GetStringUTFLength(jstring /*str*/) { return 0; }
    void GetStringUTFRegion(jstring /*str*/, jsize /*start*/, jsize /*len*/,
                            char* /*buf*/)
    {
    }
    jfloatArray NewFloatArray(jsize /*len*/) { return nullptr; }
    jobject NewGlobalRef(jobject /*obj*/) { return nullptr; }
    jstring NewStringUTF(const char* /*bytes*/) { return nullptr; }
    void SetFloatArrayRegion(jfloatArray /*array*/, jsize /*start*/,
                             jsize /*len*/, const jfloat* /*buf*/)
    {
    }
};

struct JavaVM {
    // Subconjunto usado pelo trampoline de espelho de diagnóstico.
    jint GetEnv(void** /*penv*/, jint /*version*/) { return JNI_EDETACHED; }
    jint AttachCurrentThread(JNIEnv** /*penv*/, void* /*args*/)
    {
        return JNI_OK;
    }
    jint DetachCurrentThread() { return JNI_OK; }
};
