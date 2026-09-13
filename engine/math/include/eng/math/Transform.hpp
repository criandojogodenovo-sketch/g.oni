#pragma once

#include "eng/math/Mat4.hpp"
#include "eng/math/Quat.hpp"
#include "eng/math/Vec3.hpp"

namespace eng::math {

/// Transformação TRS (translação ∘ rotação ∘ escala). Agregado:
/// `Transform{pos, rot, escala}`; padrão é a identidade.
///
/// toMatrix() = T(pos) * R(rot) * S(escala) — aplica escala, rotação e
/// translação, nesta ordem, sobre vetores-coluna.
struct Transform {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    [[nodiscard]] static constexpr Transform identity() noexcept { return {}; }

    /// Matriz 4x4 equivalente (column-major).
    [[nodiscard]] Mat4 toMatrix() const noexcept;

    /// Decompõe uma matriz TRS (escala por comprimento das colunas da base;
    /// espelhamento não é suportado — escala fica positiva). Matrizes com
    /// coluna degenerada devolvem rotação identidade para a parte afetada.
    [[nodiscard]] static Transform fromMatrix(const Mat4& m) noexcept;

    /// Transforma um ponto: pos + rot * (escala ⊙ p).
    [[nodiscard]] Vec3 transformPoint(const Vec3& p) const noexcept;

    /// Transforma uma direção: rot * (escala ⊙ v) — sem translação.
    [[nodiscard]] Vec3 transformVector(const Vec3& v) const noexcept;

    /// Composição pai ∘ filho (this é o pai): o resultado transforma da
    /// referência do filho para a referência do pai.
    [[nodiscard]] Transform operator*(const Transform& child) const noexcept;
};

} // namespace eng::math
