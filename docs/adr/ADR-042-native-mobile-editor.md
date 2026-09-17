# ADR-042 — Arquitetura do Native Mobile Editor

- **Status:** aceito (FASE 8)
- **Contexto:** missão FASE 8 exige o primeiro editor nativo Android
  (mobile-first, touch), SEM Web/Flutter (§2), reusando as APIs da própria
  engine (§2) e mantendo Kotlin como camada Android e C++ como núcleo.
- **Decisões:**
  1. **Núcleo C++ em `editor/` (consumidor de engine/)** — o estado e os
     comandos do editor vivem em `eng::editor` (EditorDocument/Inspector/
     AssetBrowser/Viewport/ViewportRenderer/EditorHost), compilando no
     Linux (suite Catch2 contra backends reais) e no APK (`libgoni.so`
     via add_subdirectory — fontes JAMAIS duplicadas). A UI Kotlin é
     chrome: nenhuma lógica de engine em `EditorActivity.kt` (§2).
  2. **UI = Views nativos (`android.widget`), programática.** Sem
     Compose/Flutter/Web. Motivação: 3,9 GB RAM e 2 núcleos no ambiente de
     build (risco de build Compose desnecessário), manutenção do "zero
     dependências de terceiros" (ADR-041) e suficiência para o escopo v1
     (listas/botões/diálogos com alvos ≥48dp, §8.8).
  3. **Viewport por RHI com transformação na CPU (v1).** A abstraction
     `eng::rhi` não tem uniforms (decisão FASE 4 — fora do escopo então);
     o editor transforma vértices world→clip em C++ e envia VBO dinâmico
     (`updateBuffer`), reusando o pipeline pos+cor das FASES 5–7
     (shaders embutidos com cópia exata e procedência). Gatilho de
     revisão: quando o RENDER DE JOGO exigir uniforms/materiais, a
     abstraction ganhará o conceito — o editor migra junto.
  4. **Fronteira JNI do editor (EditorJni.cpp — 2º TU com jni.h).** Mesmas
     regras da FASE 7 (§II): handle é o único objeto C++ que cruza;
     strings copiadas localmente; entidades como `jlong` empacotado
     (index+1|generation — valor, não ponteiro); listas como snapshot TSV
     (uma chamada por atualização — zero tipos C++ na fronteira); erros
     consultáveis (`nativeEditorLastError`) e logados no logcat [GONI].
  5. **Gestos do editor NÃO usam eng::input** (que nasce na FASE 9 e é do
     JOGO — §6.4): tap/drag/pinch do viewport vão direto ao documento.
- **Consequências:** o editor herda a garantia de teste do Linux (mesmos
  binários do APK); a UI Kotlin fica substituível sem tocar a engine;
  o custo é a transformação CPU por frame (irrelevante na escala de
  editor) e a duplicação controlada de marshalling (padrão FASE 7).
