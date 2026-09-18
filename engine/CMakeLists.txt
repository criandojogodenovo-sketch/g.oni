# =============================================================================
# engine/ — módulos do motor
# Ordem = ordem de dependência (fundações primeiro). Regra geral de camadas:
# nenhum módulo engine/* pode incluir headers de android/, editor/ ou de
# backends de outros módulos (PARTE 2).
# =============================================================================
add_subdirectory(core)    # Result, Error, Span, Version — sem dependências
add_subdirectory(math)    # Vec/Mat4/Quat/Transform — sem dependências
add_subdirectory(mem)     # Allocator/Heap/Arena — sem dependências
add_subdirectory(log)     # Logger/sinks/format — sem dependências
# FASE 2 (missão §B.0): refletem sobre core; ecs/scene no topo.
add_subdirectory(reflect) # TypeRegistry + ENG_REFLECT — sobre core
add_subdirectory(events)  # EventBus + Subscription RAII — sobre core
add_subdirectory(jobs)    # JobSystem work-stealing — sobre core
add_subdirectory(ecs)     # World + sparse-sets — sobre core/reflect
add_subdirectory(scene)   # Hierarquia de nós + transforms — sobre core/ecs/math
# FASE 3 (missão §5.1): fundações de persistência primeiro (fs), depois as
# camadas que as consomem.
add_subdirectory(fs)      # Path/File/FileSystem — sobre core/log (ADR-027)
add_subdirectory(platform) # Info/Paths/Environment — sobre core/log/fs (ADR-026)
add_subdirectory(serial)   # JSON/envelope/migrations/codec — sobre core/reflect (ADR-030/031)
add_subdirectory(assets)  # AssetId/Registry/Resolver/Manager — sobre core/fs/serial/reflect (ADR-028/029)
add_subdirectory(project) # ProjectId/Config/Paths/File — sobre core/fs/serial/platform (ADR-032)
# Evolução (P0-2): decodificação de imagens — pura sobre core (bytes→RGBA8);
# consumida pelo editor p/ o caminho importar→textura→sprite.
add_subdirectory(image)   # eng::image — decode PNG/JPEG em memória
# FASE 4 (missão §5): renderer abstraction — interface de hardware gráfico.
# Backends reais (backends/vulkan, backends/gles) entram nas FASES 5/6 como
# targets separados linkados no executável final — eng::rhi permanece 100%
# livre de dependências gráficas (ADR-035).
add_subdirectory(rhi)     # Renderer/Frame/RhiBackend — sobre core/log (ADR-035/036)
# FASE 9 (missão §6): sistemas de gameplay interativo — input/ui/audio.
# Nenhum conhece Android/RHI: eventos canônicos na fronteira JNI; UI produz
# draw-list; áudio é mixado por software com backend abstraído (ADR-045..047).
add_subdirectory(input)   # TouchState/InputSystem/Ações — sobre core/math/serial
add_subdirectory(ui)      # Widgets/layout/draw-list — sobre input (eventos)
add_subdirectory(audio)    # Wav/voices/buses/mixer — sobre core/fs/serial
# FASE 10 (missão §7): gameplay físico/visual — sobre ECS, sem RHI/Android.
add_subdirectory(physics)  # RigidBody/Collider/Raycast/timestep fixo
add_subdirectory(animation) # Clips/Animator/estados — sobre math/scene
add_subdirectory(particles) # Emitter/pool CPU determinística — sobre scene
# FASE 11 (ADR-049): NI-Script — linguagem própria compilada para bytecode.
# Núcleo puro sobre core/math/ecs/reflect; bindings de gameplay ficam no
# CONSUMIDOR (editor), igual ao catálogo do SceneSerializer (ADR-043).
add_subdirectory(niscript) # Lexer/Parser/Sema/Compiler + NI VM + bindings
# FASE 12 (ADR-050): Build & Export — pipeline de DADOS (manifest → cook
# GONI → cache determinístico → bundle verificado → export por alvo).
add_subdirectory(build)    # eng::build — sobre project/assets/serial/niscript
