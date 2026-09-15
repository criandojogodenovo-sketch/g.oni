#pragma once

/// eng::core — Result, Error, Span, Version e Uuid128.
/// FASE 1: Result/Error/Span/Version. FASE 3 (ADR-028, desvio D1): Uuid128
/// — extensão aditiva; três consumidores em camadas distintas precisam de
/// identidade UUIDv4 (assets, project, scene) e core é o ancestral comum.
#include "eng/core/Error.hpp"
#include "eng/core/Result.hpp"
#include "eng/core/Span.hpp"
#include "eng/core/Uuid.hpp"
#include "eng/core/Version.hpp"
