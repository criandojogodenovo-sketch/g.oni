package com.goni.runtime

import android.view.Surface

/**
 * Fronteira JNI do G.ONI (FASE 7, missão §II.5) — pequena e explícita.
 *
 * Contrato: `nativeCreate` devolve um handle (Long = endereço do runtime
 * C++); TODAS as demais funções recebem esse handle. Nenhum objeto C++
 * complexo (std::string/vector/referências/tipos RHI) cruza a fronteira
 * (missão §II.6) — strings entram como String e viram bytes no TU JNI.
 *
 * Ciclo de vida dirigido pela [GoniActivity] via SurfaceHolder.Callback;
 * ver GoniJni.cpp para a implementação nativa.
 */
object GoniRuntime {
    init {
        System.loadLibrary("goni")
    }

    /** Cria o runtime nativo. [backend]: "auto" | "vulkan" | "gles" (§XV). */
    external fun nativeCreate(backend: String): Long

    /** Destrói o runtime (renderer + recursos + janela — ADR-040). */
    external fun nativeDestroy(handle: Long)

    /** Surface disponível (o tamanho chega via nativeSurfaceChanged). */
    external fun nativeSurfaceCreated(handle: Long, surface: Surface)

    /** Tamanho/rotação mudaram — renderer criado/redimensionado (§IV). */
    external fun nativeSurfaceChanged(handle: Long, width: Int, height: Int)

    /** Surface destruída — renderer liberado ANTES da janela (ADR-040). */
    external fun nativeSurfaceDestroyed(handle: Long)

    external fun nativeOnPause(handle: Long)
    external fun nativeOnResume(handle: Long)

    /** Troca o backend desejado (recria o renderer se a surface viva — §XV). */
    external fun nativeSetBackend(handle: Long, backend: String)

    /** Renderiza um frame; false = não desenhou (no surface/paused — §VIII). */
    external fun nativeRenderFrame(handle: Long): Boolean
}
