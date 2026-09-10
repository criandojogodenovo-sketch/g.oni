<div align="center">

# ◈ G.oni Llumni

**Engine 3D completa, mobile-first, que roda no navegador.**

Editor · Física · Render PBR · Scripting visual e textual · Runtime · APK nativo

*Inspirada nas melhores ideias de **Blender**, **Godot**, **Unreal** e **Unity** — com identidade própria.*

</div>

---

## O que é

G.oni Llumni é uma engine 3D escrita em **TypeScript puro + WebGL2**, sem dependências de runtime (zero frameworks, zero libs externas no bundle). Tudo — renderizador PBR, física de corpos rígidos, máquina virtual de scripts, editor completo com gizmos, sistema de nós visuais e runtime de jogos exportados — foi implementado do zero.

**Mobile-first de verdade:** toda a interface funciona com toque (1 dedo orbita, 2 dedos dão zoom/pan, painéis deslizam de baixo), com alvos de toque ≥ 44px e safe-areas de iOS.

## Recursos

| Sistema | Descrição |
|---|---|
| **Render** | PBR (Cook-Torrance GGX, metal-roughness), luzes direcional/pontual/spot, sombras (shadow map 2048 + PCF 3×3), céu procedural, grid infinito, HDR + MSAA 4×, bloom, ACES tonemapping, vinheta |
| **Física** | Própria: corpos dinâmicos/estáticos/cinemáticos, colisores box (OBB/SAT)/esfera/cápsula, impulsos sequenciais com atrito e restituição, sleep, juntas (fixed/spring/hinge/slider), raycast, character controller com move & slide |
| **G.oni Script** | Linguagem própria (estilo GDScript): `func`, `var`, `const`, `class`, `signal`, corrotinas com `await`, `$No/Path`, tipagem dinâmica. Interpretador tree-walking assíncrono |
| **G.oni Visual** | Editor de nós com validação de tipos e **exportação para código G.oni Script** |
| **G.oni Signal** | `emit`/`connect`/`disconnect` + nativos: `on_ready`, `on_process`, `on_collision_enter/exit`, `on_input`, `on_destroy` |
| **G.oni Links** | Grafo de conexões entre entidades com detecção de ciclos e propagação de eventos |
| **G.oni Funciona** | Registro de funções nativas + de script, chamada cruzada, hot-reload |
| **G.oni Objetos** | Entidades com transform, componentes, hierarquia e metadados |
| **G.oni Construt** | Prefabs, instanciação em runtime (`spawn`), snap a grid, agrupamento |
| **G.oni Eliminação** | Destruição profissional: fila diferida, pooling, GC assistido, log de depuração |
| **Editor** | Viewport com picking por triângulo, gizmos (mover/rotacionar/escalar), hierarquia, inspetor, modelagem (subdividir/extrudar/chanfrar/ruído/suavizar/LOD), texturização por pintura direta no modelo (raycast→UV), animação com timeline e keyframes, script editor com console |
| **Projetos** | Tela inicial, persistência em IndexedDB, sistema de arquivos virtual (`scenes/`, `scripts/`, `models/`, `textures/`, `animations/`), import/export `.g.oni` (gzip) |
| **Runtime** | Player standalone para rodar jogos exportados (`runtime/player.html`) |
| **Mobile** | APK Android via Capacitor (`mobile/`) |

## Como rodar

```bash
npm install
npm run dev       # editor em http://localhost:5173
```

Build de produção:

```bash
npm run build     # gera dist/ (editor + player)
npm run preview   # serve o build localmente
```

Deploy (Vercel): veja **DEPLOY.md**.

## Começo rápido (2 minutos)

1. Abra o editor → **Novo Projeto** (um template com chão, luz, cubo físico e câmera é criado)
2. Toque no **Cubo** no viewport → use os gizmos laterais para mover/rotacionar/escalar
3. Abra **Script** no dock → crie um script → **Anexar ao objeto** → toque em **▶ Play**
4. Arraste o **joystick** para mover a câmera do jogo; o cubo cai e colide
5. **Salvar** persiste no navegador; **Exportar** gera o arquivo `.g.oni`

## Script de exemplo

```python
# G.oni Script — linguagem própria (estilo GDScript)
var velocidade = 90.0
var pulos = 0

signal pulou

func on_ready():
    print("Objeto pronto: " + self.name)

func on_process(delta):
    self.rotate_y(velocidade * delta)
    if input_pressed("jump"):
        emit("pulo")

func on_collision_enter(other, info):
    print("Colidiu com " + other.name)
    await wait(0.5)
    destroy(other)
```

Mais em **docs/GONI-SCRIPT.md**.

## Estrutura do projeto

```
g-oni-llumni/
├── core/                  # Núcleo da engine (TypeScript, pronto p/ WASM)
│   └── src/
│       ├── math/          # Vetores, matrizes, quaternions
│       ├── render/        # Pipeline WebGL2 + shaders GLSL
│       ├── physics/       # Corpos, colisões, juntas, raycast
│       ├── script/        # VM G.oni Script (lexer/parser/interpretador)
│       └── goni/          # Sistemas G.oni + Engine + formato .g.oni
├── editor/                # Editor web (mobile-first)
│   └── src/               # viewport, painéis, gizmos, visual scripting
├── runtime/               # Player standalone de jogos exportados
├── projects/              # Tela de projetos + IndexedDB + VFS
├── mobile/                # Capacitor: APK Android/iOS
├── docs/                  # Documentação técnica
├── capacitor.config.ts    # Config nativa (webDir: dist)
└── vite.config.ts         # Build (editor + player)
```

## Decisão técnica: TypeScript (não C++/WASM)

O plano original previa núcleo C++/WASM. Foi implementado em **TypeScript puro**
(alternativa explicitamente autorizada) porque:

- **Zero dependências** = bundle minúsculo e carregamento instantâneo em mobile
- Math/física em `core/src` são funções puras sem DOM → a API é **idêntica** à
  que um port C++ teria (mesmas classes `Vec3`, `Mat4`, `PhysicsWorld`...)
- A troca futura por WASM é localizada: os módulos `math/` e `physics/` podem
  ser compilados com Emscripten mantendo as assinaturas

Detalhes em **docs/ARCHITECTURE.md**.

## Documentação

- `docs/ARCHITECTURE.md` — arquitetura, pipeline, limitações conhecidas
- `docs/GONI-SCRIPT.md` — referência da linguagem
- `docs/FORMAT.md` — especificação do formato `.g.oni`
- `docs/ROADMAP.md` — próximos passos
- `DEPLOY.md` — deploy na Vercel (e GitHub)
- `mobile/README.md` — gerar APK Android

## Licença

MIT — use, estude, modifique e publique seus jogos livremente.
