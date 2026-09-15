#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "eng/fs/NativeFileSystem.hpp"
#include "eng/platform/Platform.hpp"

// =============================================================================
// PlatformInfo — determinístico por binário
// =============================================================================

TEST_CASE("platform: PlatformInfo fatos estáticos", "[platform]")
{
    const auto info = eng::platform::PlatformInfo::current();

    CHECK_FALSE(info.name.empty());
    CHECK_FALSE(info.arch.empty());

    // Endianness verificada de forma INDEPENDENTE: sonda de bytes real
    // (não é tautologia da macro do compilador).
    const std::uint16_t probe = 0x0102;
    const bool littleByProbe =
        static_cast<const unsigned char*>(
            static_cast<const void*>(&probe))[0] == 0x02;
    CHECK((info.endianness ==
           eng::platform::Endianness::Little) == littleByProbe);

    // buildType/sanitizers consistentes com o build do TESTE (macro corrente).
#if defined(NDEBUG)
    CHECK(info.buildType == eng::platform::BuildType::Release);
#else
    CHECK(info.buildType == eng::platform::BuildType::Debug);
#endif
#if defined(__SANITIZE_ADDRESS__)
    CHECK(info.addressSanitizer);
#else
    CHECK_FALSE(info.addressSanitizer);
#endif
#if defined(__SANITIZE_UNDEFINED__)
    CHECK(info.undefinedSanitizer);
#else
    CHECK_FALSE(info.undefinedSanitizer);
#endif
}

// =============================================================================
// PlatformPaths — raízes não-vazias e coerentes
// =============================================================================

TEST_CASE("platform: PlatformPaths raízes não-vazias e localizáveis",
          "[platform]")
{
    const auto paths =
        eng::platform::PlatformPaths::detect("eng-tests");

    CHECK_FALSE(paths.userDataRoot.isEmpty());
    CHECK_FALSE(paths.cacheRoot.isEmpty());
    CHECK_FALSE(paths.tempRoot.isEmpty());
    CHECK_FALSE(paths.executableRoot.isEmpty());

    // userData/cache segregam por app (sufixo appName).
    CHECK(paths.userDataRoot.str().find("eng-tests") != std::string::npos);
    CHECK(paths.cacheRoot.str().find("eng-tests") != std::string::npos);

    // executableRoot é o pai do executável de teste REAL.
    const auto exe = eng::platform::ProcessInfo::currentExecutablePath();
    REQUIRE(exe.ok());
    CHECK(paths.executableRoot == exe.value().parent());

    // tempRoot existe de fato no filesystem (verificação via eng::fs).
    eng::fs::NativeFileSystem fs;
    const auto tempExists = fs.exists(paths.tempRoot);
    REQUIRE(tempExists.ok());
    CHECK(tempExists.value());
}

TEST_CASE("platform: currentExecutablePath resolve o binário corrente",
          "[platform]")
{
    const auto exe = eng::platform::ProcessInfo::currentExecutablePath();
    REQUIRE(exe.ok());
    CHECK(exe.value().isAbsolute());
    CHECK_FALSE(exe.value().filename().isEmpty());

    // O binário existe de verdade (este mesmo teste em execução).
    eng::fs::NativeFileSystem fs;
    const auto exists = fs.exists(exe.value());
    REQUIRE(exists.ok());
    CHECK(exists.value());
}

// =============================================================================
// Environment — round-trip com nome único
// =============================================================================

TEST_CASE("platform: Environment get/set/unset", "[platform]")
{
    const std::string var = "ENG_PLATFORM_TEST_VAR_"
                            + std::to_string(::getpid());

    CHECK_FALSE(eng::platform::Environment::get(var).has_value());

    REQUIRE(eng::platform::Environment::set(var, "valor-1"));
    const auto got = eng::platform::Environment::get(var);
    REQUIRE(got.has_value());
    CHECK(*got == "valor-1");

    // overwrite=false preserva; true substitui.
    REQUIRE(eng::platform::Environment::set(var, "valor-2", false));
    CHECK(*eng::platform::Environment::get(var) == "valor-1");
    REQUIRE(eng::platform::Environment::set(var, "valor-2", true));
    CHECK(*eng::platform::Environment::get(var) == "valor-2");

    REQUIRE(eng::platform::Environment::unset(var));
    CHECK_FALSE(eng::platform::Environment::get(var).has_value());
    CHECK_FALSE(eng::platform::Environment::unset(var)); // já removida
}

TEST_CASE("platform: Environment rejeita nomes inválidos", "[platform]")
{
    CHECK_FALSE(eng::platform::Environment::get("").has_value());
    CHECK_FALSE(eng::platform::Environment::set("", "x"));
    CHECK_FALSE(eng::platform::Environment::set("NOME=ERRADO", "x"));
    CHECK_FALSE(eng::platform::Environment::unset(""));
}
