add_library(eng_ui STATIC
    src/Ui.cpp
    src/Font.cpp
)
add_library(eng::ui ALIAS eng_ui)
target_include_directories(eng_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_ui PUBLIC cxx_std_20)
eng_apply_warnings(eng_ui)
eng_apply_sanitizers(eng_ui)
eng_apply_lto(eng_ui)
target_compile_options(eng_ui PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_ui PUBLIC eng::core eng::math eng::input)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_ui_tests tests/UiTests.cpp)
target_link_libraries(eng_ui_tests PRIVATE eng::ui Catch2::Catch2WithMain)
eng_apply_test_policy(eng_ui_tests)
add_test(NAME ui COMMAND eng_ui_tests)
