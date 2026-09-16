# Shaders de teste — triangle (paridade Vulkan/GLES, missão §40)

O MESMO caso (vertex data + intenção de pipeline + draw) roda nos dois
backends reais para provar que a abstraction não é modelada em torno de
nenhuma das APIs:

| Arquivo | Papel |
|---|---|
| `triangle_vk.vert/.frag` | GLSL 450 (Vulkan) |
| `triangle_vk.vert.spv/.frag.spv` | SPIR-V compilado (build-time por `glslangValidator --target-env vulkan1.1 -V`, validado com `spirv-val`) |
| `triangle_vk_vert_spirv.hpp` / `triangle_vk_frag_spirv.hpp` | SPIR-V EMBUTIDO (headers gerados, committados para hermetismo do build) |
| `triangle_gles.vert/.frag` | GLSL ES 300 (FASE 6) |

## Regenerar os headers

```bash
glslangValidator -V --target-env vulkan1.1 -o triangle_vk.vert.spv triangle_vk.vert
glslangValidator -V --target-env vulkan1.1 -o triangle_vk.frag.spv triangle_vk.frag
spirv-val triangle_vk.vert.spv && spirv-val triangle_vk.frag.spv
# + o snippet python documentado no topo dos headers gerados
```

## Dados de vértice (compartilhados, não-arquivados)

Layout: binding 0, stride 32 — attr 0 `R32G32B32A32Sfloat` (posição, xy
usados), attr 1 `R32G32B32A32Sfloat` (cor RGBA).
