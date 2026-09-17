add_library(eng_input STATIC
    src/Input.cpp
)
add_library(eng::input ALIAS eng_input)
target_include_directories(eng_input PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_input PUBLIC cxx_std_20)
eng_apply_warnings(eng_input)
eng_apply_sanitizers(eng_input)
eng_apply_lto(eng_input)
target_compile_options(eng_input PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_input PUBLIC eng::core eng::math eng::serial)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_input_tests tests/InputTests.cpp)
target_link_libraries(eng_input_tests PRIVATE eng::input Catch2::Catch2WithMain)
eng_apply_test_policy(eng_input_tests)
add_test(NAME input COMMAND eng_input_tests)
