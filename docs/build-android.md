# Build Android — runtime G.ONI (FASE 7)

## Visão

```text
android/app (Gradle: Kotlin Activities + JNI)
        ↓ externalNativeBuild (CMake do NDK)
libgoni.so = GoniJni.cpp + EditorJni.cpp + android/runtime + engine/ (targets
             reusados) + editor/ (eng::editor — núcleo do editor FASE 8)
        = eng::rhi + backends Vulkan/GLES (dlopen em runtime) + eng::log
APK: arm64-v8a, minSdk 24, zero permissões, zero dependências de terceiros
LAUNCHER: EditorActivity (editor) · GoniActivity (runtime-demo, sem launcher)
```

Versões pinadas: AGP 8.5.2 · Kotlin 1.9.24 · Gradle 8.10.2 · NDK
27.0.12077973 · CMake 3.31.6 (SDK) · compileSdk/targetSdk 34 (ADR-041).

## Pré-requisitos

| Ferramenta | Versão | Observação |
|---|---|---|
| JDK | 17+ | Temurin recomendado (JRE não basta — precisa de `javac`) |
| Android SDK | cmdline-tools recentes | `sdkmanager` no PATH ou via `ANDROID_HOME` |

Instalação dos pacotes (SDK oficial — origem fixada, §XXXI):

```bash
sdkmanager "platforms;android-34" "build-tools;34.0.0" \
           "ndk;27.0.12077973" "cmake;3.31.6"
```

Configure o SDK local (fora do git):

```bash
echo "sdk.dir=/caminho/do/android-sdk" > android/local.properties
```

## Build do APK

```bash
cd android
./gradlew assembleDebug --no-daemon
# resultado: app/build/outputs/apk/debug/app-debug.apk
```

## Inspeção do APK (evidência — §XXV)

```bash
APK=android/app/build/outputs/apk/debug/app-debug.apk
unzip -l "$APK" | grep libgoni                       # lib/arm64-v8a/libgoni.so
NDK=$ANDROID_HOME/ndk/27.0.12077973/toolchains/llvm/prebuilt/linux-x86_64/bin
unzip -q "$APK" lib/arm64-v8a/libgoni.so -d /tmp/apkx
file /tmp/apkx/lib/arm64-v8a/libgoni.so               # ELF ARM aarch64
"$NDK/llvm-readelf" -d /tmp/apkx/lib/arm64-v8a/libgoni.so | grep NEEDED
"$NDK/llvm-nm" -D /tmp/apkx/lib/arm64-v8a/libgoni.so | grep Java_com_goni
"$ANDROID_HOME/build-tools/34.0.0/aapt" dump badging "$APK" | head -6
```

Esperado: ELF `ARM aarch64 ... for Android 24, built by NDK r27`;
`NEEDED` apenas libandroid/liblog/libdl/libm/libc++_shared/libc (Vulkan e
EGL são `dlopen` em runtime — nunca linkado); 60 símbolos
`Java_com_goni_*` (10 de `GoniRuntime` + 50 de `EditorJni` — atualizado
pela auditoria final 4–10: `nativeOnTouch` da FASE 9 e
`nativeEditorSetGameViewportSize` da remediação); package
`com.goni.runtime`; launchable `com.goni.runtime.EditorActivity`;
nenhuma permissão.

## Instalar/executar (quando houver adb/dispositivo)

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb logcat -s GONI        # eventos [G.ONI] (§XVIII)
# Backend por argumento (§XV):
adb shell am start -n com.goni.runtime/.GoniActivity --es backend auto
adb shell am start -n com.goni.runtime/.GoniActivity --es backend vulkan
adb shell am start -n com.goni.runtime/.GoniActivity --es backend gles
```

O EDITOR abre pelo ícone (launcher `EditorActivity`, FASE 8): painéis de
hierarquia/inspector/assets, viewport com gestos (tap=seleção,
drag=mover/pan, pinch=zoom), PLAY/STOP com separação editor×runtime e
import de assets via SAF. Logcat `adb logcat -s GONI` mostra os eventos
do editor (`Editor host criado`, `Editor surface created`, `Backend
selected`, `PLAY: runtime clone pronto`, `STOP: runtime descartado`).

Eventos esperados no logcat: `Android runtime started` → `Surface
created` → `Surface changed` → `Backend requested` → `Backend selected` →
`API version` / `GPU vendor` / `GPU renderer` / `Driver` / `Surface
status` → `First frame submitted` → `First frame presented` (triangle
colorido na tela) → `Runtime paused/resumed` (lifecycle) → `Runtime
shutdown`.

## Ciclos de teste em dispositivo (§XXVIII/§XXIX)

- Surface: girar a tela / abrir e fechar (CREATE→RENDER→DESTROY→
  RECREATE→RENDER);
- Lifecycle: botão home/recente (RUNNING→PAUSE→RESUME→RUNNING);
- Shutdown: encerrar o app pelo recentes.

## CI

`.github/workflows/ci-android.yml`: assembleDebug + inspeção completa +
upload do APK como artefato. Sem hardware no CI —
`ANDROID_HARDWARE_TEST = UNAVAILABLE` (documentado, não bloqueia).

## Notas de design

- ADR-039: Activity pura + Choreographer (render na UI thread);
- ADR-040: ownership ANativeWindow (acquire/release, ordem
  renderer→janela, estados);
- ADR-041: reuso de targets CMake, versões pinadas, evidência por
  estágio.
