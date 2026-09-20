package com.goni.runtime

import android.os.Handler
import android.os.Looper
import android.util.Log
import kotlin.concurrent.thread

/**
 * P3.5 (T3) — Watchdog de hang da main thread (auto-ANR do G.ONI).
 *
 * O PROBLEMA: hang sem ADB é INVISÍVEL. /data/anr e /data/tombstones
 * exigem root que o usuário não tem; sem input pendente o Android nem
 * gera diálogo de ANR — o app congela 1–2 min e o processo morre com
 * SIGABRT (abort interno do ART, ex.: timeout de suspensão de threads)
 * sem NENHUMA evidência acessível.
 *
 * O WATCHDOG substitui o ANR que o dispositivo não entrega:
 *
 *  1. pinger (daemon, 1 s): Handler.post na main → a RUNNABLE chama
 *     nativeWatchdogHeartbeat() — prova que a main processa mensagens;
 *  2. sem resposta no limiar (8 s default; graça de 15 s no startup):
 *     nativeWatchdogEvaluate() → pthread_kill(main, SIGUSR1);
 *  3. o handler nativo roda NA main travada e despeja o contexto
 *     EXATO do travamento em goni_crash.log (pc + pilhas [fp]/[scan] +
 *     TODAS as threads + maps cru) — o processo CONTINUA VIVO.
 *
 * POR QUE Handler.post e não Choreographer: o Choreographer só entrega
 * callbacks enquanto há trabalho de UI agendado (app em background ou
 * UI estática = doFrame NÃO dispara → falso positivo). O looper da main
 * processa mensagens SEMPRE que está saudável — o post de 1 s é o
 * heartbeat mais honesto que existe.
 *
 * Ciclo de vida: start() uma vez por processo (onCreate); nunca para —
 * se o processo for congelado (cached app), o pinger congela junto
 * (nenhum falso positivo). Toda a máquina é best-effort diagnóstico:
 * nada aqui pode derrubar o app.
 */
object Watchdog {

    private const val TAG = "GONI"
    private const val PING_INTERVAL_MS = 1000L

    private val mainHandler = Handler(Looper.getMainLooper())
    @Volatile private var started = false

    fun start() {
        if (started) {
            return
        }
        started = true
        // arm() DEVE rodar na main thread (captura pthread_self nela).
        EditorJni.nativeWatchdogArm()
        thread(name = "goni-watchdog", isDaemon = true) {
            Log.i(TAG, "[WATCHDOG] pinger ativo (ping 1 s, dump após " +
                "8 s sem resposta da main)")
            while (true) {
                try {
                    mainHandler.post {
                        // Prova de vida: a main processou ESTA mensagem.
                        EditorJni.nativeWatchdogHeartbeat()
                    }
                    Thread.sleep(PING_INTERVAL_MS)
                    if (EditorJni.nativeWatchdogEvaluate()) {
                        Log.e(TAG, "[WATCHDOG] main thread sem resposta — " +
                            "dump forense gravado em goni_crash.log")
                    }
                } catch (t: Throwable) {
                    // O pinger nunca pode morrer: é a única testemunha do
                    // hang (best-effort diagnostic — log e segue).
                    Log.w(TAG, "[WATCHDOG] ciclo falhou: ${t.message}")
                }
            }
        }
    }
}
