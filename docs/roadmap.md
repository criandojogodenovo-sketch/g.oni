# Roadmap

> Fases com contrato técnico formal. Detalhes de escopo nos ADRs e em
> `docs/architecture/00-overview.md`.

| Fase | Escopo | Estado |
|---|---|---|
| 1 | `core` (Result/Error/Span/Version/Uuid128), `math`, `mem`, `log` + build/CI/devcontainer | ✅ concluída (`d9b2d9d`) |
| 2 | `reflect`, `events`, `jobs`, `ecs`, `scene` | ✅ concluída (`8469f7c`) |
| 3 | `fs`, `platform`, `serial`, `assets`, `project` + serialização de Scene/ECS | ✅ concluída (ADRs 026–034) |
| 3.5 | Toolchain de shaders (glslang + spirv-val + SPIRV-Cross) | planejada |
| 4 | renderer abstraction `eng::rhi` — API de alto nível, contrato `RhiBackend`, registro de fábricas, frame lifecycle (ADR-035/036) | **concluída** |
| 5 | backend Vulkan real — loader dlopen, layers, GPU real, staging, swapchain, triangle submetido (ADR-037) | **concluída** |
| 6 | backend OpenGL ES real — EGL surfaceless + pbuffer, GLSL real, triangle com pixel verificado, paridade Vulkan/GLES (ADR-038) | **concluída** |
| 7 | runtime Android — JNI/lifecycle/surface/APK arm64-v8a (ADRs 039–041; runtime TESTADO no Linux, APK BUILT+INSPECTED; emulador/dispositivo UNAVAILABLE) | **concluída** |
| 8 | Native Mobile Editor — `editor/` C++ (documento/inspector reflect/assets/viewport RHI) + `EditorActivity`/`EditorJni` (ADRs 042–044; runtime TESTADO no Linux c/ backends reais, APK BUILT+INSPECTED) | **concluída** |
| 9 | input+ui+audio — `eng::input` canônico/ações, `eng::ui` draw-list/fonte pontilhada, `eng::audio` mixer pull/AAudio dlopen (ADRs 045–047) | **concluída** |
| 10 | eng::physics (rigidbody/collider/trigger/raycast, timestep fixo) + eng::animation (clips/animator/states) + eng::particles (emitter CPU) | planejada |
| 11+ | NI-Script — lexer/parser/AST/tipos/VM/bindings | planejada |

## O que a FASE 5 herda pronto

- **Cache assíncrono**: `AssetManager` é single-threaded por DECLARAÇÃO
  (ADR-034), mas a interface (load/getLoaded/unload, handles shared_ptr)
  foi desenhada para admitir `loadAsync(id, callback)` sem quebra — a
  aresta `assets → events` já está declarada no CMake para os eventos de
  carga; `eng::jobs` entra aí (proibido antes — missão §2.10/R14).
- **Formato de cache**: envelope binário `GONI` (ADR-030) existe, é
  testado e NENHUM pipeline o escreve ainda — é o contêiner do cache
  derivado quando o cook surgir (separação source/cache/runtime, ADR-029).
- **Identidade**: AssetId/SceneEntityId/ProjectId estáveis (ADR-028) —
  renomeações e movimentos já não quebram referências.
- **Migrations**: infraestrutura pronta, nenhuma ativa (ADR-031) — o
  primeiro bump de schema da cena usa `payloadVersion` + cadeia.
