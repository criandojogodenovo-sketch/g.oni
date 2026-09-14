#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "eng/reflect/Reflect.hpp"

// =============================================================================
// Tipos de teste — registrados por inicialização estática (antes de main),
// exatamente como o uso real em código de integração.
// =============================================================================

namespace {

struct Vec3Like {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class Color : std::uint8_t {
    Red = 0,
    Green = 2,
    Blue = 7,
};

enum Estado : int { Sol = 1, Chuva = 5 }; // enum não-escopado

struct Material {
    Vec3Like albedo{};
    float roughness = 0.5f;
    int flags = 0;
    std::string nome{};
};

struct OpaqueBlob {
    std::array<std::uint8_t, 16> bytes{};
};

} // namespace

// Integração de nomes canônicos para os tipos de teste (extensão prevista).
namespace eng::reflect {
template<>
struct PrimitiveName<Vec3Like> {
    static constexpr std::string_view value = "test::Vec3Like";
};
} // namespace eng::reflect

ENG_REFLECT_BEGIN(Vec3Like)
    ENG_REFLECT_FIELD(x)
    ENG_REFLECT_FIELD(y)
    ENG_REFLECT_FIELD(z)
ENG_REFLECT_END()

ENG_REFLECT(OpaqueBlob)

ENG_REFLECT_ENUM_BEGIN(Color)
    ENG_REFLECT_ENUM_VALUE(Red)
    ENG_REFLECT_ENUM_VALUE(Green)
    ENG_REFLECT_ENUM_VALUE(Blue)
ENG_REFLECT_ENUM_END()

ENG_REFLECT_ENUM_BEGIN(Estado)
    ENG_REFLECT_ENUM_VALUE(Sol)
    ENG_REFLECT_ENUM_VALUE(Chuva)
ENG_REFLECT_ENUM_END()

ENG_REFLECT_BEGIN(Material)
    ENG_REFLECT_FIELD(albedo)
    ENG_REFLECT_FIELD(roughness)
    ENG_REFLECT_FIELD(flags)
    ENG_REFLECT_FIELD(nome)
ENG_REFLECT_END()

// Determinismo do id em tempo de compilação (FNV-1a 64).
static_assert(eng::reflect::typeIdOf("a") != eng::reflect::typeIdOf("b"));
static_assert(eng::reflect::typeIdOf("test::Vec3Like")
              == eng::reflect::typeIdOf("test::Vec3Like"));

// =============================================================================
// Testes
// =============================================================================

TEST_CASE("reflect: tipos embutidos registrados no global()", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    const eng::reflect::TypeInfo* f32 = registry.find("f32");
    REQUIRE(f32 != nullptr);
    CHECK(f32->kind == eng::reflect::TypeKind::Primitive);
    CHECK(f32->size == sizeof(float));
    CHECK(f32->alignment == alignof(float));

    const eng::reflect::TypeInfo* boolean = registry.find("bool");
    REQUIRE(boolean != nullptr);
    CHECK(boolean->size == sizeof(bool));

    const eng::reflect::TypeInfo* str = registry.find("string");
    REQUIRE(str != nullptr);
    CHECK(str->size == sizeof(std::string));

    CHECK(registry.find("nao::existe") == nullptr);
}

TEST_CASE("reflect: registro de struct com propriedades", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    const eng::reflect::TypeInfo* material = registry.find("Material");
    REQUIRE(material != nullptr);
    CHECK(material->kind == eng::reflect::TypeKind::Struct);
    CHECK(material->size == sizeof(Material));
    CHECK(material->alignment == alignof(Material));
    CHECK(material->id == eng::reflect::typeIdOf("Material"));

    REQUIRE(material->properties.size() == 4);

    const auto& albedo = material->properties[0];
    CHECK(albedo.name == "albedo");
    CHECK(albedo.offset == offsetof(Material, albedo));
    CHECK(albedo.typeName == "test::Vec3Like");
    CHECK(albedo.typeId == eng::reflect::typeIdOf("test::Vec3Like"));

    CHECK(material->properties[1].name == "roughness");
    CHECK(material->properties[1].offset == offsetof(Material, roughness));
    CHECK(material->properties[1].typeId == eng::reflect::typeIdOf("f32"));

    CHECK(material->properties[2].name == "flags");
    CHECK(material->properties[2].typeId == eng::reflect::typeIdOf("i32"));

    CHECK(material->properties[3].name == "nome");
    CHECK(material->properties[3].typeId == eng::reflect::typeIdOf("string"));

    // Campo de tipo registrado ANTES (mesma TU, init estática top-down):
    // typeId já resolvido no momento do registro de Material.
    const auto* vec = registry.find("test::Vec3Like");
    REQUIRE(vec != nullptr);
    CHECK(vec->properties.size() == 3);
    CHECK(vec->properties[0].name == "x");
    CHECK(vec->properties[0].typeId == eng::reflect::typeIdOf("f32"));
}

TEST_CASE("reflect: lookup inexistente devolve nullptr", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();
    CHECK(registry.find("tipo::ausente") == nullptr);
    CHECK(registry.find(eng::reflect::TypeId{0xDEADBEEFull}) == nullptr);
    CHECK(registry.find(eng::reflect::typeIdOf("nao::existe")) == nullptr);
}

TEST_CASE("reflect: lookup por id opaco", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    const auto id = eng::reflect::typeIdOf("Material");
    const auto* byId = registry.find(id);
    REQUIRE(byId != nullptr);
    CHECK(byId->name == "Material");

    const auto* byName = registry.find("Material");
    REQUIRE(byName != nullptr);
    CHECK(byId == byName); // mesma entrada
}

TEST_CASE("reflect: enum escopado com valores e subjacente", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    const auto* color = registry.find("Color");
    REQUIRE(color != nullptr);
    CHECK(color->kind == eng::reflect::TypeKind::Enum);
    CHECK(color->size == sizeof(Color));
    CHECK(color->underlyingTypeId == eng::reflect::typeIdOf("u8"));

    REQUIRE(color->enumerators.size() == 3);
    CHECK(color->enumerators[0].name == "Red");
    CHECK(color->enumerators[0].value == 0);
    CHECK(color->enumerators[1].name == "Green");
    CHECK(color->enumerators[1].value == 2);
    CHECK(color->enumerators[2].name == "Blue");
    CHECK(color->enumerators[2].value == 7);
}

TEST_CASE("reflect: enum não-escopado", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    const auto* estado = registry.find("Estado");
    REQUIRE(estado != nullptr);
    REQUIRE(estado->enumerators.size() == 2);
    CHECK(estado->enumerators[0].name == "Sol");
    CHECK(estado->enumerators[0].value == 1);
    CHECK(estado->enumerators[1].name == "Chuva");
    CHECK(estado->enumerators[1].value == 5);
}

TEST_CASE("reflect: re-registro idempotente (primeiro vence)", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();
    const auto before = registry.count();

    const auto id1 = registry.registerType(
        eng::reflect::TypeKind::Struct, "Material",
        sizeof(Material), alignof(Material));
    const auto id2 = registry.registerType(
        eng::reflect::TypeKind::Primitive, "Material",
        1, 1); // metadados diferentes devem ser IGNORADOS

    CHECK(id1 == id2);
    CHECK(registry.count() == before); // nada novo inserido
    const auto* material = registry.find("Material");
    REQUIRE(material != nullptr);
    CHECK(material->kind == eng::reflect::TypeKind::Struct); // original intacto
    CHECK(material->size == sizeof(Material));
}

TEST_CASE("reflect: registro em runtime e lifetime além do escopo", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();
    const auto before = registry.count();

    const eng::reflect::TypeInfo* captured = nullptr;
    {
        struct Efemero {
            int a = 0;
            float b = 0.0f;
        };
        // Registro com API direta dentro de um escopo:
        const eng::reflect::TypeId id = registry.registerType(
            eng::reflect::TypeKind::Struct, "test::Efemero",
            sizeof(Efemero), alignof(Efemero),
            std::vector<eng::reflect::PropertyDesc>{
                {"a", offsetof(Efemero, a), "i32"},
                {"b", offsetof(Efemero, b), "f32"},
            });
        CHECK(id == eng::reflect::typeIdOf("test::Efemero"));
        captured = registry.find("test::Efemero");
        REQUIRE(captured != nullptr);
    } // escopo termina: o registro precisa sobreviver

    REQUIRE(captured != nullptr);
    CHECK(captured->properties.size() == 2);
    CHECK(captured->properties[0].name == "a");
    CHECK(captured->properties[1].name == "b");
    CHECK(registry.count() == before + 1);

    // Re-consulta após o escopo: mesmos dados, ponteiro estável.
    const auto* again = registry.find("test::Efemero");
    REQUIRE(again != nullptr);
    CHECK(again == captured); // ponteiro estável (deque + índices)
    CHECK(again->properties[0].name == "a");
    CHECK(again->properties[0].offset == 0);
}

TEST_CASE("reflect: leitura concorrente do registry", "[reflect][threads]") {
    auto& registry = eng::reflect::TypeRegistry::global();

    constexpr std::string_view names[] = {
        "Material", "Color", "test::Vec3Like", "f32", "string", "Estado",
    };
    const eng::reflect::TypeInfo* expected[std::size(names)];
    for (std::size_t i = 0; i < std::size(names); ++i) {
        expected[i] = registry.find(names[i]);
        REQUIRE(expected[i] != nullptr);
    }

    constexpr int kThreads = 8;
    constexpr int kLookupsPerThread = 20000;
    std::atomic<int> mismatches{0};
    std::atomic<int> totalLookups{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kLookupsPerThread; ++i) {
                const auto idx = static_cast<std::size_t>(i % std::size(names));
                const auto* info = registry.find(names[idx]);
                if (info != expected[idx]) {
                    ++mismatches;
                }
                ++totalLookups;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    CHECK(totalLookups.load() == kThreads * kLookupsPerThread);
    CHECK(mismatches.load() == 0);
}

TEST_CASE("reflect: count cresce com registros distintos", "[reflect]") {
    auto& registry = eng::reflect::TypeRegistry::global();
    const auto before = registry.count();

    (void)registry.registerType(eng::reflect::TypeKind::Primitive, "test::Extra1", 4, 4);
    CHECK(registry.count() == before + 1);
    (void)registry.registerType(eng::reflect::TypeKind::Primitive, "test::Extra2", 8, 8);
    CHECK(registry.count() == before + 2);
}

// ÚLTIMO teste do binário: esvazia o registry (invalida ponteiros).
TEST_CASE("reflect: clear esvazia tudo", "[reflect][zz-last]") {
    auto& registry = eng::reflect::TypeRegistry::global();
    REQUIRE(registry.count() > 0);
    registry.clear();
    CHECK(registry.count() == 0);
    CHECK(registry.find("f32") == nullptr);
    CHECK(registry.find("Material") == nullptr);
    CHECK(registry.find(eng::reflect::typeIdOf("bool")) == nullptr);
}
