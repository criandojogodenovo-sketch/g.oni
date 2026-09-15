#pragma once

/// eng::assets — identidade, catálogo e ciclo de vida de assets (FASE 3;
/// ADR-028/029).
///
/// Fora do escopo desta fase (missão §2.12, explicitamente): texturas GPU,
/// meshes, compilação de shaders, pipeline de importação, deduplicação por
/// content-hash, watch de arquivos, hot reload, carregamento assíncrono
/// (FASE 4, via eng::jobs — ADR da própria missão §2.10).
#include "eng/assets/AssetId.hpp"
#include "eng/assets/AssetManager.hpp"
#include "eng/assets/AssetMeta.hpp"
#include "eng/assets/AssetRegistry.hpp"
#include "eng/assets/AssetResolver.hpp"
#include "eng/assets/AssetSerial.hpp"
#include "eng/assets/AssetType.hpp"
