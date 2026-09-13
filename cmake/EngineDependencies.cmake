# =============================================================================
# eng:: — Dependências externas (FetchContent, versões pinadas — PARTE 3)
#
# FASE 1 (§17.1 item 11): SOMENTE Catch2.
# Regra: toda dependência nova exige atualização da especificação antes do
# commit (PARTE 3) e ADR quando trocar uma fixada (guardrail §15.8).
# =============================================================================
include(FetchContent)

if(NOT ENG_BUILD_TESTS)
    message(STATUS "ENG_BUILD_TESTS=OFF — Catch2 não será obtido")
    return()
endif()

# Pina por tag exata. URL_HASH (SHA-256 do tarball) será acrescentado quando
# houver pipeline automatizado de re-pinning.
set(ENG_CATCH2_TAG "v3.5.2")

# Protege o BUILD_TESTING do projeto: a dependência não deve construir seus
# próprios testes quando importada por nós (padrão recomendado pelo FetchContent).
set(ENG_HAD_BUILD_TESTING_DEFINED FALSE)
if(DEFINED BUILD_TESTING)
    set(ENG_HAD_BUILD_TESTING_DEFINED TRUE)
    set(ENG_BUILD_TESTING_SAVED "${BUILD_TESTING}")
endif()
set(BUILD_TESTING OFF)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        ${ENG_CATCH2_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

if(ENG_HAD_BUILD_TESTING_DEFINED)
    set(BUILD_TESTING "${ENG_BUILD_TESTING_SAVED}")
else()
    unset(BUILD_TESTING)
endif()
