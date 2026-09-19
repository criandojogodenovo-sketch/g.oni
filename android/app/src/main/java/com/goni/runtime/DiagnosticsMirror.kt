package com.goni.runtime

import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import java.io.File

/**
 * P3.2 — Espelho AUTOMÁTICO dos diagnósticos para armazenamento acessível.
 *
 * O PROBLEMA REAL (Realme C33): o app fecha sozinho após o splash e o
 * dispositivo NÃO expõe run-as / logcat / ADB — os arquivos privados
 * (filesDir/goni_startup.log, filesDir/goni_crash.log) ficam presos em
 * /data/data sem como o usuário lê-los. Este objeto copia os diagnósticos
 * para Download/GONI/, acessível pelo gerenciador de arquivos do sistema
 * e via cabo USB (MTP).
 *
 * ESTRATÉGIA (sem operações complexas em signal handler):
 * 1. `onNativeDiagnosticsChanged()` é chamado pelo C++ (via trampoline
 *    JNI) DEPOIS de cada estágio persistido — a cópia pública em
 *    Download/GONI/goni_startup.log é reescrita com o conteúdo completo do
 *    arquivo privado. Resultado: o último estágio concluído ANTES de uma
 *    morte súbita permanece visível publicamente.
 * 2. O crash handler nativo (P3.1) continua gravando goni_crash.log
 *    apenas no armazenamento privado (write(2) — signal-safe). A cópia
 *    pública do crash log acontece NO PRÓXIMO INÍCIO, imediatamente no
 *    onCreate, antes de qualquer carga pesada (exportCrashLogIfPresent).
 *    Em um crash-loop, a execução N sempre exporta o crash da execução
 *    N-1 ANTES de voltar a morrer.
 * 3. Se nem a biblioteca nativa carregar (dlopen/UnsatisfiedLinkError),
 *    `recordBootstrapFailure` registra a causa em Java puro e espelha.
 *
 * PERMISSÕES: Android 10+ (API 29+) usa MediaStore.Downloads — o app
 * contribui com os PRÓPRIOS arquivos sem NENHUMA permissão de
 * armazenamento (scoped storage compatível com Android 12/13).
 * Android 9- (legado): escrita direta em Environment.DIRECTORY_DOWNLOADS
 * (requer WRITE_EXTERNAL_STORAGE, declarada com maxSdkVersion=28); sem a
 * permissão, cai no app-external (Android/data/... ainda navegável).
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

    private var appContext: Context? = null

    /** Registra o contexto da aplicação (onCreate, antes de tudo). */
    fun init(context: Context) {
        appContext = context.applicationContext
    }

    /**
     * Chamado do C++ (EditorJni.cpp → trampoline) a cada estágio persistido.
     * Reescreve a cópia pública completa do log de startup.
     * NÃO pode chamar nenhuma função diag/mark (recursão proibida — o
     * trampoline dispararia de novo).
     */
    @JvmStatic
    fun onNativeDiagnosticsChanged() {
        mirrorFile(STARTUP_LOG)
    }

    /**
     * Exporta o goni_crash.log de execução ANTERIOR para Download/GONI.
     * Chamado no onCreate ANTES de qualquer carga (funciona mesmo que o
     * editor não abra). Retorna se havia crash p/ exportar.
     */
    fun exportCrashLogIfPresent(): Boolean {
        val ctx = appContext ?: return false
        if (!File(ctx.filesDir, CRASH_LOG).isFile) {
            return false
        }
        if (File(ctx.filesDir, CRASH_LOG).length() == 0L) {
            return false
        }
        return mirrorFile(CRASH_LOG)
    }

    /**
     * Falha ANTES do native (System.loadLibrary/dlopen): registra a causa
     * com stack completa em Java puro (não depende de libgoni.so) e
     * espelha imediatamente. Cobertura da janela pré-diagnóstico.
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
        mirrorFile(STARTUP_LOG)
    }

    /** Copia um arquivo do filesDir para Download/GONI (best-effort). */
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
        //    filtro exato nunca casava entre execuções — a prova: a linha
        //    criada pelo processo anterior não era achada no relaunch e um
        //    "(1).log" duplicado nascia. Owner + prefixo é imune a
        //    normalização de caminho; sobras duplicadas são APAGADAS.
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
        // Limpeza: linhas próprias duplicadas (de execuções anteriores ao
        // fix ou renomeações do MediaStore) são removidas — o usuário vê
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

    /** Escreve o conteúdo completo e garante publicação (linha pendente
     *  herdada de uma execução morta no meio do insert fica visível). */
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
            // Sem WRITE_EXTERNAL_STORAGE concedida: o melhor destino
            // acessível restante é o app-external (gerenciador de arquivos
            // navega em Android/data sem permissão).
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
