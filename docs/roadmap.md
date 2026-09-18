# Roadmap

> Fases com contrato técnico formal. Detalhes de escopo nos ADRs e em
> `docs/architecture/00-overview.md`.

| Fase | Escopo | Estado |
|---|---|---|
| 1 | `core` (Result/Error/Span/Version/Uuid128), `math`, `mem`, `log` + build/CI/devcontainer | ✅ concluída (`d9b2d9d`) |
| 2 | `reflect`, `events`, `jobs`, `ecs`, `scene` | ✅ concluída (`8469f7c`) |
| 3 | `fs`, `platform`, `serial`, `assets`, `project` + serialização de Scene/ECS | ✅ concluída (ADRs 026–034) |
| 4 | renderer abstraction `eng::rhi` — API de alto nível, contrato `RhiBackend`, registro de fábricas, frame lifecycle (ADR-035/036) | ✅ concluída (`b36f350`) |
| 5 | backend Vulkan real — loader dlopen, layers, GPU real, staging, swapchain, triangle submetido (ADR-037) | ✅ concluída (`24d3950`) |
| 6 | backend OpenGL ES real — EGL surfaceless + pbuffer, GLSL real, triangle com pixel verificado, paridade Vulkan/GLES (ADR-038) | ✅ concluída (`35ac978`) |
| 7 | runtime Android — JNI/lifecycle/surface/APK arm64-v8a (ADRs 039–041; runtime TESTADO no Linux, APK BUILT+INSPECTED no CI; emulador/dispositivo UNAVAILABLE) | ✅ concluída (`f1bd4ce`) |
| 8 | Native Mobile Editor — `editor/` C++ (documento/inspector reflect/assets/viewport RHI) + `EditorActivity`/`EditorJni` (ADRs 042–044; runtime TESTADO no Linux c/ backends reais, APK BUILT+INSPECTED) | ✅ concluída (`9a144e4`) |
| 9 | input+ui+audio — `eng::input` canônico/ações, `eng::ui` draw-list/fonte pontilhada, `eng::audio` mixer pull/AAudio dlopen (ADRs 045–047) | ✅ concluída (`969ac2d`) |
| 10 | physics+animation+particles — esfera/AABB+timestep fixo+raycast; clips TRS/cross-fade; emitter CPU determinístico (ADR-048) | ✅ concluída (`f8314d8`) |
| 11 | **NI-Script** — linguagem de script própria: lexer/parser/AST/sema/compiler/bytecode/VM determinística com orçamento/bindings ECS refletidos/`add &BL`/eventos `up`+`emit`/`link to`/semântica formal de `repeat`/`repair`/`timeout` (ADR-049) | ✅ concluída |
| 12 | **Build & Export Pipeline** — build.json (paths relativos)/manifesto determinístico/grafo+scan de referências/cook em envelope GONI (SOURCE/DERIVED)/cache conteúdo-endereçado/validação bloqueante (scripts compilam!)/bundle verificado/export android-arm64 + linux-dev (ADR-050) | ✅ concluída |

A auditoria final independente das FASES 4–10 (com remediação dos bugs
críticos) está em `docs/final_phase4_10_audit.md`.

## FASE 11 — concluída (resumo de evidências)

- **Linguagem**: `.nis` → Lexer → Parser → Sema → Compiler → bytecode →
  NI VM (`engine/niscript`, 8 TU); semântica de `repeat`/`repair`/
  `timeout` FORMALIZADA ANTES (`phase11_audit/design.md §5`) e testada
  regra a regra;
- **Segurança**: nativos fechados em compile-time, orçamento global de
  instruções (loop infinito impossível), handles geracionais (ADR-024
  propagado), SEM nil na linguagem;
- **Bindings**: tabela registrada pelo CONSUMIDOR (editor) reusando o
  catálogo+reflexão (ADR-043/D2) — açúcar `position`/`rotation`(graus)/
  `scale`/`name` + catálogo inteiro por alias canônico/curto;
- **Integração**: `NiScriptComponent` no catálogo ÚNICO; PLAY compila os
  scripts do CLONE (ADR-044), roda `@init`→`up start`→`up update`→
  `up destroy`; script quebrado é desabilitado com log (cena segue);
- **Testes**: 58 casos/516+ asserções (incl. E2E
  `.nis→compile→bytecode→VM→binding→mudança ECS` + determinismo
  byte-a-byte) + 2 casos de play no editor; debug/release 27/27 suites;
  CI Linux+Android verdes (APK BUILT no CI);
- **Ferramentas**: diagnósticos linha/coluna em toda etapa; hook de
  trace; UI de script ADIADA e declarada (`docs/ni-script/08`).

## FASE 12 — concluída (resumo de evidências)

- **Pipeline de DADOS** (`engine/build`, ADR-050): as 10 etapas da missão
  (projeto→config→manifest→grafo→scan→validação→cook→cache→bundle→
  verify/export); paths RELATIVOS obrigatórios; engine × dados separados;
- **Cook + cache**: envelope GONI por asset (reuso ADR-030) com
  classificação SOURCE/DERIVED no formato (v1: tudo SOURCE); cache por
  FNV-1a 64 (cookerVersion ‖ type ‖ conteúdo) — invalidação automática;
- **Validação bloqueante**: fonte ausente, ids duplicados, script que
  não compila (embutido OU standalone), cena JSON inválida, target/
  manifest inválidos; não-usado = WARN listado no report;
- **Determinismo**: manifest e bundle byte-a-byte idênticos entre
  builds limpos (testado); verify em duas camadas antes de exportar;
- **Export**: android-arm64 (prioritário — INSTALL.md com caminho no
  APK existente) + linux-dev (README); loader no runtime = FUTURO
  declarado (honesto);
- **Testes**: 23 casos/232 asserções cobrindo TODA a lista obrigatória
  da missão (vazio/mínimo, ausente/não-usado/duplicado, scripts,
  manifest/target, cache hit/miss/invalidação, clean/incremental,
  E2E export).

---

## Pós-FASE 12 — estado geral

**As 12 fases do roadmap estão CONCLUÍDAS.** Próximos passos são
PLANEJADOS (sem data, sem compromisso de escopo):

- Loader de bundles + browsing de projetos na Activity do runtime
  Android (consumidor do export da FASE 12);
- UI de edição de script no editor (a fonte .nis é editável via
  Inspector hoje — docs/ni-script/08);
- Cooks DERIVED (texturas/malhas) e asset loaders das categorias
  reservadas (ADR-029);
- Serialização de bytecode NI-Script (cache de compilação — ADR-049);
- Skeletal animation, gamepad/mouse reais, audio streaming, LOD.
