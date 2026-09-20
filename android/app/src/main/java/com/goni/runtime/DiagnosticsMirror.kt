package com.goni.runtime

import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * P3.5 — Espelho de diagnósticos FORA DA MAIN THREAD (T1).
 *
 * O PROBLEMA REAL (Realme C33, APK P3.4): o trampoline JNI chamava
 * mirrorFile() NA MAIN THREAD a cada estágio — query + openOutputStream +
 * write no MediaStore (binder + camada Java sobre eMMC lento) são I/O
 * caros e DOCUMENTADOS como causa de UI freezes no Android 11–13. Com
 * ~20 marks no startup, a main thread passava a janela resume→surface
 * inteira dentro do MediaProvider — e um binder travado ali congela o
 * app (hang) até o ART abortar o processo (SIGABRT por timeout interno).
 *
 * CORREÇÃO P3.5:
 *  1. onNativeDiagnosticsChanged() APENAS ENFILEIRA (post no executor
 *     single-background) e retorna em microssegundos — a main thread
 *     nunca mais toca MediaStore no caminho de mark;
 *  2. COALESCE: enquanto um export está em andamento, pedidos novos
 *     colapsam em UM (nunca fila infinita — cada export reescreve o
 *     arquivo COMPLETO, então o último pedido vence);
 *  3. TIMEOUT de 2 s por exportação: um export que não termina em 2 s
 *     é declarado WEDGED — a worker é abandonada (o binder travado fica
 *     preso lá, sem segurar ninguém) e uma nova worker nasce para os
 *     próximos pedidos. Recursos borned: no máx. 1 worker ativa + as
 *     wedged que o kernel eventualmente libera;
 *  4. O ficheiro PRIVADO em filesDir continua síncrono (I/O local
 *     barato — feito pelo C++ com flush+fsync antes do enqueue).
 *
 * Regressões P3.2 preservadas: zero duplicatas (Owner+prefixo + limpeza
 * de sobras), ordem (FIFO do post + coalesce só de pedidos PENDENTES,
 * nunca do que já está a correr), re-publicação de IS_PENDING, cache de
 * Uri com re-resolve.
 *
 * CONTRATO: tudo aqui é best-effort — falha de espelho NUNCA derruba o
 * app (o trampoline JNI já engole exceções; aqui engolimos de novo).
 */
object DiagnosticsMirror {

    private const val TAG = "GONI"
    private const val PUBLIC_DIR = "GONI"
    private const val RELATIVE_PATH = "Download/GONI"
    private const val STARTUP_LOG = "goni_startup.log"
    private const val CRASH_LOG = "goni_crash.log"

    /** P3.5: teto de tempo de UM export antes de declarar wedge. */
    private const val EXPORT_TIMEOUT_MS = 2000L

    private var appContext: Context? = null

    /** Registra o contexto da aplicação (onCreate, antes de tudo). */
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    // --- fila do executor (T1) ------------------------------------------------

    /** Um export lógico por vez; pedidos durante um export colapsam nele
     *  (o export reescreve o arquivo COMPLETO — o último pedido vence). */
    private val pending = AtomicBoolean(false)
    /** Identifica o export atual: tarefas de workers ABANDONADAS (wedged)
     *  viram no-op — nunca tocam as flags do ciclo vivo. */
    private val generation = AtomicLong(0)
    /** Início da parte pesada do export CORRENTE (0 = nenhuma). */
    private val exportStartedAt = AtomicLong(0)
    private val workerLock = Any()

    /** Worker corrente (nasce de novo após um wedge — ver doc da classe). */
    private var worker: HandlerThread? = null
    private var workerHandler: Handler? = null

    private fun uptime(): Long = android.os.SystemClock.uptimeMillis()

    /** Worker viva para novos posts (cria se não existe). */
    private fun ensureWorker(): Handler {
        synchronized(workerLock) {
            if (workerHandler == null) {
                val fresh = HandlerThread("goni-mirror").apply { start() }
                worker = fresh
                workerHandler = Handler(fresh.looper)
            }
            return workerHandler!!
        }
    }

    /** Delega o post à worker; recupera de WEDGE (export > 2 s) criando
     *  uma worker nova e abandonando a antiga (a tarefa dela vira no-op
     *  pela geração — se um dia o binder soltar, ela não interfere). */
    private fun postExport(name: String, awaitMs: Long): Boolean {
        val gen = generation.incrementAndGet()
        val latch = if (awaitMs > 0) CountDownLatch(1) else null
        var posted = false
        try {
            ensureWorker().post {
                // Geração velha (worker abandonada): no-op — os flags do
                // ciclo VIVO pertencem a outra geração.
                if (generation.get() != gen || !pending.get()) {
                    latch?.countDown()
                    return@post
                }
                exportStartedAt.set(uptime())
                try {
                    mirrorFile(name)
                } catch (t: Throwable) {
                    Log.e(TAG, "mirror: export de $name falhou: ${t.message}")
                } finally {
                    if (generation.get() == gen) {
                        exportStartedAt.set(0)
                        pending.set(false)
                    }
                    latch?.countDown()
                }
            }
            posted = true
        } catch (t: Throwable) {
            Log.e(TAG, "mirror: post de $name falhou: ${t.message}")
            if (generation.get() == gen) {
                exportStartedAt.set(0)
                pending.set(false)
            }
            latch?.countDown()
        }
        if (latch != null) {
            try {
                latch.await(awaitMs, TimeUnit.MILLISECONDS)
            } catch (_: InterruptedException) {
                Thread.currentThread().interrupt()
            }
        }
        return posted
    }

    /**
     * Chamado do C++ (thread de despacho do diag → trampoline JNI).
     * APENAS ENFILEIRA e retorna — o export pesado roda na worker
     * background (ver doc da classe). NÃO pode chamar nenhuma função
     * diag/mark (recursão proibida).
     */
    @JvmStatic
    fun onNativeDiagnosticsChanged() {
        enqueue(STARTUP_LOG, awaitMs = 0)
    }

    /** Enfileira um export completo do [name]. [awaitMs] > 0 espera o
     *  resultado com prazo (usado pelo export do crash log da execução
     *  anterior — garantia crash-loop: borned, nunca hang). */
    private fun enqueue(name: String, awaitMs: Long): Boolean {
        var attempts = 0
        while (true) {
            if (pending.compareAndSet(false, true)) {
                return postExport(name, awaitMs)
            }
            // Alguém está exportando: coalesce normal — OU recuperação
            // de wedge (export pesado há MAIS de 2 s sem terminar).
            val started = exportStartedAt.get()
            val wedged = started != 0L && uptime() - started > EXPORT_TIMEOUT_MS
            if (!wedged || ++attempts > 2) {
                return true  // pedido colapsa no export em voo
            }
            // WEDGED: a worker é abandonada (posts dela vêm como no-op —
            // geração), o ciclo lógico é liberado e tentamos uma worker
            // NOVA. A thread antiga segue presa no binder: ninguém a
            // espera (documentado — não há como abortar um binder call).
            Log.w(TAG, "mirror: worker wedged há ${uptime() - started} ms " +
                "— abandonando e criando outra")
            synchronized(workerLock) {
                worker = null
                workerHandler = null
            }
            generation.incrementAndGet()  // desativa as tarefas da worker velha
            pending.set(false)
            // loop: o CAS agora deve ganhar
        }
    }

    /**
     * Exporta o goni_crash.log de execução ANTERIOR para Download/GONI.
     * Chamado no onCreate ANTES de qualquer carga (funciona mesmo que o
     * editor não abra). Enfileira e espera no máx. 2 s — a garantia
     * crash-loop (execução N exporta o crash de N-1 antes de voltar a
     * morrer) é preservada SEM bloqueio indefinido da main. Retorna se
     * havia crash p/ exportar.
     */
    fun exportCrashLogIfPresent(): Boolean {
        val ctx = appContext ?: return false
        if (!File(ctx.filesDir, CRASH_LOG).isFile) {
            return false
        }
        if (File(ctx.filesDir, CRASH_LOG).length() == 0L) {
            return false
        }
        return enqueue(CRASH_LOG, awaitMs = EXPORT_TIMEOUT_MS)
    }

    /**
     * Falha ANTES do native (System.loadLibrary/dlopen): registra a causa
     * com stack completa em Java puro (não depende de libgoni.so) e
     * espelha com espera borned (2 s) — o app está morrendo de qualquer
     * forma; a cópia pública precisa aterrissar ANTES do finish().
     */
    fun recordBootstrapFailure(t: Throwable) {
        val ctx = appContext ?: return
        try {
            val line = "[bootstrap] falha nativa ANTES do diagnóstico: " +
                "${t.javaClass.name}: ${t.message}\n" +
                t.stackTraceToString() + "\n"
            val f = File(ctx.filesDir, STARTUP_LOG)
            f.parentFile?.mkdirs()
            f.appendText(line)
        } catch (e: Exception) {
            Log.e(TAG, "mirror: registro de bootstrap falhou: ${e.message}")
        }
        enqueue(STARTUP_LOG, awaitMs = EXPORT_TIMEOUT_MS)
    }

    /** Copia um arquivo do filesDir para Download/GONI (best-effort).
     *  P3.5: chamado SÓ da worker background. */
    fun mirrorFile(name: String): Boolean {
        val ctx = appContext ?: return false
        val bytes = try {
            val f = File(ctx.filesDir, name)
            if (!f.isFile || f.length() == 0L) {
                return false  // nada p/ copiar (ainda não existe/vazio)
            }
            f.readBytes()
        } catch (e: Exception) {
            Log.e(TAG, "mirror: leitura de $name falhou: ${e.message}")
            return false
        }
        val ok = try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                writeViaMediaStore(ctx, name, bytes)
            } else {
                writeLegacy(ctx, name, bytes)
            }
        } catch (e: Exception) {
            Log.e(TAG, "mirror: escrita de $name falhou: ${e.message}")
            false
        }
        if (ok) {
            Log.i(TAG, "[MIRROR] $name → $RELATIVE_PATH (${bytes.size} B)")
        }
        return ok
    }

    // --- Android 10+ (scoped storage): MediaStore.Downloads ------------------
    //
    // LIÇÕES DO EMULADOR (evidência /tmp/p32_probe + ciclos ATD API 31):
    // 1. MediaStore REESCREVE o nome pelo MIME: "goni_startup.log" +
    //    "text/plain" virava "goni_startup.log.txt". A busca por nome EXATO
    //    nunca achava a própria linha → cada mark INSERIA uma nova → 22
    //    duplicatas "goni_startup.log (N).txt". Correções: MIME
    //    application/octet-stream (sem extensão conhecida → nome mantido)
    //    + busca da linha própria por PREFIXO + cache do Uri no processo.
    // 2. RELATIVE_PATH é gravado NORMALIZADO ("Download/GONI/"): filtrar
    //    por "Download/GONI" sem a barra final NUNCA casa — a linha do
    //    processo anterior não era achada no relaunch e nascia um
    //    "(1).log". Correção: casar por OWNER + prefixo de nome (imune à
    //    normalização); duplicatas próprias são apagadas.
    // 3. Linha IS_PENDING órfã (processo morreu entre insert e publish):
    //    a reescrita também re-publica (IS_PENDING=0) — senão o arquivo
    //    ficaria invisível para sempre.
    // 4. Uri pode ser invalidado (usuário apagou o arquivo em Downloads):
    //    falha de escrita limpa o cache e o próximo mirror re-resolve.

    private val uriCache = HashMap<String, Uri>()

    private fun writeViaMediaStore(ctx: Context, name: String, bytes: ByteArray): Boolean {
        val resolver = ctx.contentResolver
        val collection = MediaStore.Downloads.EXTERNAL_CONTENT_URI

        // 1. Uri em cache (processo vivo): reescreve direto.
        val cached = uriCache[name]
        if (cached != null) {
            if (writeAndPublish(resolver, cached, bytes)) {
                return true
            }
            uriCache.remove(name)  // linha morta — re-resolve abaixo
        }

        // 2. Linhas PRÓPRIAS existentes: mesmo dono + nome com o PREFIXO do
        //    arquivo (o MediaStore pode acrescentar sufixo na renomeação —
        //    ver lição 1). NÃO filtra por RELATIVE_PATH: o MediaStore norma-
        //    liza o valor gravado com barra final ("Download/GONI/") e um
        //    filtro exato nunca casava entre execuções. Owner + prefixo é
        //    imune a normalização de caminho; sobras duplicadas são APAGADAS.
        val staleUris = ArrayList<Uri>()
        val existingUri = resolver.query(
            collection,
            arrayOf(MediaStore.MediaColumns._ID),
            "${MediaStore.MediaColumns.OWNER_PACKAGE_NAME}=? AND " +
                "${MediaStore.MediaColumns.DISPLAY_NAME} LIKE ?",
            arrayOf(ctx.packageName, "$name%"),
            "${MediaStore.MediaColumns.DATE_ADDED} ASC",
        )?.use { c ->
            var first: Uri? = null
            while (c.moveToNext()) {
                val uri = ContentUris.withAppendedId(collection, c.getLong(0))
                if (first == null) {
                    first = uri  // mais antiga = a cópia canônica
                } else {
                    staleUris.add(uri)  // duplicatas p/ limpeza
                }
            }
            first
        }
        // Limpeza: linhas próprias duplicadas são removidas — o usuário vê
        // exatamente UM goni_startup.log/goni_crash.log em Download/GONI.
        for (stale in staleUris) {
            try {
                resolver.delete(stale, null, null)
            } catch (e: Exception) {
                Log.w(TAG, "mirror: duplicata não apagou: ${e.message}")
            }
        }
        if (existingUri != null) {
            if (writeAndPublish(resolver, existingUri, bytes)) {
                uriCache[name] = existingUri
                return true
            }
        }

        // 3. Primeira vez: insert pendente → escreve → publica (visível só
        //    completa — nunca um arquivo parcial no meio de um estágio).
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, name)
            // octet-stream: sem mapeamento de extensão no MediaProvider —
            // o NOME não é reescrito ("goni_startup.log" permanece).
            put(MediaStore.MediaColumns.MIME_TYPE, "application/octet-stream")
            put(MediaStore.MediaColumns.RELATIVE_PATH, RELATIVE_PATH)
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }
        val uri = resolver.insert(collection, values) ?: return false
        val publish = ContentValues().apply {
            put(MediaStore.MediaColumns.IS_PENDING, 0)
        }
        return try {
            if (!writeAll(resolver, uri, bytes)) {
                return false
            }
            resolver.update(uri, publish, null, null)
            uriCache[name] = uri
            true
        } catch (e: Exception) {
            Log.e(TAG, "mirror: insert de $name falhou: ${e.message}")
            false
        }
    }

    /** Escreve o conteúdo completo e garante publicação. */
    private fun writeAndPublish(
        resolver: android.content.ContentResolver,
        uri: Uri,
        bytes: ByteArray,
    ): Boolean {
        return try {
            if (!writeAll(resolver, uri, bytes)) {
                return false
            }
            val publish = ContentValues().apply {
                put(MediaStore.MediaColumns.IS_PENDING, 0)
            }
            resolver.update(uri, publish, null, null)
            true
        } catch (e: Exception) {
            Log.e(TAG, "mirror: rewrite falhou: ${e.message}")
            false
        }
    }

    private fun writeAll(
        resolver: android.content.ContentResolver,
        uri: Uri,
        bytes: ByteArray,
    ): Boolean {
        resolver.openOutputStream(uri, "wt")?.use { out ->
            out.write(bytes)
            out.flush()
        } ?: return false
        return true
    }

    // --- Android 9- (legado): escrita direta --------------------------------

    private fun writeLegacy(ctx: Context, name: String, bytes: ByteArray): Boolean {
        return try {
            val dir = File(
                Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_DOWNLOADS,
                ),
                PUBLIC_DIR,
            )
            if (!dir.exists() && !dir.mkdirs()) {
                return fallbackAppExternal(ctx, name, bytes)
            }
            File(dir, name).writeBytes(bytes)
            true
        } catch (e: Exception) {
            fallbackAppExternal(ctx, name, bytes)
        }
    }

    private fun fallbackAppExternal(ctx: Context, name: String, bytes: ByteArray): Boolean {
        return try {
            val ext = ctx.getExternalFilesDir(null) ?: return false
            val dir = File(ext, PUBLIC_DIR).apply { mkdirs() }
            File(dir, name).writeBytes(bytes)
            true
        } catch (e: Exception) {
            false
        }
    }
}
