add_library(eng_physics STATIC
    src/Physics.cpp
)
add_library(eng::physics ALIAS eng_physics)
target_include_directories(eng_physics PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_physics PUBLIC cxx_std_20)
eng_apply_warnings(eng_physics)
eng_apply_sanitizers(eng_physics)
eng_apply_lto(eng_physics)
target_compile_options(eng_physics PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_physics PUBLIC eng::core eng::math eng::scene eng::reflect)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_physics_tests tests/PhysicsTests.cpp)
target_link_libraries(eng_physics_tests PRIVATE eng::physics Catch2::Catch2WithMain)
eng_apply_test_policy(eng_physics_tests)
add_test(NAME physics COMMAND eng_physics_tests)
