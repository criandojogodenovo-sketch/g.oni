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
| 12 | **Build & Export Pipeline** — configuração de build/manifesto determinístico/grafo de dependências+scanner de referências/cook de assets/cache derivado/validação bloqueante/export Android (APK reproducível) e Linux (bundle) | planejada |

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

## FASE 12 — o que herda pronto

- **Identidade estável**: AssetId/ProjectId/SceneEntityId (ADR-028) —
  renomeações/movimentos não quebram referências; nada de path-hash.
- **Envelope binário**: `GONI` + CRC-32 (ADR-030) — contêiner do asset
  cozido e do bundle de runtime.
- **Cena/projeto serializados**: `SceneSerializer` + `project.goni.json`
  (ADRs 032/033) — a fonte do scanner de dependências.
- **Runtime Android pronto**: APK arm64-v8a com engine+editor embutidos
  (FASES 7/8) — o export injeta os dados do projeto em vez de
  recompilá-los no engine.
