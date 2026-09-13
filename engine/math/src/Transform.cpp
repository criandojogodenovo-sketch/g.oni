#include "eng/math/Transform.hpp"

#include <cmath>

namespace eng::math {

Mat4 Transform::toMatrix() const noexcept {
    return Mat4::translation(position) * rotation.toMatrix() * Mat4::scale(scale);
}

Transform Transform::fromMatrix(const Mat4& m) noexcept {
    Transform out;

    out.position = {m.at(3, 0), m.at(3, 1), m.at(3, 2)};

    const Vec3 c0{m.at(0, 0), m.at(0, 1), m.at(0, 2)};
    const Vec3 c1{m.at(1, 0), m.at(1, 1), m.at(1, 2)};
    const Vec3 c2{m.at(2, 0), m.at(2, 1), m.at(2, 2)};

    const float sx = c0.length();
    const float sy = c1.length();
    const float sz = c2.length();
    out.scale = {sx, sy, sz};

    // Base ortonormal para extrair a rotação; colunas degeneradas (escala
    // zero) recebem o eixo canônico correspondente.
    Vec3 r0 = sx > 1e-8f ? c0 / sx : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 r1 = sy > 1e-8f ? c1 / sy : Vec3{0.0f, 1.0f, 0.0f};
    Vec3 r2 = sz > 1e-8f ? c2 / sz : Vec3{0.0f, 0.0f, 1.0f};

    // Shear/espelhamento não são representáveis em TRS: projetamos de volta
    // no espaço de rotação pura (r_ij = M(i,j) normalizado).
    const float r00 = r0.x, r01 = r1.x, r02 = r2.x;
    const float r10 = r0.y, r11 = r1.y, r12 = r2.y;
    const float r20 = r0.z, r21 = r1.z, r22 = r2.z;

    // Método de Shepperd: escolhe o maior "caminho" numérico.
    Quat q;
    const float trace = r00 + r11 + r22;
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r21 - r12) / s;
        q.y = (r02 - r20) / s;
        q.z = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        const float s = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        const float s = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        const float s = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }

    out.rotation = q.normalized();
    return out;
}

Vec3 Transform::transformPoint(const Vec3& p) const noexcept {
    return position + rotation.rotate(Vec3{p.x * scale.x, p.y * scale.y, p.z * scale.z});
}

Vec3 Transform::transformVector(const Vec3& v) const noexcept {
    return rotation.rotate(Vec3{v.x * scale.x, v.y * scale.y, v.z * scale.z});
}

Transform Transform::operator*(const Transform& child) const noexcept {
    Transform out;
    out.scale = {scale.x * child.scale.x, scale.y * child.scale.y, scale.z * child.scale.z};
    out.rotation = rotation * child.rotation;
    out.position = position + rotation.rotate(Vec3{
                                  child.position.x * scale.x,
                                  child.position.y * scale.y,
                                  child.position.z * scale.z,
                              });
    return out;
}

} // namespace eng::math
