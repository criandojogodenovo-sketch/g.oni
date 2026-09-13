#pragma once

namespace eng::math {

/// Vetor 4D de floats (posições homogêneas, quatérnios como dados brutos).
struct Vec4 {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    float w{0.0f};

    // --- aritmética -----------------------------------------------------------

    [[nodiscard]] constexpr Vec4 operator+(const Vec4& o) const noexcept {
        return {x + o.x, y + o.y, z + o.z, w + o.w};
    }
    [[nodiscard]] constexpr Vec4 operator-(const Vec4& o) const noexcept {
        return {x - o.x, y - o.y, z - o.z, w - o.w};
    }
    [[nodiscard]] constexpr Vec4 operator-() const noexcept { return {-x, -y, -z, -w}; }
    [[nodiscard]] constexpr Vec4 operator*(float s) const noexcept {
        return {x * s, y * s, z * s, w * s};
    }
    [[nodiscard]] constexpr Vec4 operator/(float s) const noexcept {
        return {x / s, y / s, z / s, w / s};
    }

    constexpr Vec4& operator+=(const Vec4& o) noexcept {
        x += o.x;
        y += o.y;
        z += o.z;
        w += o.w;
        return *this;
    }
    constexpr Vec4& operator*=(float s) noexcept {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }

    /// Produto escalar (4 componentes).
    [[nodiscard]] constexpr float dot(const Vec4& o) const noexcept {
        return x * o.x + y * o.y + z * o.z + w * o.w;
    }

    [[nodiscard]] constexpr float lengthSquared() const noexcept { return dot(*this); }
    [[nodiscard]] float length() const noexcept;

    /// Versão normalizada; vetor nulo devolve vetor nulo (garantia sem NaN).
    [[nodiscard]] Vec4 normalized() const noexcept;

    [[nodiscard]] constexpr bool operator==(const Vec4& o) const noexcept = default;
};

[[nodiscard]] constexpr Vec4 operator*(float s, const Vec4& v) noexcept { return v * s; }

} // namespace eng::math
