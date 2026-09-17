# =============================================================================
# android/runtime — runtime C++ do Android (FASE 7; ADR-039/040)
#
# CONSUMIDOR de engine/ (nunca o contrário — regra 00-overview.md). Os
# fontes aqui são compilados:
#   - no LINUX (este CMakeLists, via raiz com ENG_BUILD_TESTS) para os
#     testes do state machine/lifecycle contra backends REAIS;
#   - no APK (android/app/src/main/cpp) para arm64-v8a — os MESMOS arquivos,
#     sem duplicação (missão §XXIII).
#
# jni.h NÃO aparece aqui: a fronteira JNI vive em android/app (GoniJni.cpp).
# =============================================================================
if(NOT ENG_BUILD_TESTS)
    return()  # o alvo Android (libgoni.so) é construído pelo CMake do app
endif()

add_executable(eng_android_runtime_tests
    tests/AndroidRuntimeTests.cpp
    src/AndroidRuntime.cpp
)
target_include_directories(eng_android_runtime_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(eng_android_runtime_tests PRIVATE
    eng::rhi
    eng::rhi::vulkan
    eng::rhi::gles
    eng::input
    eng::log
    Catch2::Catch2WithMain)
eng_apply_test_policy(eng_android_runtime_tests)
add_test(NAME android_runtime COMMAND eng_android_runtime_tests)
set_tests_properties(android_runtime PROPERTIES LABELS "rhi_hardware"
    # Bug C-17: Catch2 devolve exit 4 quando TODOS os casos SKIPam (ex.:
    # ambiente sem driver) — para o CTest isso é SKIP, não FAIL.
    SKIP_RETURN_CODE 4
    # LSan: origens de terceiros (ICD/layers/drivers) — ver
    # tests/drivers_lsan.supp; leaks do MOTOR continuam detectáveis.
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/drivers_lsan.supp")
