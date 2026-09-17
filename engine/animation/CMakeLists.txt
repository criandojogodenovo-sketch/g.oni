add_library(eng_animation STATIC
    src/Animation.cpp
)
add_library(eng::animation ALIAS eng_animation)
target_include_directories(eng_animation PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_animation PUBLIC cxx_std_20)
eng_apply_warnings(eng_animation)
eng_apply_sanitizers(eng_animation)
eng_apply_lto(eng_animation)
target_compile_options(eng_animation PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_animation PUBLIC eng::core eng::math eng::scene eng::reflect)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_animation_tests tests/AnimationTests.cpp)
target_link_libraries(eng_animation_tests PRIVATE eng::animation Catch2::Catch2WithMain)
eng_apply_test_policy(eng_animation_tests)
add_test(NAME animation COMMAND eng_animation_tests)
