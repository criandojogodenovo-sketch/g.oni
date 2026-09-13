#pragma once

namespace eng::math {

/// Vetor 2D de floats. Agregado: `Vec2{x, y}`; padrão `{0, 0}`.
struct Vec2 {
    float x{0.0f};
    float y{0.0f};

    // --- aritmética -----------------------------------------------------------

    [[nodiscard]] constexpr Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    [[nodiscard]] constexpr Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    [[nodiscard]] constexpr Vec2 operator-() const noexcept { return {-x, -y}; }
    [[nodiscard]] constexpr Vec2 operator*(float s) const noexcept { return {x * s, y * s}; }
    [[nodiscard]] constexpr Vec2 operator/(float s) const noexcept { return {x / s, y / s}; }

    constexpr Vec2& operator+=(const Vec2& o) noexcept {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(const Vec2& o) noexcept {
        x -= o.x;
        y -= o.y;
        return *this;
    }
    constexpr Vec2& operator*=(float s) noexcept {
        x *= s;
        y *= s;
        return *this;
    }

    /// Produto escalar.
    [[nodiscard]] constexpr float dot(const Vec2& o) const noexcept { return x * o.x + y * o.y; }

    /// Norma ao quadrado (sem sqrt — preferir em comparações).
    [[nodiscard]] constexpr float lengthSquared() const noexcept { return dot(*this); }

    /// Norma euclidiana.
    [[nodiscard]] float length() const noexcept;

    /// Versão normalizada; vetor nulo devolve vetor nulo (garantia sem NaN).
    [[nodiscard]] Vec2 normalized() const noexcept;

    [[nodiscard]] constexpr bool operator==(const Vec2& o) const noexcept = default;
};

[[nodiscard]] constexpr Vec2 operator*(float s, const Vec2& v) noexcept { return v * s; }

} // namespace eng::math
