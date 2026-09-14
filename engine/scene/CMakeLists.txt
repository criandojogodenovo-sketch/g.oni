# eng::scene — hierarquia de nós sobre o ECS (FASE 2)
# (missão §B.5; ADR-025)
eng_add_module(scene
    src/Scene.cpp
)
# Arestas do grafo da FASE 2 (missão §B.0): scene sobre core, ecs e math.
# ecs/math são PUBLIC porque headers públicos de scene incluem os dois.
# core declara a camada (nenhum símbolo consumido nesta fase).
target_link_libraries(eng_scene PUBLIC eng::ecs eng::math PRIVATE eng::core)

if(ENG_BUILD_TESTS)
    add_executable(eng_scene_tests
        tests/SceneTests.cpp
    )
    target_link_libraries(eng_scene_tests PRIVATE eng::scene Catch2::Catch2WithMain)
    eng_apply_test_policy(eng_scene_tests)
    add_test(NAME scene COMMAND eng_scene_tests)
endif()
