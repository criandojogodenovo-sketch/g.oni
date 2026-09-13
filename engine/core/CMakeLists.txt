# eng::core — Result, Error, Span, Version (§17.1 item 16)
eng_add_module(core
    src/Error.cpp
    src/Version.cpp
)

if(ENG_BUILD_TESTS)
    add_executable(eng_core_tests
        tests/ResultTests.cpp
        tests/VersionTests.cpp
        tests/SpanTests.cpp
    )
    target_link_libraries(eng_core_tests PRIVATE eng::core Catch2::Catch2WithMain)
    eng_apply_test_policy(eng_core_tests)
    add_test(NAME core COMMAND eng_core_tests)
endif()
