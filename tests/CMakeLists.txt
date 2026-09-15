# =============================================================================
# tests/ — integração fim-a-fim (FASE 3, missão §2.8)
#
# Único lugar do grafo autorizado a depender de eng::scene JUNTO com os
# módulos de persistência (missão §5.1: "nenhum módulo depende de scene
# exceto testes de integração em tests/"). Convenção NOVA de diretório —
# os testes UNITÁRIOS continuam em engine/<mod>/tests/.
# =============================================================================
if(ENG_BUILD_TESTS)
    add_executable(eng_integration_tests
        Phase3IntegrationTests.cpp
    )
    target_link_libraries(eng_integration_tests
        PRIVATE eng::project eng::scene eng::assets eng::fs eng::serial
                Catch2::Catch2WithMain
    )
    eng_apply_test_policy(eng_integration_tests)
    add_test(NAME integration COMMAND eng_integration_tests)
endif()
