# Build — Comandos Canônicos (FASE 1)

## Pré-requisitos

| Ferramenta | Versão mínima | Uso |
|---|---|---|
| CMake | 3.28 | build system (ADR-003) |
| Ninja | 1.11 | gerador obrigatório (ADR-003) |
| GCC | 13 | C++20 completo (ADR-001) |
| Git | 2.40 | clone/commits |
| Rede | — | FetchContent baixa Catch2 v3.5.2 na 1ª configuração |

Alternativa sem pré-instalação: abrir o repositório em um **devcontainer**
(CodeSpace) — o `postCreateCommand` provisiona tudo (§12) e `verify.sh`
valida as versões mínimas.

## Presets (CMakePresets.json, schema v6)

| Preset | Tipo | Flags relevantes |
|---|---|---|
| `linux-debug` | Debug | `-O0 -g`, ASan+UBSan, LSan, `-Werror` |
| `linux-release` | Release | `-O3 -DNDEBUG`, LTO, `-Werror` |

Opções de cache úteis:

| Opção | Padrão | Efeito |
|---|---|---|
| `ENG_BUILD_TESTS` | `ON` | constrói os testes Catch2 |
| `ENG_ENABLE_WERROR` | `ON` | warnings como erros (ADR-020) |
| `ENG_ENABLE_SANITIZERS` | `OFF` (`ON` no debug) | ASan+UBSan não-recuperáveis |
| `ENG_ENABLE_LTO` | `OFF` (`ON` no release) | otimização em tempo de link |
| `ENG_MATH_VULKAN_DEPTH` | `OFF` | profundidade [0,1] (Vulkan) em `eng::math` |

## Comandos canônicos

```bash
# Linux (desenvolvimento)
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure

# Linux (release/LTO)
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

Os binários ficam em `build/<preset>/` (ignorado pelo git), com
`compile_commands.json` no build de debug para uso por clangd/IDE.

## Testes

Quatro executáveis, um por módulo, registrados no CTest:

| Teste | Módulo coberto |
|---|---|
| `core` | Result/Error/Span/Version |
| `math` | Vec2/3/4, Mat4, Quat, Transform |
| `mem` | HeapAllocator, ArenaAllocator |
| `log` | Format, Logger, ConsoleSink, macros |

Rodar um módulo isolado com saída verbosa:

```bash
./build/linux-debug/engine/math/eng_math_tests -s
```

No preset de debug, o CTest define `ASAN_OPTIONS=detect_leaks=1` e
`UBSAN_OPTIONS=halt_on_error=1` — qualquer UB ou vazamento falha o build.

## Notas técnicas

- **Catch2 v3.5.2** é obtido via `FetchContent` (pinado por tag — PARTE 3) na
  primeira configuração e cacheado em `build/<preset>/_deps`. Builds offline
  requerem `ENG_BUILD_TESTS=OFF` ou um cache pré-populado.
- Bibliotecas do motor compilam com `-fno-exceptions -fno-rtti`
  (ADR-004/005); testes compilam com exceções (Catch2) e sem RTTI — ver
  `docs/architecture/00-overview.md` para a justificativa completa.

## Hardware tests de gráficos (FASE 5+)

Testes do backend Vulkan (`rhi_vulkan`, labels CTest `rhi_hardware`)
executam GPU REAL quando há loader + ICD; caso contrário SKIPAM com motivo
— nunca falham por ausência de GPU:

```bash
# CI/Ubuntu (paths padrão do sistema — sem env):
sudo apt-get install libvulkan1 mesa-vulkan-drivers vulkan-validationlayers
ctest --preset linux-debug -L rhi_hardware --output-on-failure

# Sysroot local (ICD lavapipe + layers fora do sistema):
export LD_LIBRARY_PATH=/caminho/sysroot/usr/lib/x86_64-linux-gnu
export VK_ICD_FILENAMES=/caminho/sysroot/usr/share/vulkan/icd.d/lvp_icd.json
export VK_LAYER_PATH=/caminho/sysroot/usr/share/vulkan/explicit_layer.d
ctest --preset linux-debug -L rhi_hardware --output-on-failure
```

Notas: lavapipe/llvmpipe é renderização por SOFTWARE — os testes reportam
`softwareRendering` verdadeiro e nunca declaram suporte de hardware. O
`LSAN_OPTIONS` com suppressions do ICD já é aplicado pela propriedade do
teste (ver `engine/rhi/backends/vulkan/tests/lavapipe_lsan.supp`).
