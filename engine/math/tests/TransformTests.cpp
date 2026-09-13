#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "eng/math/Math.hpp"

namespace {

using eng::math::Mat4;
using eng::math::Quat;
using eng::math::Transform;
using eng::math::Vec3;

Catch::Approx close(float v) { return Catch::Approx(v).epsilon(0.0001); }
Catch::Approx nearZero(float v) { return Catch::Approx(v).margin(0.0001); }

constexpr float kHalfPi = 1.5707963267948966f;

} // namespace

TEST_CASE("Transform identidade é o elemento neutro", "[math][transform]") {
    const Transform t = Transform::identity();
    CHECK(t.position == Vec3{0.0f, 0.0f, 0.0f});
    CHECK(t.scale == Vec3{1.0f, 1.0f, 1.0f});
    CHECK(t.rotation == Quat::identity());

    const Vec3 p = t.transformPoint({1.0f, 2.0f, 3.0f});
    CHECK(p.x == close(1.0f));
    CHECK(p.y == close(2.0f));
    CHECK(p.z == close(3.0f));
}

TEST_CASE("Transform toMatrix equivale a T * R * S", "[math][transform]") {
    Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.rotation = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 0.6f);
    t.scale = {2.0f, 3.0f, 4.0f};

    const Mat4 manual = Mat4::translation(t.position) * t.rotation.toMatrix() *
                        Mat4::scale(t.scale);
    const Mat4 computed = t.toMatrix();

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            CHECK(computed.at(col, row) == close(manual.at(col, row)));
        }
    }

    const Vec3 p{1.0f, 1.0f, 1.0f};
    const Vec3 viaMatrix = computed.transformPoint(p);
    const Vec3 viaTransform = t.transformPoint(p);
    CHECK(viaTransform.x == close(viaMatrix.x));
    CHECK(viaTransform.y == close(viaMatrix.y));
    CHECK(viaTransform.z == close(viaMatrix.z));
}

TEST_CASE("Transform aplica escala antes da rotação", "[math][transform]") {
    Transform t;
    t.rotation = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi); // +X → +Y
    t.scale = {1.0f, 2.0f, 1.0f};

    // p = (1, 0, 0): escala mantém (1,0,0); rotação leva a (0,1,0).
    const Vec3 p = t.transformPoint({1.0f, 0.0f, 0.0f});
    CHECK(p.x == nearZero(0.0f));
    CHECK(p.y == close(1.0f));
    CHECK(p.z == nearZero(0.0f));

    // p = (0, 1, 0): escala dobra y → (0,2,0); rotação leva a (-2,0,0).
    const Vec3 q = t.transformPoint({0.0f, 1.0f, 0.0f});
    CHECK(q.x == close(-2.0f));
    CHECK(q.y == nearZero(0.0f));

    // Direções ignoram a translação.
    const Vec3 d = t.transformVector({1.0f, 0.0f, 0.0f});
    CHECK(d.y == close(1.0f));
}

TEST_CASE("Transform compõe pai e filho", "[math][transform]") {
    const Transform parent = Transform::identity();
    Transform child;
    child.position = {0.0f, 1.0f, 0.0f};

    const Transform worldNoScale = parent * child;
    CHECK(worldNoScale.position.y == close(1.0f));
}

TEST_CASE("Transform composição aplica escala do pai no filho", "[math][transform]") {
    Transform parent;
    parent.position = {1.0f, 0.0f, 0.0f};
    parent.scale = {2.0f, 2.0f, 2.0f};

    Transform child;
    child.position = {0.0f, 1.0f, 0.0f};

    const Transform world = parent * child;
    // posição = pai.pos + pai.rot * (pai.escala ⊙ filho.pos) = (1,0,0) + (0,2,0)
    CHECK(world.position.x == close(1.0f));
    CHECK(world.position.y == close(2.0f));
    CHECK(world.position.z == close(0.0f));
    CHECK(world.scale == Vec3{2.0f, 2.0f, 2.0f});
    CHECK(world.rotation == Quat::identity());
}

TEST_CASE("Transform composição aplica rotação do pai no filho", "[math][transform]") {
    Transform parent;
    parent.rotation = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi); // +X → +Y

    Transform child;
    child.position = {1.0f, 0.0f, 0.0f};

    const Transform world = parent * child;
    CHECK(world.position.x == nearZero(0.0f));
    CHECK(world.position.y == close(1.0f));
    CHECK(world.position.z == nearZero(0.0f));

    // Rotações também compõem.
    Transform spinningChild;
    spinningChild.rotation = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, kHalfPi);
    const Transform worldRot = parent * spinningChild;
    const Vec3 v = worldRot.rotation.rotate({1.0f, 0.0f, 0.0f});
    CHECK(v.x == close(-1.0f)); // 180 graus no total
}

TEST_CASE("Transform fromMatrix decompõe TRS", "[math][transform]") {
    Transform original;
    original.position = {1.0f, 2.0f, 3.0f};
    original.rotation = Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, 1.0471975512f); // 60°
    original.scale = {2.0f, 3.0f, 4.0f};

    const Mat4 m = original.toMatrix();
    const Transform decomposed = Transform::fromMatrix(m);

    CHECK(decomposed.position.x == close(1.0f));
    CHECK(decomposed.position.y == close(2.0f));
    CHECK(decomposed.position.z == close(3.0f));

    CHECK(decomposed.scale.x == close(2.0f));
    CHECK(decomposed.scale.y == close(3.0f));
    CHECK(decomposed.scale.z == close(4.0f));

    // Compara rotação pelo efeito sobre um vetor (evita dupla representação q/-q).
    const Vec3 probe{1.0f, 0.0f, 0.0f};
    const Vec3 expected = original.rotation.rotate(probe);
    const Vec3 actual = decomposed.rotation.rotate(probe);
    CHECK(actual.x == close(expected.x));
    CHECK(actual.y == close(expected.y));
    CHECK(actual.z == close(expected.z));
}

TEST_CASE("Transform fromMatrix de translação pura", "[math][transform]") {
    const Transform t = Transform::fromMatrix(Mat4::translation({5.0f, -1.0f, 2.0f}));
    CHECK(t.position == Vec3{5.0f, -1.0f, 2.0f});
    CHECK(t.scale == Vec3{1.0f, 1.0f, 1.0f});
    CHECK(t.rotation == Quat::identity());
}

TEST_CASE("Transform fromMatrix de rotação pura preserva a norma", "[math][transform]") {
    const Transform t = Transform::fromMatrix(Mat4::rotationX(0.9f));
    CHECK(t.scale == Vec3{1.0f, 1.0f, 1.0f});
    const Vec3 v = t.rotation.rotate({0.0f, 1.0f, 0.0f});
    CHECK(v.length() == close(1.0f));
}

TEST_CASE("Transform roundtrip por toMatrix/fromMatrix estável", "[math][transform]") {
    Transform t;
    t.position = {-4.0f, 0.5f, 9.0f};
    t.rotation = Quat::fromEulerAngles(0.2f, 1.1f, -0.4f);
    t.scale = {1.0f, 1.5f, 0.75f};

    const Transform roundtrip = Transform::fromMatrix(t.toMatrix());
    const Transform twice = Transform::fromMatrix(roundtrip.toMatrix());

    CHECK(twice.position.x == close(t.position.x));
    CHECK(twice.position.y == close(t.position.y));
    CHECK(twice.position.z == close(t.position.z));
    CHECK(twice.scale.x == close(t.scale.x));
    CHECK(twice.scale.y == close(t.scale.y));
    CHECK(twice.scale.z == close(t.scale.z));
}
