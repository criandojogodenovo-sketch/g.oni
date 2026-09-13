#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "eng/math/Math.hpp"

namespace {

using eng::math::Mat4;
using eng::math::Vec3;
using eng::math::Vec4;

Catch::Approx close(float v) { return Catch::Approx(v).epsilon(0.0001); }
Catch::Approx nearZero(float v) { return Catch::Approx(v).margin(0.0001); }

constexpr float kHalfPi = 1.5707963267948966f;

} // namespace

TEST_CASE("Mat4 identidade é elemento neutro", "[math][mat4]") {
    const Mat4 i = Mat4::identity();
    CHECK(i.at(0, 0) == 1.0f);
    CHECK(i.at(1, 1) == 1.0f);
    CHECK(i.at(2, 2) == 1.0f);
    CHECK(i.at(3, 3) == 1.0f);
    CHECK(i.at(1, 0) == 0.0f);
    CHECK(i.at(3, 2) == 0.0f);

    const Mat4 t = Mat4::translation({1.0f, 2.0f, 3.0f});
    const Mat4 it = i * t;
    CHECK(it.at(3, 0) == 1.0f);
    CHECK(it.at(3, 1) == 2.0f);
    CHECK(it.at(3, 2) == 3.0f);
    CHECK(it.at(0, 0) == 1.0f);

    const Mat4 ti = t * i;
    CHECK(ti.at(3, 0) == 1.0f);
    CHECK(ti.at(3, 2) == 3.0f);
}

TEST_CASE("Mat4 translação move pontos e ignora direções", "[math][mat4]") {
    const Mat4 t = Mat4::translation({10.0f, -20.0f, 2.0f});
    const Vec3 p = t.transformPoint({1.0f, 1.0f, 1.0f});
    CHECK(p.x == close(11.0f));
    CHECK(p.y == close(-19.0f));
    CHECK(p.z == close(3.0f));

    const Vec3 d = t.transformDirection({1.0f, 0.0f, 0.0f});
    CHECK(d.x == close(1.0f));
    CHECK(d.y == close(0.0f));
    CHECK(d.z == close(0.0f));
}

TEST_CASE("Mat4 escala multiplica pontos", "[math][mat4]") {
    const Mat4 s = Mat4::scale({2.0f, 3.0f, 4.0f});
    const Vec3 p = s.transformPoint({1.0f, 1.0f, 1.0f});
    CHECK(p.x == close(2.0f));
    CHECK(p.y == close(3.0f));
    CHECK(p.z == close(4.0f));
    CHECK(s.determinant() == close(24.0f));
}

TEST_CASE("Mat4 rotações elementares seguem right-handed", "[math][mat4]") {
    const Vec3 v = Mat4::rotationX(kHalfPi).transformPoint({0.0f, 1.0f, 0.0f});
    CHECK(v.x == nearZero(0.0f));
    CHECK(v.y == nearZero(0.0f));
    CHECK(v.z == close(1.0f)); // +Y → +Z

    const Vec3 w = Mat4::rotationY(kHalfPi).transformPoint({0.0f, 0.0f, 1.0f});
    CHECK(w.x == close(1.0f)); // +Z → +X
    CHECK(w.y == nearZero(0.0f));
    CHECK(w.z == nearZero(0.0f));

    const Vec3 u = Mat4::rotationZ(kHalfPi).transformPoint({1.0f, 0.0f, 0.0f});
    CHECK(u.x == nearZero(0.0f));
    CHECK(u.y == close(1.0f)); // +X → +Y
    CHECK(u.z == nearZero(0.0f));
}

TEST_CASE("Mat4 determinante", "[math][mat4]") {
    CHECK(Mat4::identity().determinant() == close(1.0f));
    CHECK(Mat4::translation({5.0f, 5.0f, 5.0f}).determinant() == close(1.0f));
    CHECK(Mat4::scale({0.0f, 1.0f, 1.0f}).determinant() == close(0.0f));
    CHECK(Mat4::rotationZ(0.7f).determinant() == close(1.0f));
}

TEST_CASE("Mat4 transposta", "[math][mat4]") {
    const Mat4 t = Mat4::translation({1.0f, 2.0f, 3.0f});
    const Mat4 tt = t.transposed();
    // O termo de translação (col 3) migra para a linha 3 após transpor.
    CHECK(tt.at(0, 3) == 1.0f);
    CHECK(tt.at(1, 3) == 2.0f);
    CHECK(tt.at(2, 3) == 3.0f);

    const Mat4 composite = Mat4::rotationY(0.3f) * Mat4::scale({1.0f, 2.0f, 3.0f});
    const Mat4 roundtrip = composite.transposed().transposed();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            CHECK(roundtrip.at(col, row) == close(composite.at(col, row)));
        }
    }
}

TEST_CASE("Mat4 inversa compõe de volta à identidade", "[math][mat4]") {
    const Mat4 m = Mat4::translation({1.0f, -2.0f, 3.0f}) * Mat4::rotationY(0.7f) *
                   Mat4::scale({2.0f, 3.0f, 4.0f});

    const auto inv = m.inverted();
    REQUIRE(inv.has_value());

    const Mat4 product = m * inv.value();
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const float expected = (col == row) ? 1.0f : 0.0f;
            CHECK(product.at(col, row) == close(expected));
        }
    }

    // A inversa desfaz a transformação em pontos.
    const Vec3 p = m.transformPoint({5.0f, 5.0f, 5.0f});
    const Vec3 restored = inv.value().transformPoint(p);
    CHECK(restored.x == close(5.0f));
    CHECK(restored.y == close(5.0f));
    CHECK(restored.z == close(5.0f));
}

TEST_CASE("Mat4 inversa reporta singularidade", "[math][mat4]") {
    CHECK_FALSE(Mat4::scale({0.0f, 1.0f, 1.0f}).inverted().has_value());
    CHECK_FALSE(Mat4::scale({1.0f, 0.0f, 1.0f}).inverted().has_value());
}

TEST_CASE("Mat4 perspectiva mapeia near/far para o intervalo de profundidade", "[math][mat4]") {
    const float nearZ = 1.0f;
    const float farZ = 10.0f;
    const Mat4 p = Mat4::perspective(kHalfPi, 1.0f, nearZ, farZ); // fov 90°, GL [-1,1]

    // Ponto no plano near (z = -near) → z_ndc = -1.
    const Vec3 nearPoint = p.transformPoint({0.0f, 0.0f, -nearZ});
    CHECK(nearPoint.z == close(-1.0f));

    // Ponto no plano far → z_ndc = +1.
    const Vec3 farPoint = p.transformPoint({0.0f, 0.0f, -farZ});
    CHECK(farPoint.z == close(1.0f));

    // fov 90° com aspect 1: o ponto (±d, 0, -d) cai na borda do frustum.
    const Vec3 edge = p.transformPoint({1.0f, 0.0f, -1.0f});
    CHECK(edge.x == close(1.0f));
    const Vec3 edgeY = p.transformPoint({0.0f, 1.0f, -1.0f});
    CHECK(edgeY.y == close(1.0f));
}

TEST_CASE("Mat4 ortográfica mapeia o volume para o cubo unitário", "[math][mat4]") {
    const Mat4 o = Mat4::orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 10.0f);

    const Vec3 nearCorner = o.transformPoint({-1.0f, -1.0f, -1.0f});
    CHECK(nearCorner.x == close(-1.0f));
    CHECK(nearCorner.y == close(-1.0f));
    CHECK(nearCorner.z == close(-1.0f));

    const Vec3 farCorner = o.transformPoint({1.0f, 1.0f, -10.0f});
    CHECK(farCorner.x == close(1.0f));
    CHECK(farCorner.y == close(1.0f));
    CHECK(farCorner.z == close(1.0f));
}

TEST_CASE("Mat4 lookAt posiciona a câmera na origem olhando para -Z", "[math][mat4]") {
    const eng::math::Vec3 eye{0.0f, 0.0f, 5.0f};
    const Mat4 view = Mat4::lookAt(eye, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});

    const Vec3 eyeInView = view.transformPoint(eye);
    CHECK(eyeInView.x == nearZero(0.0f));
    CHECK(eyeInView.y == nearZero(0.0f));
    CHECK(eyeInView.z == nearZero(0.0f));

    const Vec3 targetInView = view.transformPoint({0.0f, 0.0f, 0.0f});
    CHECK(targetInView.x == nearZero(0.0f));
    CHECK(targetInView.y == nearZero(0.0f));
    CHECK(targetInView.z == close(-5.0f));

    // Um ponto acima do olho permanece "para cima" no espaço da câmera.
    const Vec3 above = view.transformPoint({0.0f, 1.0f, 5.0f});
    CHECK(above.x == nearZero(0.0f));
    CHECK(above.y == close(1.0f));
    CHECK(above.z == nearZero(0.0f));
}

TEST_CASE("Mat4 opera sobre Vec4 homogêneo", "[math][mat4]") {
    const Mat4 t = Mat4::translation({2.0f, 0.0f, 0.0f});
    const Vec4 point{1.0f, 0.0f, 0.0f, 1.0f};
    const Vec4 moved = t * point;
    CHECK(moved.x == close(3.0f));
    CHECK(moved.w == close(1.0f));

    const Vec4 direction{1.0f, 0.0f, 0.0f, 0.0f};
    const Vec4 unmoved = t * direction;
    CHECK(unmoved.x == close(1.0f));
}
