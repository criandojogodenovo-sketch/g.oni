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
