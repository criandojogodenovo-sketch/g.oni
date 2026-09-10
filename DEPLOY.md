# Deploy — G.oni Llumni

Build de produção 100% estático (`dist/`), pronto para Vercel, Netlify,
GitHub Pages ou qualquer CDN.

---

## 1. Vercel via painel (recomendado)

1. Faça push do projeto para um repositório GitHub
2. Acesse **vercel.com/new** e importe o repositório
3. A Vercel detecta **Vite** automaticamente; se pedir:
   - **Framework Preset:** Vite
   - **Build Command:** `npm run build`
   - **Output Directory:** `dist`
4. Clique em **Deploy** ✓

Cada push na `main` gera um novo deploy automaticamente.

## 2. Vercel via CLI

```bash
npm i -g vercel

vercel login          # ou use um token: export VERCEL_TOKEN=...

vercel                # preview (deploy de teste)
vercel --prod         # produção
```

Com token (CI/automação):

```bash
VERCEL_TOKEN=<seu-token> vercel --prod --yes
```

## 3. Link com o repositório (deploys automáticos)

Após o primeiro deploy via CLI:

1. No painel da Vercel, abra o projeto → **Settings → Git**
2. **Connect** → escolha o repositório GitHub
3. Branch de produção: `main` (padrão)
4. Pronto: `git push` = deploy

Ou via CLI:

```bash
vercel link           # vincula o diretório ao projeto na nuvem
```

## 4. Netlify (alternativa)

```toml
# netlify.toml
[build]
  command = "npm run build"
  publish = "dist"
```

## 5. GitHub Pages (alternativa)

```bash
npm run build
# publique dist/ na branch gh-pages (ex.: com o pacote "gh-pages")
npx gh-pages -d dist
```

---

## Comandos git (primeiro envio)

```bash
git init
git add .
git commit -m "feat: initial G.oni Llumni engine"
git branch -M main
git remote add origin https://github.com/SEU_USUARIO/g.oni.git
git push -u origin main
```

> **Nunca** comite tokens/segredos. O `.gitignore` já exclui `.env` e `.vercel`.

## Variáveis de ambiente

O projeto é estático — **não precisa de nenhuma** variável para funcionar.
Tudo (projetos, scripts, assets) persiste no IndexedDB do navegador.

## URLs do build

| Página | Caminho |
|---|---|
| Editor | `/` (index.html) |
| Runtime player | `/runtime/player.html` |

O player aceita `?src=<url-do-arquivo.g.oni>` para carregar jogos hospedados.
