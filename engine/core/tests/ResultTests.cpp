#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>

#include "eng/core/Core.hpp"

namespace {

using eng::core::Error;
using eng::core::makeUnexpected;
using eng::core::Result;
using eng::core::StatusCode;

eng::core::Result<int> parseIntish(bool fail) {
    if (fail) {
        return makeUnexpected(Error{StatusCode::ParseError, "falha proposital"});
    }
    return 42;
}

} // namespace

// ---------------------------------------------------------------------------
// Result<T>: estado, acesso, valor-padrão
// ---------------------------------------------------------------------------

TEST_CASE("Result guarda valor de sucesso", "[core][result]") {
    const Result<int> r = 42;
    CHECK(r.ok());
    CHECK_FALSE(r.isError());
    CHECK(r.value() == 42);
    CHECK(r.valueOr(0) == 42);
}

TEST_CASE("Result guarda erro e valueOr devolve fallback", "[core][result]") {
    const Result<int> r = makeUnexpected(Error{StatusCode::ParseError, "nope"});
    CHECK(r.isError());
    CHECK_FALSE(r.ok());
    CHECK(r.error().code == StatusCode::ParseError);
    CHECK(r.error().message == "nope");
    CHECK(r.valueOr(-1) == -1);
}

TEST_CASE("Result converte explicitamente para bool", "[core][result]") {
    CHECK(static_cast<bool>(Result<int>(1)));
    CHECK_FALSE(static_cast<bool>(Result<int>(makeUnexpected(Error{}))));
}

TEST_CASE("Result constroi a partir de função que falha ou acerta", "[core][result]") {
    const Result<int> ok = parseIntish(false);
    const Result<int> err = parseIntish(true);
    CHECK(ok.value() == 42);
    CHECK(err.error().code == StatusCode::ParseError);
    CHECK(err.error().message == "falha proposital");
}

TEST_CASE("Result copia preservando estado", "[core][result]") {
    const Result<std::string> original = std::string("dados");
    const Result<std::string> copia = original;
    CHECK(copia.ok());
    CHECK(copia.value() == "dados");
    CHECK(original.value() == "dados"); // original permanece intacto
}

TEST_CASE("Result move transfere o valor", "[core][result]") {
    Result<std::string> original = std::string("transferido");
    const Result<std::string> destino = std::move(original);
    CHECK(destino.ok());
    CHECK(destino.value() == "transferido");
}

TEST_CASE("Result atribuição substitui o estado ativo", "[core][result]") {
    Result<std::string> r = std::string("primeiro");
    r = Result<std::string>(makeUnexpected(Error{StatusCode::Unknown, "x"}));
    CHECK(r.isError());
    CHECK(r.error().message == "x");
    r = std::string("segundo");
    CHECK(r.ok());
    CHECK(r.value() == "segundo");
}

TEST_CASE("Result deduz tipos (CTAD)", "[core][result]") {
    const Result deduzido{7};
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(deduzido)>, Result<int, Error>>);
    CHECK(deduzido.value() == 7);

    const Result statusErrado = makeUnexpected(Error{StatusCode::NotFound, "sumiu"});
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(statusErrado)>, Result<void, Error>>);
    CHECK(statusErrado.isError());
}

// ---------------------------------------------------------------------------
// Result<void>: status puro
// ---------------------------------------------------------------------------

TEST_CASE("Result<void> por padrão é sucesso", "[core][result]") {
    const Result<void> r;
    CHECK(r.ok());
    CHECK(static_cast<bool>(r));
}

TEST_CASE("Result<void> carrega erro", "[core][result]") {
    const Result<void> r = makeUnexpected(Error{StatusCode::IOError, "disco"});
    CHECK(r.isError());
    CHECK(r.error().code == StatusCode::IOError);
    CHECK(r.error().message == "disco");
}

// ---------------------------------------------------------------------------
// Error
// ---------------------------------------------------------------------------

TEST_CASE("Error expõe código e nome estável", "[core][error]") {
    const Error e{StatusCode::ParseError, "detalhe"};
    CHECK(e.code == StatusCode::ParseError);
    CHECK(e.message == "detalhe");
    CHECK(e.codeName() == "ParseError");

    const Error padrao{};
    CHECK(padrao.code == StatusCode::Unknown);
    CHECK(padrao.message.empty());
    CHECK(padrao.codeName() == "Unknown");
}

TEST_CASE("Error compara por igualdade padrão", "[core][error]") {
    const Error a{StatusCode::NotFound, "a"};
    const Error b{StatusCode::NotFound, "a"};
    const Error c{StatusCode::NotFound, "b"};
    CHECK(a == b);
    CHECK_FALSE(a == c);
}
