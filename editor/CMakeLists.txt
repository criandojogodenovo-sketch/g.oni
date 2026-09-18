# =============================================================================
# editor/ — núcleo C++ do Native Mobile Editor (FASE 8, missão §5)
#
# CONSUMIDOR de engine/ (nunca o contrário — regra 00-overview.md):
#   - no LINUX (raiz, ENG_BUILD_TESTS): biblioteca + suite Catch2 com
#     backends REAIS (lavapipe/llvmpipe) — o mesmo código que o APK embute;
#   - no APK (android/app/src/main/cpp): add_subdirectory deste arquivo
#     com ENG_BUILD_TESTS=OFF — apenas a biblioteca eng::editor, fontes
#     JAMAIS duplicadas.
#
# jni.h NÃO aparece aqui (fronteira vive em android/app/EditorJni.cpp).
# Depende de eng::rhi::vulkan/gles (registrar fábricas é papel de HOST —
# mesma decisão do runtime Android FASE 7, ADR-036/039).
# =============================================================================

add_library(eng_editor STATIC
    src/EditorDocument.cpp
    src/Inspector.cpp
    src/AssetBrowser.cpp
    src/Viewport.cpp
    src/ViewportRenderer.cpp
    src/EditorHost.cpp
    src/ComponentRegistration.cpp
    src/NiRuntime.cpp
    src/TextureCache.cpp
)
add_library(eng::editor ALIAS eng_editor)

target_include_directories(eng_editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_editor PUBLIC cxx_std_20)

eng_apply_warnings(eng_editor)
eng_apply_sanitizers(eng_editor)
eng_apply_lto(eng_editor)
# Mesmo contrato das libs do motor: sem exceções/RTTI (ADR-004/005).
target_compile_options(eng_editor PRIVATE ${ENG_RUNTIME_FLAGS})

target_link_libraries(eng_editor PUBLIC
    eng::niscript
    eng::scene
    eng::serial
    eng::project
    eng::assets
    eng::image
    eng::reflect
    eng::input
    eng::physics
    eng::animation
    eng::particles
    eng::log
    eng::core
    eng::rhi
    eng::rhi::vulkan
    eng::rhi::gles)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_editor_tests tests/EditorTests.cpp)
target_include_directories(eng_editor_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(eng_editor_tests PRIVATE
    eng::editor
    eng::fs
    Catch2::Catch2WithMain)
eng_apply_test_policy(eng_editor_tests)
add_test(NAME editor COMMAND eng_editor_tests)
set_tests_properties(editor PROPERTIES
    SKIP_RETURN_CODE 4
    LABELS "rhi_hardware"
    # LSan: origens de terceiros (ICD/layers/drivers) — ver
    # tests/drivers_lsan.supp; leaks do MOTOR continuam detectáveis.
    ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tests/drivers_lsan.supp")
