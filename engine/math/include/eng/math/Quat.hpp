#pragma once

#include "eng/math/Mat4.hpp"
#include "eng/math/Vec3.hpp"

namespace eng::math {

/// Quatérnio de rotação (w real; x/y/z imaginários). Rotações compostas
/// pela multiplicação Hamilton: (q1 * q2) aplica q2 antes de q1.
struct Quat {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{1.0f};

    [[nodiscard]] static constexpr Quat identity() noexcept { return {}; }

    /// Eixo unitário + ângulo em radianos. O eixo é normalizado internamente;
    /// eixo nulo devolve a identidade.
    [[nodiscard]] static Quat fromAxisAngle(const Vec3& axis, float radians) noexcept;

    /// Ângulos de Euler em radianos na ordem (pitch = X, yaw = Y, roll = Z),
    /// compondo R = RotY(yaw) * RotX(pitch) * RotZ(roll).
    [[nodiscard]] static Quat fromEulerAngles(float pitchX, float yawY,
                                              float rollZ) noexcept;

    // --- álgebra ---------------------------------------------------------------

    /// Multiplicação Hamilton (composição de rotações).
    [[nodiscard]] Quat operator*(const Quat& o) const noexcept;
    [[nodiscard]] Quat operator-() const noexcept { return {-x, -y, -z, -w}; }

    [[nodiscard]] constexpr float dot(const Quat& o) const noexcept {
        return x * o.x + y * o.y + z * o.z + w * o.w;
    }
    [[nodiscard]] constexpr float lengthSquared() const noexcept { return dot(*this); }
    [[nodiscard]] float length() const noexcept;
    [[nodiscard]] Quat normalized() const noexcept;

    /// Conjugado (inversa quando unitário).
    [[nodiscard]] constexpr Quat conjugate() const noexcept { return {-x, -y, -z, w}; }
    [[nodiscard]] Quat inverse() const noexcept;

    /// Rotaciona um vetor (equivalente a q * v * q⁻¹, sem alocação).
    [[nodiscard]] Vec3 rotate(const Vec3& v) const noexcept;

    /// Matriz de rotação 4x4 equivalente (sem translação/escala).
    [[nodiscard]] Mat4 toMatrix() const noexcept;

    /// Interpolação esférica; t ∈ [0, 1] com caminho mínimo de arco
    /// (correção de sinal) e fallback para nlerp quando quase colinear.
    [[nodiscard]] Quat slerp(const Quat& target, float t) const noexcept;

    /// Igualdade exata por componentes (para testes/constantes; para
    /// comparações numéricas use os vetores rotacionados com tolerância).
    [[nodiscard]] constexpr bool operator==(const Quat&) const noexcept = default;
};

} // namespace eng::math
