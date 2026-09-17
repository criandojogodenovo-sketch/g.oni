add_library(eng_particles STATIC
    src/Particles.cpp
)
add_library(eng::particles ALIAS eng_particles)
target_include_directories(eng_particles PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_particles PUBLIC cxx_std_20)
eng_apply_warnings(eng_particles)
eng_apply_sanitizers(eng_particles)
eng_apply_lto(eng_particles)
target_compile_options(eng_particles PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_particles PUBLIC eng::core eng::math eng::scene eng::reflect)

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_particles_tests tests/ParticlesTests.cpp)
target_link_libraries(eng_particles_tests PRIVATE eng::particles Catch2::Catch2WithMain)
eng_apply_test_policy(eng_particles_tests)
add_test(NAME particles COMMAND eng_particles_tests)
