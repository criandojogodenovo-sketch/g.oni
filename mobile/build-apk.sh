#!/usr/bin/env bash
# ============================================================
# G.oni Llumni — Build do APK Android (via Capacitor)
#
# Requisitos (na máquina local):
#   - Node.js 18+
#   - JDK 17
#   - Android SDK (ANDROID_HOME configurado)
#   - Gradle (ou use o gradlew gerado pelo Capacitor)
#
# Uso:
#   bash mobile/build-apk.sh          # debug APK
#   bash mobile/build-apk.sh release  # release (não assinado)
# ============================================================
set -euo pipefail

MODE="${1:-debug}"

echo "▸ G.oni Llumni — build Android ($MODE)"
echo "▸ 1/4: instalando dependências web..."
npm install

echo "▸ 2/4: build de produção (dist/)..."
npm run build

echo "▸ 3/4: Capacitor sync (copia dist/ para a plataforma nativa)..."
npx cap sync android

echo "▸ 4/4: Gradle assemble${MODE^}..."
(cd android && ./gradlew "assemble${MODE^}")

APK=$(find android/app/build/outputs -name "*.apk" | head -1 || true)
if [ -n "$APK" ]; then
  echo ""
  echo "✅ APK gerado: $APK"
  echo "   Instale com: adb install -r $APK"
else
  echo "⚠️  APK não encontrado — verifique os erros do Gradle acima."
  exit 1
fi
