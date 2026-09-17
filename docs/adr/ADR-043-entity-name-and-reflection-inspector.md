# ADR-043 — Nome de entidade e Inspector por reflexão

- **Status:** aceito (FASE 8)
- **Contexto:** hierarquia/inspector exigem nomes legíveis (§8.2/§8.3) e o
  inspector NÃO pode hard-codar componentes se a reflection puder fornecer
  metadados (§8.4).
- **Decisões:**
  1. **`eng::scene::Name{std::string value}` é componente de DOMÍNIO** (não
     do editor): a FASE 11 (NI-Script) fará lookup por nome. Registrado no
     reflect + built-in do SceneSerializer (persistido em cena). Sem
     unicidade imposta (desambiguação por SceneEntityId — ADR-028/033).
  2. **Catálogo ÚNICO de componentes** — `ComponentEntry` do SceneSerializer
     ganha `emplaceDefault`/`get`/`getMutable`/`removeFrom` (type-erased) em
     vez de o editor criar um segundo registry. Componentes das FASES 9/10
     (input/ui/audio/physics/animation/particles) aparecem no inspector
     automaticamente ao serem registrados no serializer.
  3. **Inspector por offset+typeName** (`PropertyInfo` do reflect): leitura
     e escrita de campos por caminho (`position.x`), valores como string na
     fronteira (JNI recebe texto), enums por NOME do enumerador
     (estabilidade entre builds — ADR-033). Zero conhecimento de
     componentes específicos no Inspector (nem um `if` de "Transform").
  4. **Protegidos de remoção:** `eng::math::Transform` (geometria do nó) e
     `eng::scene::Name` (rótulo mínimo). Internos (Hierarchy/SceneIdentity/
     WorldMatrix) nem aparecem no catálogo.
- **Consequências:** mudanças aditivas pequenas em `engine/scene`
  (documentadas na auditoria como D1/D2); o editor ganha um inspector que
  evolui com os componentes do jogo sem tocar na UI; o custo é o acesso
  por offset exigir layout público (já era requisito do reflect — ADR-021).
