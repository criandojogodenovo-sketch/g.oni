# ADR-041 — Android: integração de build (Gradle/NDK/CMake)

- **Estado:** aceito (FASE 7, missão §XXI–§XXV/§XXX/§XXXI)
- **Contexto:** o APK precisa compilar o engine EXISTENTE para arm64-v8a
  sem duplicar fontes, com build reproduzível e dependências oficiais
  fixadas.

## Decisão

### Reuso integral de targets CMake (missão §XXIII)

`android/app/src/main/cpp/CMakeLists.txt` faz
`add_subdirectory(<repo>/engine engine)` — os MESMOS `CMakeLists.txt` dos
módulos (core…rhi/backends) compilam no Linux (testes) e no NDK (APK):
zero cópias Android de fontes. Requisitos respeitados pelo build raiz
(desde as fases anteriores): módulos guardam testes atrás de
`ENG_BUILD_TESTS`; Catch2 é pulado com `ENG_BUILD_TESTS=OFF`;
`nlohmann/json` (dependência de BUILD) é header-only e busca-se igual.

`libgoni.so` = `GoniJni.cpp` + `android/runtime/src/AndroidRuntime.cpp`
+ `eng_rhi` + `eng_rhi_vulkan` + `eng_rhi_gles` + `eng_log` + `eng_core`,
com `-landroid -llog`. Vulkan/EGL são `dlopen` em runtime (ADR-037/038) —
o `.so` NÃO linka com libvulkan/libEGL (verificável em `NEEDED`).

### Versões PINADAS (missão §XXII/§XXXI — tudo oficial)

| Peça | Versão | Origem |
|---|---|---|
| AGP | 8.5.2 | google() |
| Kotlin | 1.9.24 | mavenCentral() |
| Gradle | 8.10.2 (wrapper) | services.gradle.org |
| compileSdk/targetSdk | 34 | SDK oficial |
| minSdk | 24 (Android 7.0 — primeiro com Vulkan no NDK) | — |
| NDK | 27.0.12077973 (r27) | dl.google.com |
| CMake (SDK) | 3.31.6 | dl.google.com |
| JDK | 17+ (CI usa Temurin 17) | Adoptium |

Sem dependências de terceiros: `dependencies {}` vazio (Activity pura +
kotlin-stdlib do plugin — missão §XXII). Manifest sem NENHUMA permissão
(§XXI). `local.properties` (sdk.dir) fica fora do git.

### Gradle Kotlin DSL + limites de recursos

`build.gradle.kts`/`settings.gradle.kts`; `org.gradle.jvmargs=-Xmx2048m` e
`parallel=false` (ambiente de build com 2 vCPU/3GB — o mesmo build roda no
CI com folga).

### Evidência por estágio (missão §XXXIX)

| Estágio | Local | CI Android |
|---|---|---|
| IMPLEMENTED | código commitado | idem |
| UNIT/INTEGRATION TESTED (runtime, Linux) | 10 casos/86 asserções vs backends reais | ci-linux (19 suites) |
| BUILT (APK) | `./gradlew assembleDebug` | ci-android (assembleDebug) |
| APK INSPECTED | unzip/readelf/nm/aapt | ci-android (passo de inspeção + artefato) |
| APK INSTALLED / EMULATOR TESTED / DEVICE TESTED | **UNAVAILABLE** (sem adb/KVM/dispositivo) | idem |

## Consequências

- `ci-android.yml` ADICIONA workflow (não substitui o CI Linux — §XXX);
  hardware não é exigido no CI (`ANDROID_HARDWARE_TEST = UNAVAILABLE`
  documentado, não bloqueia build).
- clang (NDK) revelou warnings que o GCC não dava — corrigidos no código
  (não `-Wno-`): categoria de log não usada, constante Linux-only no
  Android, `VK_USE_PLATFORM_ANDROID_KHR` antes do primeiro `vulkan.h`.
