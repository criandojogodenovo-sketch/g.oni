package com.goni.runtime

import android.app.Activity
import android.os.Bundle
import android.view.Choreographer
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager

/**
 * Activity mínima do runtime G.ONI (FASE 7, missão §III).
 *
 * Responsabilidades (SOMENTE): inicialização do runtime nativo, lifecycle,
 * Surface e o loop de frames via Choreographer — que roda na UI thread
 * (decisão ADR-039: a arquitetura de render mais simples correta; o
 * runtime nativo é single-threaded por contrato — ADR-035).
 *
 * NENHUMA lógica do engine aqui (missão §III.4) e nenhum rendering em
 * Kotlin (§III.5) — tudo nativo via [GoniRuntime].
 *
 * Backend por argumento (§XV): `adb shell am start -n
 * com.goni.runtime/.GoniActivity --es backend vulkan` (auto|vulkan|gles).
 */
class GoniActivity : Activity(), SurfaceHolder.Callback2 {

    private var runtimeHandle: Long = 0L
    private var choreographer: Choreographer? = null
    private var surfaceReady = false

    // Var anulável atribuída em onCreate: a lambda lê o callback ATRAVÉS da
    // var (leitura tardia — sem referência à propriedade em construção).
    private var frameCallback: Choreographer.FrameCallback? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val backend = intent?.getStringExtra(EXTRA_BACKEND) ?: BACKEND_AUTO
        runtimeHandle = GoniRuntime.nativeCreate(backend)
        frameCallback = Choreographer.FrameCallback { _ ->
            val handle = runtimeHandle
            if (handle != 0L && surfaceReady) {
                GoniRuntime.nativeRenderFrame(handle)
            }
            choreographer?.postFrameCallback(frameCallback)
        }

        val view = SurfaceView(this)
        view.holder.addCallback(this)
        setContentView(view)
    }

    override fun onResume() {
        super.onResume()
        if (runtimeHandle != 0L) {
            GoniRuntime.nativeOnResume(runtimeHandle)
        }
        choreographer = Choreographer.getInstance().also { it.postFrameCallback(frameCallback) }
    }

    override fun onPause() {
        choreographer?.removeFrameCallback(frameCallback)
        choreographer = null
        if (runtimeHandle != 0L) {
            GoniRuntime.nativeOnPause(runtimeHandle)
        }
        super.onPause()
    }

    override fun onDestroy() {
        if (runtimeHandle != 0L) {
            GoniRuntime.nativeDestroy(runtimeHandle)
            runtimeHandle = 0L
        }
        super.onDestroy()
    }

    // --- SurfaceHolder.Callback (§IV/§V/§VI) --------------------------------

    override fun surfaceCreated(holder: SurfaceHolder) {
        val handle = runtimeHandle
        if (handle != 0L) {
            GoniRuntime.nativeSurfaceCreated(handle, holder.surface)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        val handle = runtimeHandle
        if (handle != 0L) {
            GoniRuntime.nativeSurfaceChanged(handle, width, height)
            surfaceReady = true
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false  // nada é renderizado daqui em diante (§VIII)
        val handle = runtimeHandle
        if (handle != 0L) {
            GoniRuntime.nativeSurfaceDestroyed(handle)
        }
    }

    override fun surfaceRedrawNeeded(holder: SurfaceHolder) {
        // Renderização dirigida por Choreographer — redraw explícito não
        // é necessário (o próximo frame cobre).
    }

    companion object {
        const val EXTRA_BACKEND = "backend"
        const val BACKEND_AUTO = "auto"
    }
}
