// =============================================================================
// editor/tests/jni_shim/android/native_window.h — SHIM DE TESTE (P4.5.2).
// Substituto mínimo do header NDK para compilar EditorJni.cpp no Linux.
// Só declara o que o TU usa; corpos inline vazios — nunca executados.
// =============================================================================

#pragma once

#include <jni.h>

struct ANativeWindow;

extern "C" {

/// EditorJni.cpp chama no SurfaceCreated — shim devolve nullptr (não roda).
inline ANativeWindow* ANativeWindow_fromSurface(JNIEnv* /*env*/,
                                                jobject /*surface*/)
{
    return nullptr;
}

/// EditorJni.cpp chama no SurfaceDestroyed — no-op.
inline void ANativeWindow_release(ANativeWindow* /*window*/) {}
}  // extern "C"
