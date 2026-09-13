#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "eng/math/Math.hpp"

namespace {

using eng::math::Mat4;
using eng::math::Quat;
using eng::math::Vec3;

Catch::Approx close(float v) { return Catch::Approx(v).epsilon(0.0001); }
Catch::Approx nearZero(float v) { return Catch::Approx(v).margin(0.0001); }

constexpr float kHalfPi = 1.5707963267948966f;
constexpr float kPi = 3.14159265358979323846f;

} // namespace

TEST_CASE("Quat identidade não rotaciona", "[math][quat]") {
    const Quat q = Quat::identity();
    CHECK(q.w == 1.0f);
    CHECK(q.x == 0.0f);
    CHECK(q.lengthSquared() == close(1.0f));

    const Vec3 v = q.rotate({1.0f, -2.0f, 3.0f});
    CHECK(v.x == close(1.0f));
    CHECK(v.y == close(-2.0f));
    CHECK(v.z == close(3.0f));

    CHECK(Quat{} == Quat::identity());
}

TEST_CASE("Quat fromAxisAngle gira 90 graus em torno de Z", "[math][quat]") {
    const Quat q = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi);
    CHECK(q.length() == close(1.0f));
    CHECK(q.w == close(std::cos(kHalfPi / 2.0f)));
    CHECK(q.z == close(std::sin(kHalfPi / 2.0f)));

    const Vec3 v = q.rotate({1.0f, 0.0f, 0.0f});
    CHECK(v.x == nearZero(0.0f));
    CHECK(v.y == close(1.0f));
    CHECK(v.z == nearZero(0.0f));
}

TEST_CASE("Quat fromAxisAngle normaliza o eixo internamente", "[math][quat]") {
    const Quat a = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi);
    const Quat b = Quat::fromAxisAngle({0.0f, 0.0f, 7.0f}, kHalfPi);
    CHECK(b.x == close(a.x));
    CHECK(b.y == close(a.y));
    CHECK(b.z == close(a.z));
    CHECK(b.w == close(a.w));

    const Quat degenerate = Quat::fromAxisAngle({0.0f, 0.0f, 0.0f}, kHalfPi);
    CHECK(degenerate == Quat::identity());
}

TEST_CASE("Quat multiplicação compõe rotações", "[math][quat]") {
    const Quat qx90 = Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, kHalfPi);
    const Quat qz90 = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi);

    // (qz90 * qx90) aplica qx90 primeiro: +Y → +Z (por X) → +Z (imune a Z).
    const Quat composed = qz90 * qx90;
    CHECK(composed.length() == close(1.0f));

    const Vec3 v = composed.rotate({0.0f, 1.0f, 0.0f});
    CHECK(v.x == nearZero(0.0f));
    CHECK(v.y == nearZero(0.0f));
    CHECK(v.z == close(1.0f));
}

TEST_CASE("Quat toMatrix concorda com Mat4 de rotação", "[math][quat]") {
    const Quat q = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.7f);
    const Mat4 fromQuat = q.toMatrix();
    const Mat4 fromMat4 = Mat4::rotationZ(0.7f);

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            CHECK(fromQuat.at(col, row) == close(fromMat4.at(col, row)));
        }
    }
}

TEST_CASE("Quat inversa desfaz a rotação", "[math][quat]") {
    const Quat q = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 1.234f);
    const Quat inv = q.inverse();

    const Quat roundtrip = q * inv;
    CHECK(roundtrip.w == close(1.0f));
    CHECK(roundtrip.x == nearZero(0.0f));
    CHECK(roundtrip.y == nearZero(0.0f));
    CHECK(roundtrip.z == nearZero(0.0f));

    const Vec3 original{0.3f, -1.0f, 2.0f};
    const Vec3 restored = inv.rotate(q.rotate(original));
    CHECK(restored.x == close(original.x));
    CHECK(restored.y == close(original.y));
    CHECK(restored.z == close(original.z));
}

TEST_CASE("Quat conjugado de quatérnio unitário é a inversa", "[math][quat]") {
    const Quat q = Quat::fromAxisAngle({1.0f, 1.0f, 0.0f}, 0.9f);
    const Quat c = q.conjugate();
    const Quat product = q * c;
    CHECK(product.w == close(1.0f));
    CHECK(product.x == nearZero(0.0f));
    CHECK(product.y == nearZero(0.0f));
    CHECK(product.z == nearZero(0.0f));
}

TEST_CASE("Quat slerp respeita os extremos", "[math][quat]") {
    const Quat a = Quat::identity();
    const Quat b = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kPi);

    const Quat start = a.slerp(b, 0.0f);
    CHECK(start.w == close(1.0f));
    CHECK(start.x == nearZero(0.0f));

    const Quat end = a.slerp(b, 1.0f);
    const Vec3 v = end.rotate({1.0f, 0.0f, 0.0f});
    CHECK(v.x == close(-1.0f)); // 180 graus: +X → -X
    CHECK(v.y == nearZero(0.0f));
}

TEST_CASE("Quat slerp interpola o arco no meio do caminho", "[math][quat]") {
    const Quat a = Quat::identity();
    // 2 rad (~114,6 graus): evita o caso degenerado dot≈0 do ângulo π exato,
    // em que o ruído de float decide o sinal da geodésica.
    const Quat b = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 2.0f);

    const Quat mid = a.slerp(b, 0.5f);
    CHECK(mid.length() == close(1.0f));

    // Meio do arco = rotação de 1 rad: +X → (cos 1, sin 1, 0).
    const Vec3 v = mid.rotate({1.0f, 0.0f, 0.0f});
    CHECK(v.x == close(std::cos(1.0f)));
    CHECK(v.y == close(std::sin(1.0f)));
    CHECK(v.z == nearZero(0.0f));
}

TEST_CASE("Quat slerp escolhe o caminho curto (correção de sinal)", "[math][quat]") {
    const Quat a = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.1f);
    const Quat bNegated = -Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.4f); // mesmo alvo

    const Quat mid = a.slerp(bNegated, 0.5f);
    // Ângulo esperado: ~(0.1 + 0.4)/2 = 0.25 rad em torno de Y.
    const Vec3 v = mid.rotate({1.0f, 0.0f, 0.0f});
    CHECK(v.x == close(std::cos(0.25f)));
    CHECK(v.z == close(-std::sin(0.25f)));
}

TEST_CASE("Quat fromEulerAngles compõe yaw-pitch-roll", "[math][quat]") {
    // Yaw puro deve casar com Mat4::rotationY.
    const Quat q = Quat::fromEulerAngles(0.0f, kHalfPi, 0.0f);
    const Vec3 v = q.rotate({0.0f, 0.0f, 1.0f});
    CHECK(v.x == close(1.0f));
    CHECK(v.y == nearZero(0.0f));
    CHECK(v.z == nearZero(0.0f));

    // Roll puro deve casar com Mat4::rotationZ.
    const Quat roll = Quat::fromEulerAngles(0.0f, 0.0f, kHalfPi);
    const Vec3 r = roll.rotate({1.0f, 0.0f, 0.0f});
    CHECK(r.x == nearZero(0.0f));
    CHECK(r.y == close(1.0f));

    // Pitch puro deve casar com Mat4::rotationX.
    const Quat pitch = Quat::fromEulerAngles(kHalfPi, 0.0f, 0.0f);
    const Vec3 p = pitch.rotate({0.0f, 1.0f, 0.0f});
    CHECK(p.z == close(1.0f));
}

TEST_CASE("Quat quatérnio não-unitário é normalizado", "[math][quat]") {
    Quat q = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.8f);
    q.x *= 2.0f; // distorce
    const Quat n = q.normalized();
    CHECK(n.length() == close(1.0f));
}
