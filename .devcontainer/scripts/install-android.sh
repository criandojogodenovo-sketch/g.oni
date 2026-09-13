#!/usr/bin/env bash
# =============================================================================
# Camada Android do devcontainer (§12): SDK/NDK/Gradle com versões fixadas
# (PARTE 3): AGP 8.5.x, Gradle 8.7, NDK r26d (26.3.11579264), SDK 34.
# Idempotente: pula o que já está instalado no local esperado.
#
# ATENÇÃO: o código android/ (Kotlin/JNI) entra na FASE 6 (§17.2) — esta
# camada apenas provisiona as ferramentas para que verify.sh e as fases
# seguintes tenham o ambiente pronto.
# =============================================================================
set -euo pipefail

ANDROID_SDK_ROOT_DEFAULT="${HOME}/android-sdk"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-${ANDROID_SDK_ROOT_DEFAULT}}"
NDK_VERSION="26.3.11579264"
GRADLE_VERSION="8.7"
CMDLINE_TOOLS_VERSION="11076708"
SDK_LEVEL="34"
BUILD_TOOLS="34.0.0"

echo "==> [android] SDK root: ${ANDROID_SDK_ROOT}"

if [ -d "${ANDROID_SDK_ROOT}/ndk/${NDK_VERSION}" ]; then
    echo "==> [android] NDK ${NDK_VERSION} já presente — pulando"
else
    echo "==> [android] baixando command-line-tools..."
    mkdir -p "${ANDROID_SDK_ROOT}/cmdline-tools"
    TOOLS_DIR="${ANDROID_SDK_ROOT}/cmdline-tools/latest"
    if [ ! -d "${TOOLS_DIR}" ]; then
        curl -fsSL -o /tmp/cmdline-tools.zip \
            "https://dl.google.com/android/repository/commandlinetools-linux-${CMDLINE_TOOLS_VERSION}_latest.zip"
        unzip -q /tmp/cmdline-tools.zip -d /tmp/cmdline-tools
        mkdir -p "${TOOLS_DIR}"
        mv /tmp/cmdline-tools/cmdline-tools/* "${TOOLS_DIR}/"
        rm -rf /tmp/cmdline-tools /tmp/cmdline-tools.zip
    fi

    export ANDROID_HOME="${ANDROID_SDK_ROOT}"
    SDKMANAGER="${TOOLS_DIR}/bin/sdkmanager"

    echo "==> [android] aceitando licenças..."
    yes | "${SDKMANAGER}" --licenses >/dev/null 2>&1 || true

    echo "==> [android] instalando platform-tools, SDK ${SDK_LEVEL} e NDK..."
    yes | "${SDKMANAGER}" \
        "platform-tools" \
        "platforms;android-${SDK_LEVEL}" \
        "build-tools;${BUILD_TOOLS}" \
        "ndk;${NDK_VERSION}" >/dev/null
fi

if command -v gradle >/dev/null 2>&1 && gradle --version 2>/dev/null | grep -q "${GRADLE_VERSION}"; then
    echo "==> [android] Gradle ${GRADLE_VERSION} já presente — pulando"
else
    echo "==> [android] instalando Gradle ${GRADLE_VERSION}..."
    curl -fsSL -o /tmp/gradle.zip \
        "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip"
    sudo unzip -q /tmp/gradle.zip -d /opt
    sudo ln -sf "/opt/gradle-${GRADLE_VERSION}/bin/gradle" /usr/local/bin/gradle
    rm -f /tmp/gradle.zip
fi

echo "==> [android] versões instaladas:"
"${ANDROID_SDK_ROOT}/platform-tools/adb" version | head -n1
ls -d "${ANDROID_SDK_ROOT}/ndk/${NDK_VERSION}"
gradle --version | grep Gradle | head -n1
echo "==> [android] OK"
