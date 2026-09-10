/**
 * G.oni Llumni — Configuração Capacitor (APK nativo Android/iOS)
 * Nota: este arquivo vive na raiz porque o CLI do Capacitor procura
 * capacitor.config.* a partir do diretório do projeto web.
 * Veja mobile/README.md para o fluxo completo de build do APK.
 */
const config = {
  appId: 'com.goni.llumni',
  appName: 'G.oni Llumni',
  webDir: 'dist',
  bundledWebRuntime: false,
  android: {
    allowMixedContent: false,
    captureInput: true,
    webContentsDebuggingEnabled: false,
  },
  ios: {
    contentInset: 'always',
  },
  server: {
    androidScheme: 'https',
  },
};

export default config;
