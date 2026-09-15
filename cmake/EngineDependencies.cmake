# =============================================================================
# eng:: — Dependências externas (FetchContent, versões pinadas — PARTE 3)
#
# FASE 1: Catch2 v3.5.2 (testes).
# FASE 3 (ADR-030): nlohmann/json v3.11.3 — serial é dependência de BUILD
# (não só de teste), logo a busca é incondicional.
# Regra: toda dependência nova exige atualização da especificação antes do
# commit (PARTE 3) e ADR quando trocar uma fixada (guardrail §15.8).
# =============================================================================
include(FetchContent)

# --- Proteção de BUILD_TESTING compartilhada --------------------------------
# Dependências não devem construir seus próprios testes quando importadas
# por nós (padrão recomendado pelo FetchContent).
function(eng_fetchcontent_guard_build_testing)
    set(ENG_HAD_BUILD_TESTING_DEFINED FALSE)
    if(DEFINED BUILD_TESTING)
        set(ENG_HAD_BUILD_TESTING_DEFINED TRUE)
        set(ENG_BUILD_TESTING_SAVED "${BUILD_TESTING}")
    endif()
    set(BUILD_TESTING OFF PARENT_SCOPE)
endfunction()

function(eng_fetchcontent_restore_build_testing)
    if(DEFINED ENG_BUILD_TESTING_SAVED)
        set(BUILD_TESTING "${ENG_BUILD_TESTING_SAVED}" PARENT_SCOPE)
    else()
        unset(BUILD_TESTING PARENT_SCOPE)
    endif()
endfunction()

# --- nlohmann/json (FASE 3, ADR-030) -----------------------------------------
# MIT, header-only, amplamente testada. Pina por tag exata (URL_HASH será
# acrescentado junto com o pipeline de re-pinning, como em Catch2).
# JSON_ImplicitConversions=OFF: conversões implícitas json→T são armadilha;
# o wrapper eng::serial::JsonValue usa .get<T>() explícito.
set(ENG_NLOHMANN_JSON_TAG "v3.11.3")

eng_fetchcontent_guard_build_testing()
set(JSON_BuildTests OFF)
set(JSON_Install OFF)
set(JSON_ImplicitConversions OFF)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        ${ENG_NLOHMANN_JSON_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
eng_fetchcontent_restore_build_testing()

# Headers de dependência são SYSTEM para os consumidores: os warnings do
# projeto (ADR-020) aplicam-se ao NOSSO código — dependências compilam com
# seus flags nativos (política já registrada em EngineWarnings.cmake).
get_target_property(ENG_NLOHMANN_INC nlohmann_json
    INTERFACE_INCLUDE_DIRECTORIES)
if(ENG_NLOHMANN_INC)
    set_target_properties(nlohmann_json PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${ENG_NLOHMANN_INC}")
endif()

# --- Catch2 (FASE 1; testes) --------------------------------------------------
if(NOT ENG_BUILD_TESTS)
    message(STATUS "ENG_BUILD_TESTS=OFF — Catch2 não será obtido")
    return()
endif()

# Pina por tag exata. URL_HASH (SHA-256 do tarball) será acrescentado quando
# houver pipeline automatizado de re-pinning.
set(ENG_CATCH2_TAG "v3.5.2")

eng_fetchcontent_guard_build_testing()
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${ENG_CATCH2_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)
eng_fetchcontent_restore_build_testing()
