#pragma once

/// eng::platform — fatos do sistema hospedeiro: quem somos, onde estão as
/// raízes, quais variáveis de ambiente existem (FASE 3; ADR-026).
///
/// O que este módulo NÃO tem (missão §2.2 — abstração prematura é dívida):
/// tempo (core cobre), contagem de CPUs (jobs cobre), filesystem próprio
/// (fs cobre), logging (log cobre), thread pool (jobs cobre), display,
/// janelas, input — nada "guarda-chuva".
#include "eng/platform/Environment.hpp"
#include "eng/platform/PlatformInfo.hpp"
#include "eng/platform/PlatformPaths.hpp"
#include "eng/platform/ProcessInfo.hpp"
