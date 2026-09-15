#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "eng/core/Uuid.hpp"
#include "eng/reflect/Reflect.hpp"
#include "eng/serial/Serial.hpp"

// =============================================================================
// Tipos de teste registrados no reflect (registro em inicialização estática)
// =============================================================================

enum class SerialTestMode { Off, On, Auto };

struct SerialTestVec {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct SerialTestPayload {
    bool flag = false;
    std::int32_t count = 0;
    std::uint64_t stamp = 0;
    float ratio = 0;
    std::string name;
    SerialTestVec dir;
    SerialTestMode mode = SerialTestMode::Off;
};

ENG_REFLECT_BEGIN(SerialTestVec)
    ENG_REFLECT_FIELD(x)
    ENG_REFLECT_FIELD(y)
    ENG_REFLECT_FIELD(z)
ENG_REFLECT_END()

ENG_REFLECT_ENUM_BEGIN(SerialTestMode)
    ENG_REFLECT_ENUM_VALUE(Off)
    ENG_REFLECT_ENUM_VALUE(On)
    ENG_REFLECT_ENUM_VALUE(Auto)
ENG_REFLECT_ENUM_END()

ENG_REFLECT_BEGIN(SerialTestPayload)
    ENG_REFLECT_FIELD(flag)
    ENG_REFLECT_FIELD(count)
    ENG_REFLECT_FIELD(stamp)
    ENG_REFLECT_FIELD(ratio)
    ENG_REFLECT_FIELD(name)
    ENG_REFLECT_FIELD_AS(dir, "SerialTestVec")
    ENG_REFLECT_FIELD_AS(mode, "SerialTestMode")
ENG_REFLECT_END()

namespace {

const eng::reflect::TypeInfo* infoOf(const char* name)
{
    const auto* info = eng::reflect::TypeRegistry::global().find(name);
    REQUIRE(info != nullptr);
    return info;
}

} // namespace

// =============================================================================
// parse/dump com limites
// =============================================================================

TEST_CASE("serial: parse JSON válido, inválido e vazio", "[serial]")
{
    using eng::core::StatusCode;

    // válido (com espaços — RFC 8259 permite)
    {
        const auto r = eng::serial::parseJson("  {\"a\": 1, \"b\": [true, null]}  ");
        REQUIRE(r.ok());
        CHECK(r.value().isObject());
        CHECK(r.value().size() == 2);
        const auto a = r.value().find("a");
        REQUIRE(a.has_value());
        // nlohmann parseia inteiros positivos como unsigned — "qualquer
        // inteiro" = isInteger() || isUnsigned() (semântica do wrapper).
        CHECK((a->isInteger() || a->isUnsigned()));
        CHECK(a->asI64() == 1);
    }

    // inválido
    for (const std::string_view bad :
         {"", "{", "{\"a\":}", "[1,2", "\"sem fecha", "{\"a\" 1}", "tru"}) {
        const auto r = eng::serial::parseJson(bad);
        INFO("input: " << bad);
        CHECK(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }

    // null/true/número soltos são JSON válido
    CHECK(eng::serial::parseJson("null").value().isNull());
    CHECK(eng::serial::parseJson("true").value().isBool());
    CHECK(eng::serial::parseJson("-12.5").value().isNumber());
}

TEST_CASE("serial: limites de tamanho e profundidade (mitigação DoS)",
          "[serial]")
{
    using eng::core::StatusCode;

    // tamanho: limite apertado + entrada maior → erro claro, sem crash
    {
        eng::serial::JsonLimits limits;
        limits.maxTextBytes = 64;
        const std::string big(200, ' ');
        const auto r = eng::serial::parseJson(big, limits);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
        CHECK(r.error().message.find("excede") != std::string::npos);
    }

    // profundidade: 70 níveis com limite 64 → erro; 60 → ok
    {
        const auto nest = [](std::size_t depth) {
            return std::string(depth, '[') + "0" + std::string(depth, ']');
        };
        const auto tooDeep = eng::serial::parseJson(nest(70));
        REQUIRE(tooDeep.isError());
        CHECK(tooDeep.error().code == StatusCode::ParseError);
        CHECK(tooDeep.error().message.find("profundidade") !=
              std::string::npos);

        const auto ok = eng::serial::parseJson(nest(60));
        CHECK(ok.ok());
    }
}

TEST_CASE("serial: dump determinístico e round-trip de texto", "[serial]")
{
    // Ordem de inserção DIFERENTE → dump IDÊNTICO (chaves ordenadas).
    eng::serial::JsonValue a = eng::serial::JsonValue::object();
    a.set("zebra", eng::serial::JsonValue::integer(1));
    a.set("alfa", eng::serial::JsonValue::string("x"));

    eng::serial::JsonValue b = eng::serial::JsonValue::object();
    b.set("alfa", eng::serial::JsonValue::string("x"));
    b.set("zebra", eng::serial::JsonValue::integer(1));

    CHECK(a.dump() == b.dump());
    CHECK(a.dump() == "{\"alfa\":\"x\",\"zebra\":1}");

    // round-trip texto → valor → texto
    const auto parsed = eng::serial::parseJson(a.dump());
    REQUIRE(parsed.ok());
    CHECK(eng::serial::dumpJson(parsed.value()) == a.dump());
}

// =============================================================================
// Binary — primitivas BE com validação de limites
// =============================================================================

TEST_CASE("serial: Binary writer/reader round-trip big-endian", "[serial]")
{
    std::vector<std::byte> buffer;
    eng::serial::BinaryWriter w(buffer);

    const eng::core::Uuid128 uuid = eng::core::Uuid128::generate();
    w.u8(0xAB);
    w.u16BE(0x0102);
    w.u32BE(0x04030201u);
    w.u64BE(0x0807060504030201ull);
    w.i32BE(-123456);
    w.i64BE(-9876543210ll);
    w.f32BE(3.5f);
    w.f64BE(-2.25);
    w.uuid(uuid);
    const std::vector<std::byte> blob{std::byte{1}, std::byte{2}, std::byte{3}};
    w.bytes(blob);

    // Verificação independente do layout BE no stream.
    REQUIRE(buffer.size() == 1 + 2 + 4 + 8 + 4 + 8 + 4 + 8 + 16 + 3);
    CHECK(static_cast<unsigned>(buffer[0]) == 0xAB);
    CHECK(static_cast<unsigned>(buffer[1]) == 0x01);
    CHECK(static_cast<unsigned>(buffer[2]) == 0x02);

    eng::serial::BinaryReader r(buffer);
    CHECK(r.u8().value() == 0xAB);
    CHECK(r.u16BE().value() == 0x0102);
    CHECK(r.u32BE().value() == 0x04030201u);
    CHECK(r.u64BE().value() == 0x0807060504030201ull);
    CHECK(r.i32BE().value() == -123456);
    CHECK(r.i64BE().value() == -9876543210ll);
    CHECK(r.f32BE().value() == 3.5f);
    CHECK(r.f64BE().value() == -2.25);
    CHECK(r.uuid().value() == uuid);
    CHECK(r.bytes(3).value() == blob);
    CHECK(r.atEnd());

    // EOF: leitura além do fim → IOError claro (sem crash/UB).
    const auto over = r.u8();
    REQUIRE(over.isError());
    CHECK(over.error().code == eng::core::StatusCode::IOError);
}

// =============================================================================
// Envelope binário
// =============================================================================

TEST_CASE("serial: envelope round-trip, CRC, magic e versão futura",
          "[serial]")
{
    using eng::core::StatusCode;

    const std::vector<std::byte> payload{std::byte{0xDE}, std::byte{0xAD},
                                         std::byte{0xBE}, std::byte{0xEF}};
    const auto envelope =
        eng::serial::encodeEnvelope(7, 3, payload);

    // round-trip
    {
        const auto decoded = eng::serial::decodeEnvelope(envelope);
        REQUIRE(decoded.ok());
        CHECK(decoded.value().assetType == 7);
        CHECK(decoded.value().payloadVersion == 3);
        CHECK(decoded.value().payload == payload);
    }

    // CRC errado: corrompe 1 byte do payload
    {
        std::vector<std::byte> corrupt = envelope;
        corrupt[eng::serial::kEnvelopeOverhead] = std::byte{0x00};
        const auto r = eng::serial::decodeEnvelope(corrupt);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
        CHECK(r.error().message.find("CRC") != std::string::npos);
    }

    // magic errado
    {
        std::vector<std::byte> corrupt = envelope;
        corrupt[1] = std::byte{'X'};
        const auto r = eng::serial::decodeEnvelope(corrupt);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
        CHECK(r.error().message.find("magic") != std::string::npos);
    }

    // formatVersion futura → NotSupported claro
    {
        std::vector<std::byte> future = envelope;
        future[4] = std::byte{0};
        future[5] = std::byte{0};
        future[6] = std::byte{0};
        future[7] = std::byte{9};
        // CRC recalculado para isolar a rejeição de versão
        std::vector<std::byte> head(future.begin(),
                                    future.end() - sizeof(std::uint32_t));
        const std::uint32_t crc = eng::serial::crc32(head);
        eng::serial::BinaryWriter(future).u32BE(crc);
        const auto r = eng::serial::decodeEnvelope(future);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotSupported);
        CHECK(r.error().message.find("maior que o suportado") !=
              std::string::npos);
    }

    // truncado
    {
        std::vector<std::byte> trunc(envelope.begin(), envelope.begin() + 10);
        const auto r = eng::serial::decodeEnvelope(trunc);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }

    // payloadSize inconsistente
    {
        std::vector<std::byte> lie = envelope;
        lie[12] = std::byte{0};
        lie[13] = std::byte{0};
        lie[14] = std::byte{0};
        lie[15] = std::byte{9}; // size=9 ≠ 4 real
        const auto r = eng::serial::decodeEnvelope(lie);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::ParseError);
    }

    // CRC-32 vetor conhecido: "123456789" → 0xCBF43926
    {
        const char* text = "123456789";
        const auto* bytes = reinterpret_cast<const std::byte*>(text);
        CHECK(eng::serial::crc32({bytes, 9}) == 0xCBF43926u);
    }
}

// =============================================================================
// Migrations — infraestrutura (nenhuma migration REAL ativa; ADR-031)
// =============================================================================

namespace {

class FakeMigration final : public eng::serial::Migration {
public:
    FakeMigration(eng::serial::SchemaVersion from,
                  eng::serial::SchemaVersion to)
        : from_(from), to_(to)
    {
    }

    eng::serial::SchemaVersion from() const noexcept override { return from_; }
    eng::serial::SchemaVersion to() const noexcept override { return to_; }

    eng::core::Result<eng::serial::JsonValue> migrate(
        eng::serial::JsonValue data) const override
    {
        data.set("step" + std::to_string(to_),
                 eng::serial::JsonValue::integer(static_cast<std::int64_t>(to_)));
        return data;
    }

private:
    eng::serial::SchemaVersion from_;
    eng::serial::SchemaVersion to_;
};

} // namespace

TEST_CASE("serial: MigrationRegistry cadeia e validações", "[serial]")
{
    using eng::core::StatusCode;

    eng::serial::MigrationRegistry registry;

    // validações de add
    CHECK(registry.add(std::make_unique<FakeMigration>(2, 1)).isError());
    CHECK(registry.add(std::make_unique<FakeMigration>(3, 3)).isError());
    CHECK(registry.add(nullptr).isError());
    REQUIRE(registry.add(std::make_unique<FakeMigration>(1, 2)).ok());
    CHECK(registry.add(std::make_unique<FakeMigration>(1, 5)).isError());
    REQUIRE(registry.add(std::make_unique<FakeMigration>(2, 4)).ok());
    CHECK(registry.count() == 2);
    CHECK(registry.latestKnown() == 4);

    eng::serial::JsonValue data = eng::serial::JsonValue::object();
    data.set("versão-original", eng::serial::JsonValue::integer(1));

    // cadeia 1 → 4 aplica os dois passos EM ORDEM
    {
        const auto r = registry.migrateTo(data, 1, 4);
        REQUIRE(r.ok());
        CHECK(r.value().find("step2")->asI64() == 2);
        CHECK(r.value().find("step4")->asI64() == 4);
        CHECK(r.value().find("versão-original")->asI64() == 1);
    }

    // alvo intermediário: 1 → 2
    {
        const auto r = registry.migrateTo(data, 1, 2);
        REQUIRE(r.ok());
        CHECK(r.value().find("step2").has_value());
        CHECK_FALSE(r.value().find("step4").has_value());
    }

    // downgrade → InvalidArgument
    {
        const auto r = registry.migrateTo(data, 4, 1);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::InvalidArgument);
    }

    // sem link na cadeia (2 → 3 não existe) → NotSupported claro
    {
        const auto r = registry.migrateTo(data, 2, 3);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotSupported);
    }

    // passo que ultrapassa o alvo (leitor mais antigo que o arquivo)
    {
        const auto r = registry.migrateTo(data, 1, 3);
        REQUIRE(r.isError());
        CHECK(r.error().code == StatusCode::NotSupported);
    }

    // current == target: no-op
    {
        const auto r = registry.migrateTo(data, 4, 4);
        REQUIRE(r.ok());
        CHECK_FALSE(r.value().find("step4").has_value());
    }
}

// =============================================================================
// StructCodec — reflect-driven, determinístico, estrito
// =============================================================================

TEST_CASE("serial: StructCodec round-trip completo com todos os kinds",
          "[serial]")
{
    SerialTestPayload original;
    original.flag = true;
    original.count = -42;
    original.stamp = 0xFFFF'FFFF'FFFF'0001ull;
    original.ratio = 0.25f;
    original.name = "teste sério";
    original.dir = {1.5f, -2.5f, 0.0f};
    original.mode = SerialTestMode::Auto;

    const auto encoded =
        eng::serial::encodeStruct(&original, *infoOf("SerialTestPayload"));
    REQUIRE(encoded.ok());

    // enum vira NOME; u64 grande (> i64 max) preservado; floats preservados
    const std::string text = eng::serial::dumpJson(encoded.value());
    CHECK(text.find("\"mode\":\"Auto\"") != std::string::npos);
    // 0xFFFF'FFFF'FFFF'0001 = 18446744073709486081
    CHECK(text.find("18446744073709486081") != std::string::npos);

    SerialTestPayload decoded;
    const auto status =
        eng::serial::decodeStruct(encoded.value(), &decoded,
                                  *infoOf("SerialTestPayload"));
    REQUIRE(status.ok());

    CHECK(decoded.flag == original.flag);
    CHECK(decoded.count == original.count);
    CHECK(decoded.stamp == original.stamp);
    CHECK(decoded.ratio == original.ratio);
    CHECK(decoded.name == original.name);
    CHECK(decoded.dir.x == original.dir.x);
    CHECK(decoded.dir.y == original.dir.y);
    CHECK(decoded.dir.z == original.dir.z);
    CHECK(decoded.mode == original.mode);

    // Determinismo: reencode do decodificado → bytes idênticos.
    const auto reencoded =
        eng::serial::encodeStruct(&decoded, *infoOf("SerialTestPayload"));
    REQUIRE(reencoded.ok());
    CHECK(eng::serial::dumpJson(reencoded.value()) == text);
}

TEST_CASE("serial: StructCodec estrito — ausente, tipo errado, desconhecido",
          "[serial]")
{
    using eng::core::StatusCode;

    SerialTestPayload payload;

    // campo obrigatório ausente
    {
        const auto r = eng::serial::parseJson(
            "{\"flag\":true,\"count\":1,\"stamp\":2,\"ratio\":0.5,"
            "\"name\":\"n\",\"dir\":{\"x\":0,\"y\":0,\"z\":0},"
            "\"mode\":\"On\"}");
        REQUIRE(r.ok());
        auto value = r.value();
        value.set("name", eng::serial::JsonValue::null());
        // remove um campo reconstruindo o objeto sem ele
        eng::serial::JsonValue rebuilt = eng::serial::JsonValue::object();
        rebuilt.set("flag", eng::serial::JsonValue::boolean(true));
        rebuilt.set("count", eng::serial::JsonValue::integer(1));
        rebuilt.set("stamp", eng::serial::JsonValue::uinteger(2));
        rebuilt.set("ratio", eng::serial::JsonValue::real(0.5));
        rebuilt.set("dir", eng::serial::JsonValue::object());
        rebuilt.set("mode", eng::serial::JsonValue::string("On"));
        const auto d = eng::serial::decodeStruct(
            rebuilt, &payload, *infoOf("SerialTestPayload"));
        REQUIRE(d.isError());
        CHECK(d.error().code == StatusCode::ParseError);
        CHECK(d.error().message.find("ausente: 'name'") != std::string::npos);
    }

    // tipo errado (string onde espera bool)
    {
        eng::serial::JsonValue value = eng::serial::JsonValue::object();
        value.set("flag", eng::serial::JsonValue::string("sim"));
        const auto d = eng::serial::decodeStruct(
            value, &payload, *infoOf("SerialTestPayload"));
        REQUIRE(d.isError());
        CHECK(d.error().code == StatusCode::ParseError);
    }

    // enumerador inexistente (objeto COMPLETO para isolar a rejeição)
    {
        eng::serial::JsonValue value = eng::serial::JsonValue::object();
        value.set("flag", eng::serial::JsonValue::boolean(false));
        value.set("count", eng::serial::JsonValue::integer(0));
        value.set("stamp", eng::serial::JsonValue::uinteger(0));
        value.set("ratio", eng::serial::JsonValue::real(0.0));
        value.set("name", eng::serial::JsonValue::string(""));
        eng::serial::JsonValue dir = eng::serial::JsonValue::object();
        dir.set("x", eng::serial::JsonValue::real(0.0));
        dir.set("y", eng::serial::JsonValue::real(0.0));
        dir.set("z", eng::serial::JsonValue::real(0.0));
        value.set("dir", std::move(dir));
        value.set("mode", eng::serial::JsonValue::string("ModoNovo"));
        const auto d = eng::serial::decodeStruct(
            value, &payload, *infoOf("SerialTestPayload"));
        REQUIRE(d.isError());
        CHECK(d.error().message.find("ModoNovo") != std::string::npos);
    }

    // chaves desconhecidas são IGNORADAS (evolução aditiva — ADR-031)
    {
        eng::serial::JsonValue value = eng::serial::JsonValue::object();
        value.set("flag", eng::serial::JsonValue::boolean(true));
        value.set("count", eng::serial::JsonValue::integer(7));
        value.set("stamp", eng::serial::JsonValue::uinteger(0));
        value.set("ratio", eng::serial::JsonValue::real(1.0));
        value.set("name", eng::serial::JsonValue::string("ok"));
        eng::serial::JsonValue dir = eng::serial::JsonValue::object();
        dir.set("x", eng::serial::JsonValue::real(0.0));
        dir.set("y", eng::serial::JsonValue::real(0.0));
        dir.set("z", eng::serial::JsonValue::real(0.0));
        value.set("dir", std::move(dir));
        value.set("mode", eng::serial::JsonValue::string("Off"));
        value.set("campo-do-futuro", eng::serial::JsonValue::integer(99));

        SerialTestPayload decoded;
        REQUIRE(eng::serial::decodeStruct(value, &decoded,
                                          *infoOf("SerialTestPayload"))
                    .ok());
        CHECK(decoded.count == 7);
    }
}

TEST_CASE("serial: StructCodec rejeita NaN/Inf no encode", "[serial]")
{
    SerialTestPayload payload;
    payload.ratio = std::numeric_limits<float>::quiet_NaN();

    const auto r =
        eng::serial::encodeStruct(&payload, *infoOf("SerialTestPayload"));
    REQUIRE(r.isError());
    CHECK(r.error().code == eng::core::StatusCode::ParseError);
    CHECK(r.error().message.find("não finito") != std::string::npos);
}
