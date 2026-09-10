/**
 * G.oni Llumni — Configuração Vite
 * Build de produção gera dist/ compatível com deploy estático (Vercel).
 * Duas entradas: o editor (index.html) e o runtime player (runtime/player.html).
 */
import { defineConfig } from 'vite';
import { fileURLToPath, URL } from 'node:url';

const dir = fileURLToPath(new URL('.', import.meta.url));

export default defineConfig({
  base: './',
  resolve: {
    alias: {
      '@core': fileURLToPath(new URL('./core/src', import.meta.url)),
      '@editor': fileURLToPath(new URL('./editor/src', import.meta.url)),
      '@projects': fileURLToPath(new URL('./projects/src', import.meta.url)),
      '@runtime': fileURLToPath(new URL('./runtime/src', import.meta.url)),
    },
  },
  build: {
    outDir: 'dist',
    target: 'es2022',
    sourcemap: false,
    rollupOptions: {
      input: {
        main: `${dir}index.html`,
        player: `${dir}runtime/player.html`,
      },
    },
  },
  server: {
    host: true,
    port: 5173,
  },
  preview: {
    host: true,
    port: 4173,
  },
});
