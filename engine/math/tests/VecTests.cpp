#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "eng/math/Math.hpp"

namespace {

using eng::math::Vec2;
using eng::math::Vec3;
using eng::math::Vec4;

Catch::Approx close(float v) { return Catch::Approx(v).epsilon(0.0001); }

} // namespace

// Verificação em tempo de compilação das operações constexpr.
static_assert(Vec2{1.0f, 2.0f} + Vec2{3.0f, 4.0f} == Vec2{4.0f, 6.0f});
static_assert(Vec3{1.0f, 0.0f, 0.0f}.cross(Vec3{0.0f, 1.0f, 0.0f}) == Vec3{0.0f, 0.0f, 1.0f});
static_assert(Vec3{2.0f, 0.0f, 0.0f}.lengthSquared() == 4.0f);
static_assert(Vec4{1.0f, 1.0f, 1.0f, 1.0f}.dot(Vec4{1.0f, 2.0f, 3.0f, 4.0f}) == 10.0f);
static_assert(2.0f * Vec2{1.0f, 2.0f} == Vec2{2.0f, 4.0f});

TEST_CASE("Vec2 aritmética básica", "[math][vec2]") {
    const Vec2 a{1.0f, 2.0f};
    const Vec2 b{3.0f, -4.0f};

    CHECK(a + b == Vec2{4.0f, -2.0f});
    CHECK(a - b == Vec2{-2.0f, 6.0f});
    CHECK(-a == Vec2{-1.0f, -2.0f});
    CHECK(a * 2.0f == Vec2{2.0f, 4.0f});
    CHECK(a / 2.0f == Vec2{0.5f, 1.0f});
    CHECK(a == Vec2{1.0f, 2.0f});
    CHECK(a != b);
}

TEST_CASE("Vec2 produto escalar e norma", "[math][vec2]") {
    const Vec2 a{3.0f, 4.0f};
    CHECK(a.dot(Vec2{1.0f, 1.0f}) == 7.0f);
    CHECK(a.lengthSquared() == 25.0f);
    CHECK(a.length() == close(5.0f));

    const Vec2 n = a.normalized();
    CHECK(n.length() == close(1.0f));
    CHECK(n.x == close(0.6f));
    CHECK(n.y == close(0.8f));
}

TEST_CASE("Vec2 normalização de vetor nulo devolve nulo", "[math][vec2]") {
    const Vec2 zero{};
    CHECK(zero.lengthSquared() == 0.0f);
    const Vec2 n = zero.normalized();
    CHECK(n.x == 0.0f);
    CHECK(n.y == 0.0f);
}

TEST_CASE("Vec2 operadores compostos", "[math][vec2]") {
    Vec2 v{1.0f, 1.0f};
    v += Vec2{1.0f, 2.0f};
    CHECK(v == Vec2{2.0f, 3.0f});
    v -= Vec2{1.0f, 1.0f};
    CHECK(v == Vec2{1.0f, 2.0f});
    v *= 3.0f;
    CHECK(v == Vec2{3.0f, 6.0f});
}

TEST_CASE("Vec3 aritmética e produto vetorial right-handed", "[math][vec3]") {
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};

    CHECK(x.cross(y) == z);
    CHECK(y.cross(z) == x);
    CHECK(z.cross(x) == y);
    CHECK(x.cross(x) == Vec3{0.0f, 0.0f, 0.0f});

    CHECK(x + y + z == Vec3{1.0f, 1.0f, 1.0f});
    CHECK(z - z == Vec3{0.0f, 0.0f, 0.0f});
    CHECK(-z == Vec3{0.0f, 0.0f, -1.0f});
    CHECK((y * 2.5f) == Vec3{0.0f, 2.5f, 0.0f});
    CHECK((0.5f * x) == Vec3{0.5f, 0.0f, 0.0f});
}

TEST_CASE("Vec3 norma e normalização", "[math][vec3]") {
    const Vec3 v{1.0f, 2.0f, 2.0f};
    CHECK(v.lengthSquared() == 9.0f);
    CHECK(v.length() == close(3.0f));

    const Vec3 n = v.normalized();
    CHECK(n.length() == close(1.0f));
    CHECK(n.x == close(1.0f / 3.0f));
    CHECK(n.z == close(2.0f / 3.0f));

    const Vec3 zero{};
    CHECK(zero.normalized() == Vec3{0.0f, 0.0f, 0.0f});
}

TEST_CASE("Vec4 aritmética homogênea", "[math][vec4]") {
    const Vec4 a{1.0f, 2.0f, 3.0f, 1.0f};
    const Vec4 b{0.5f, 0.5f, 0.5f, 0.0f};

    CHECK(a + b == Vec4{1.5f, 2.5f, 3.5f, 1.0f});
    CHECK(a - b == Vec4{0.5f, 1.5f, 2.5f, 1.0f});
    CHECK(-a == Vec4{-1.0f, -2.0f, -3.0f, -1.0f});
    CHECK(a.dot(b) == close(3.0f));
    CHECK(a.lengthSquared() == close(15.0f));

    const Vec4 n = a.normalized();
    CHECK(n.length() == close(1.0f));
    CHECK(n.w == close(1.0f / std::sqrt(15.0f)));
}

TEST_CASE("Vec4 normalização de vetor nulo devolve nulo", "[math][vec4]") {
    const Vec4 zero{};
    CHECK(zero.normalized() == Vec4{0.0f, 0.0f, 0.0f, 0.0f});
}
