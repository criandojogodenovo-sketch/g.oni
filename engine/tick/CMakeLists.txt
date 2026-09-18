# eng::tick — arquitetura de Tick: fases, agendador e ticks concretos
# (evolução P0-5, ADR-051). TOPO da pilha de gameplay: compõe física,
# animação e partículas sobre a cena — o futuro runtime de bundles
# reutiliza exatamente este frame.
eng_add_module(tick
    src/Tick.cpp
    src/Camera.cpp
)
# scene PUBLIC (headers usam Scene&/Scene.hpp). physics/animation/particles
# PRIVATE: só os .cpp dos ticks concretos incluem (o scheduler em si é
# livre). reflect: ENG_REFLECT da CameraData (header — PUBLIC).
target_link_libraries(eng_tick
    PUBLIC eng::core eng::scene eng::physics eng::animation eng::particles eng::reflect
    PRIVATE eng::log
)

if(ENG_BUILD_TESTS)
    add_executable(eng_tick_tests
        tests/TickTests.cpp
        tests/CameraTests.cpp
    )
    target_link_libraries(eng_tick_tests PRIVATE eng::tick Catch2::Catch2WithMain)
    eng_apply_test_policy(eng_tick_tests)
    add_test(NAME tick COMMAND eng_tick_tests)
endif()
