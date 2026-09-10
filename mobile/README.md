# G.oni Llumni — Mobile (APK nativo)

Gerar APK nativo (Android) a partir do mesmo código web, via **Capacitor**.

## Estrutura

```
mobile/
├── android/        # gerado por `npx cap add android` (ver passo 3)
├── ios/            # gerado por `npx cap add ios` (macOS + Xcode)
├── build-apk.sh    # script completo: web build + sync + gradle
└── README.md       # este arquivo
```

> A configuração do Capacitor fica na **raiz** do projeto
> (`capacitor.config.ts`), pois é onde o CLI procura por padrão.

## Requisitos locais

| Ferramenta | Versão | Observação |
|---|---|---|
| Node.js | 18+ | mesmo do desenvolvimento web |
| JDK | 17 | `java -version` |
| Android SDK | API 34+ | `ANDROID_HOME` no PATH |
| Gradle | incluído | o projeto gera `gradlew` |

## Passo a passo (Android)

```bash
# 1. Instalar dependências (web + Capacitor)
npm run mobile:setup
#   → instala @capacitor/cli, @capacitor/core e @capacitor/android

# 2. Build web de produção
npm run build
#   → gera dist/ (editor + runtime player)

# 3. Adicionar a plataforma Android (só a primeira vez)
npx cap add android
#   → cria android/ na raiz (pode mover p/ mobile/android com `cap copy`)

# 4. Sincronizar assets web
npm run mobile:sync
#   → npx cap sync android

# 5. Gerar o APK
npm run mobile:apk
#   → bash mobile/build-apk.sh
#   → android/app/build/outputs/apk/debug/app-debug.apk
```

Instalar no dispositivo:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

## Atalho: tudo em um comando

```bash
bash mobile/build-apk.sh          # APK debug
bash mobile/build-apk.sh release  # APK release (não assinado)
```

## Permissões

O app é um editor 3D offline — nenhuma permissão é exigida por padrão.
Se você adicionar importação de arquivos do dispositivo, acrescente em
`android/app/src/main/AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
```

## iOS (macOS)

```bash
npm i @capacitor/ios
npx cap add ios
npx cap sync ios
npx cap open ios   # abre o Xcode → Product ▸ Archive
```

## Ícone e splash

1. Substitua `android/app/src/main/res/mipmap-*/ic_launcher.png` pelos
   seus ícones (use [icon.kitchen](https://icon.kitchen) para gerar todos
   os tamanhos a partir do logo).
2. Splash: edite `res/drawable/splash.png` e o estilo
   `SplashTheme` em `res/values/styles.xml`.

## Observações

- O **mesmo** bundle web roda no navegador e no APK — persistência via
  IndexedDB funciona nativamente no WebView do Android.
- Para publicar na Play Store, gere um release assinado
  (`apksigner` + keystore) — siga a [documentação oficial](https://developer.android.com/studio/publish/app-signing).
