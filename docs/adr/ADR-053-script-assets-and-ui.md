# ADR-053 — Scripts NI-Script como assets do projeto (.nis) + UI de edição

- **Status**: ACEITO
- **Data**: 2026-09-18 (evolução P0-7)
- **Contexto**: `docs/goni_engine_audit_current.md` §5.13/§7-P0.7; adiância
  declarada desde a FASE 11 (`docs/ni-script/08-tools.md`)

## Contexto

A FASE 11 anexou scripts ao editor via `NiScriptComponent{source}` — a fonte
.nis COMPLETA vive no componente da entidade. Funciona (play/tick/stop
testados), mas a auditoria PHASE 0 (§5.13) apontou o que faltava para a
missão P0:

1. **Sem asset .nis** — script não é arquivo do projeto: não aparece no
   AssetBrowser como artefato listável/versionável; não há workflow
   "criar script → editar → reusar em várias entidades".
2. **Sem UI de script** — editar `source` pelo Inspector era um EditText de
   UMA LINHA com truncamento silencioso em 512 bytes (limite defensivo do
   buffer JNI `copyJString`): na prática, IMPOSSÍVEL escrever código real
   no aparelho.
3. **Sem compilação de validação** — erros só apareciam no logcat ao dar
   PLAY (pior lugar possível para descobrir um typo).

## Decisão

### 1. O asset .nis é a FONTE DE EDIÇÃO; a cena carrega CÓPIA estável

`assets/scripts/*.nis` são artefatos do projeto (AssetBrowser category
"scripts", AssetType::Script — categoria que existia vazia desde a FASE 8).
`scriptAssign(entity, name)` copia o CONTEÚDO do asset para
`NiScriptComponent.source`:

- a cena continua AUTOCONTIDA (serialização/runtime nunca dependem de
  arquivos externos — ADR-043/044 preservados; bundles exportados não
  precisam resolver paths);
- reusar o mesmo script em N entidades é copiar N vezes — deliberado:
  editar o asset depois NÃO muta cenas salvas (previsibilidade), e o
  usuário re-anexa quando quiser;
- deletar o asset não quebra a cena (a cópia segue válida).

### 2. `EditorDocument` ganha a API de scripts

`scriptList/scriptRead/scriptWrite/scriptCreate/scriptDelete/scriptCompile/
scriptAssign`:

- `scriptWrite` escreve direto (fs writeAllText) e cataloga arquivos novos
  via `AssetBrowser::registerExisting` (novo método público: upsert de meta
  para arquivo JÁ POSICIONADO, preservando AssetId por path — ADR-029);
- `scriptCreate` força extensão `.nis`, recusa duplicados/traversal, e
  gera o TEMPLATE canônico — que o TESTE prova compilar limpo;
- `scriptCompile` valida a fonte com a MESMA `NiNativeTable` do runtime de
  Play (`addBaseLibrary` + `addStandardHost`) — o que valida é o que o
  jogo compila. Retorna `ScriptCheck{ok, diags[]}` dentro do Result: o
  Result falha só em erro INTERNO; o veredito do compilador é dado.

### 3. JNI: strings de conteúdo SEM limite

`jniToString` novo (GetStringUTFRegion → std::string alocada) para
CONTEÚDO de script (multi-KB). Nomes continuam no caminho limitado
(512B) — são curtos por natureza. O TSV de diagnóstico é
`"1|0"` + linhas `line\tcol\tmessage`.

### 4. Painel Scripts no editor Android

Barra inferior ganha "Scripts": lista dos .nis do projeto, "+ Novo
script" (dialog de nome), toque abre o EDITOR: fonte multi-linha
monospace em ScrollView, com ações **Compilar** (diálogos de diagnóstico
`line:col message` em vermelho, ou confirmação), **Anexar** à entidade
selecionada (toast de feedback), **Salvar** (write + fecha).

## Alternativas rejeitadas

- **Referência por path/id no componente** (em vez de cópia): quebraria a
  autocontenção da cena e o clone de Play precisaria resolver assets —
  complexidade sem ganho para o estágio atual.
- **Validação só no Play**: já era o estado anterior; descobrir erro de
  sintaxe ao rodar é hostil (a missão exige "compilar/diagnósticos").
- **Reuso de import SAF para criar .nis**: o import move um arquivo
  existente; scripts NASCEM no editor (writeAllText direto) —
  `registerExisting` cobre a catalogação.

## Consequências

- O buffer de 512B deixa de limitar a EDIÇÃO de scripts (fontes multi-KB);
  o campo `source` no Inspector continua um text de uma linha — o caminho
  de edição agora é o painel Scripts (o Inspector mostra o conteúdo, não
  é o editor).
- Workflows futuros (P2 visual scripting) podem reusar `scriptCompile` como
  validação de saída gerada.
- O template inicial é minimal mas REAL (usa self()/position — os bindings
  da FASE 11): serve de documentação viva no aparelho.

## Evidências

- `editor/tests/EditorTests.cpp`: 4 casos novos (create/template-compila/
  duplicado/traversal + registry; write/read round-trip com id estável e
  delete; compile válido vs string-aberta vs nativo-inexistente;
  assign→PLAY→tick roda up update com efeito verificado no Transform) —
  suite 50 casos/566 asserções verdes.
