# =============================================================================
# engine/ — módulos do motor (FASE 1, §17.1)
# Ordem = ordem de dependência (fundações primeiro). Regra geral de camadas:
# nenhum módulo engine/* pode incluir headers de android/, editor/ ou de
# backends de outros módulos (PARTE 2).
# =============================================================================
add_subdirectory(core) # Result, Span, Version — sem dependências
add_subdirectory(math) # Vec/Mat4/Quat/Transform — sem dependências
add_subdirectory(mem)  # Allocator/Heap/Arena — sem dependências
add_subdirectory(log)  # Logger/sinks/format — sem dependências
