# Roadmap

> Fases com contrato técnico formal. Detalhes de escopo nos ADRs e em
> `docs/architecture/00-overview.md`.

| Fase | Escopo | Estado |
|---|---|---|
| 1 | `core` (Result/Error/Span/Version/Uuid128), `math`, `mem`, `log` + build/CI/devcontainer | ✅ concluída (`d9b2d9d`) |
| 2 | `reflect`, `events`, `jobs`, `ecs`, `scene` | ✅ concluída (`8469f7c`) |
| 3 | `fs`, `platform`, `serial`, `assets`, `project` + serialização de Scene/ECS | ✅ concluída (ADRs 026–034) |
| 3.5 | Toolchain de shaders (glslang + spirv-val + SPIRV-Cross) | planejada |
| 4 | `rhi` + backend Vulkan (validado em dispositivo/emulador) + `assets::loadAsync` (eng::jobs entra) | preparada |
| 5 | Backend GL/GLES | planejada |
| 6 | Física (Jolt), áudio (miniaudio), Android (JNI/APK) | planejada |
| 7+ | Scripting (Lua), runtime, editor web | planejada |

## O que a FASE 4 herda pronto

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
