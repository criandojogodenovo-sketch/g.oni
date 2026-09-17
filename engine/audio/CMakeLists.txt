add_library(eng_audio STATIC
    src/Audio.cpp
    src/Wav.cpp
    src/AAudioBackend.cpp   # Linux: TU vazia (guard __ANDROID__); APK: real
)
add_library(eng::audio ALIAS eng_audio)
target_include_directories(eng_audio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(eng_audio PUBLIC cxx_std_20)
eng_apply_warnings(eng_audio)
eng_apply_sanitizers(eng_audio)
eng_apply_lto(eng_audio)
# Mesa de áudio usa std::mutex (não é exceção — é sincronização §D7).
target_compile_options(eng_audio PRIVATE ${ENG_RUNTIME_FLAGS})
target_link_libraries(eng_audio PUBLIC eng::core eng::fs eng::serial)
# Android: dl para dlopen do libaaudio (padrão dos backends gráficos).
if(ANDROID)
    target_link_libraries(eng_audio PUBLIC dl)
endif()

if(NOT ENG_BUILD_TESTS)
    return()
endif()

add_executable(eng_audio_tests tests/AudioTests.cpp)
target_link_libraries(eng_audio_tests PRIVATE eng::audio eng::fs Catch2::Catch2WithMain)
eng_apply_test_policy(eng_audio_tests)
add_test(NAME audio COMMAND eng_audio_tests)
