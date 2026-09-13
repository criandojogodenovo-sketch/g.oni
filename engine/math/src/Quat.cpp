#include "eng/math/Quat.hpp"

#include <cmath>

namespace eng::math {

Quat Quat::fromAxisAngle(const Vec3& axis, float radians) noexcept {
    const float lenSq = axis.lengthSquared();
    if (lenSq <= 0.0f) {
        return identity(); // eixo nulo → sem rotação
    }
    const float invLen = 1.0f / std::sqrt(lenSq);
    const float halfAngle = radians * 0.5f;
    const float s = std::sin(halfAngle);
    return {
        axis.x * invLen * s,
        axis.y * invLen * s,
        axis.z * invLen * s,
        std::cos(halfAngle),
    };
}

Quat Quat::fromEulerAngles(float pitchX, float yawY, float rollZ) noexcept {
    // R = RotY(yaw) * RotX(pitch) * RotZ(roll)
    const Quat pitch = fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitchX);
    const Quat yaw = fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawY);
    const Quat roll = fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, rollZ);
    return yaw * pitch * roll;
}

Quat Quat::operator*(const Quat& o) const noexcept {
    return {
        w * o.x + x * o.w + y * o.z - z * o.y,
        w * o.y - x * o.z + y * o.w + z * o.x,
        w * o.z + x * o.y - y * o.x + z * o.w,
        w * o.w - x * o.x - y * o.y - z * o.z,
    };
}

float Quat::length() const noexcept {
    return std::sqrt(lengthSquared());
}

Quat Quat::normalized() const noexcept {
    const float lenSq = lengthSquared();
    if (lenSq <= 0.0f) {
        return identity();
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {x * inv, y * inv, z * inv, w * inv};
}

Quat Quat::inverse() const noexcept {
    const float lenSq = lengthSquared();
    if (lenSq <= 0.0f) {
        return identity();
    }
    const float inv = 1.0f / lenSq;
    return {-x * inv, -y * inv, -z * inv, w * inv};
}

Vec3 Quat::rotate(const Vec3& v) const noexcept {
    // v' = v + 2w(u × v) + 2(u × (u × v)) — sem quatérnios auxiliares.
    const Vec3 u{x, y, z};
    const Vec3 t = u.cross(v) * 2.0f;
    return v + t * w + u.cross(t);
}

Mat4 Quat::toMatrix() const noexcept {
    const float x2 = x + x;
    const float y2 = y + y;
    const float z2 = z + z;
    const float xx = x * x2;
    const float xy = x * y2;
    const float xz = x * z2;
    const float yy = y * y2;
    const float yz = y * z2;
    const float zz = z * z2;
    const float wx = w * x2;
    const float wy = w * y2;
    const float wz = w * z2;

    Mat4 out = Mat4::identity();
    // at(col, row) = M(row, col) — fórmulas clássicas em notação linha-coluna:
    // M(0,1)=2xy−2wz, M(0,2)=2xz+2wy, M(1,0)=2xy+2wz, M(1,2)=2yz−2wx,
    // M(2,0)=2xz−2wy, M(2,1)=2yz+2wx (xx/yy/zz já valem 2x²/2y²/2z²).
    out.at(0, 0) = 1.0f - (yy + zz);
    out.at(1, 0) = xy - wz;
    out.at(2, 0) = xz + wy;
    out.at(0, 1) = xy + wz;
    out.at(1, 1) = 1.0f - (xx + zz);
    out.at(2, 1) = yz - wx;
    out.at(0, 2) = xz - wy;
    out.at(1, 2) = yz + wx;
    out.at(2, 2) = 1.0f - (xx + yy);
    return out;
}

Quat Quat::slerp(const Quat& target, float t) const noexcept {
    float cosOmega = dot(target);
    Quat end = target;

    // Caminho mínimo de arco: gira o alvo quando o cosseno é negativo.
    if (cosOmega < 0.0f) {
        end = -end;
        cosOmega = -cosOmega;
    }

    if (cosOmega > 0.9995f) {
        // Quase colinear: interpolação linear + normalização é estável.
        Quat out{
            x + t * (end.x - x),
            y + t * (end.y - y),
            z + t * (end.z - z),
            w + t * (end.w - w),
        };
        return out.normalized();
    }

    const float clamped = cosOmega > 1.0f ? 1.0f : (cosOmega < -1.0f ? -1.0f : cosOmega);
    const float omega = std::acos(clamped);
    const float sinOmega = std::sqrt(1.0f - clamped * clamped);
    const float scaleThis = std::sin((1.0f - t) * omega) / sinOmega;
    const float scaleEnd = std::sin(t * omega) / sinOmega;

    return {
        x * scaleThis + end.x * scaleEnd,
        y * scaleThis + end.y * scaleEnd,
        z * scaleThis + end.z * scaleEnd,
        w * scaleThis + end.w * scaleEnd,
    };
}

} // namespace eng::math
