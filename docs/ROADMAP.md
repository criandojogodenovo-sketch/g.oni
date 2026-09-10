# G.oni Llumni — Roadmap

## v1 (entregue)

- [x] Render PBR WebGL2 (sombras PCF, HDR+MSAA, bloom, ACES)
- [x] Física própria (OBB SAT, impulsos sequenciais, juntas, raycast, CC)
- [x] G.oni Script (lexer/parser/interpretador async, corrotinas, classes)
- [x] G.oni Visual (editor de nós → export p/ script)
- [x] G.oni Signal / Links / Funciona / Objetos / Construt / Eliminação
- [x] Editor mobile-first completo (gizmos, 7 painéis, play mode)
- [x] Formato .g.oni (gzip) + VFS IndexedDB
- [x] Runtime player standalone
- [x] Configuração Capacitor + script de APK

## v1.1 (curto prazo)

- [ ] Seleção de vértice/face no editor de modelagem (half-edge)
- [ ] Broadphase por hash espacial (cenas 500+ corpos)
- [ ] Cápsula-vs-caixa exata (capsule clipping)
- [ ] Undo granular (por operação, não snapshot)
- [ ] Multi-cena + gerenciador de cenas
- [ ] Animação de propriedades de material (emissive/opacity)

## v1.2

- [ ] WebGPU com fallback automático a WebGL2
- [ ] SSAO e FXAA
- [ ] Occlusion culling (occluders/portais)
- [ ] Skinning + importação glTF
- [ ] Port C++/WASM de math+physics (ver ARCHITECTURE.md §migração)
- [ ] Compilação de G.oni Script para closures (10× mais rápido que árvore)
- [ ] G.oni Visual: execução direta do grafo (sem exportar)

## v2

- [ ] Colaboração em tempo real (WebSocket)
- [ ] Editor de shaders GLSL visual com preview
- [ ] Asset store / biblioteca de prefabs
- [ ] Export PWA + iOS via Capacitor
- [ ] IA assistente de cena (script scaffolding)

---

Contribuições são bem-vindas — a arquitetura em módulos isolados
(`core/src/{math,render,physics,script,goni}`) foi desenhada para permitir
evolução independente de cada subsistema.
