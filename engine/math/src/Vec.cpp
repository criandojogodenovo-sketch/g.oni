#include "eng/math/Math.hpp"

#include <cmath>

namespace eng::math {

float Vec2::length() const noexcept {
    return std::sqrt(lengthSquared());
}

Vec2 Vec2::normalized() const noexcept {
    const float lenSq = lengthSquared();
    if (lenSq <= 0.0f) {
        return {}; // vetor nulo → vetor nulo (sem NaN)
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {x * inv, y * inv};
}

float Vec3::length() const noexcept {
    return std::sqrt(lengthSquared());
}

Vec3 Vec3::normalized() const noexcept {
    const float lenSq = lengthSquared();
    if (lenSq <= 0.0f) {
        return {}; // vetor nulo → vetor nulo (sem NaN)
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {x * inv, y * inv, z * inv};
}

float Vec4::length() const noexcept {
    return std::sqrt(lengthSquared());
}

Vec4 Vec4::normalized() const noexcept {
    const float lenSq = lengthSquared();
    if (lenSq <= 0.0f) {
        return {}; // vetor nulo → vetor nulo (sem NaN)
    }
    const float inv = 1.0f / std::sqrt(lenSq);
    return {x * inv, y * inv, z * inv, w * inv};
}

} // namespace eng::math
