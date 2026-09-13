#include "eng/math/Mat4.hpp"

#include <cmath>

namespace eng::math {

namespace {

/// Determinante da submatriz 3x3 formada pelas linhas {r0,r1,r2} e colunas
/// {c0,c1,c2} da matriz 4x4 (índices lógicos linha/coluna).
[[nodiscard]] float subDet(const Mat4& m, int r0, int r1, int r2,
                           int c0, int c1, int c2) noexcept {
    const auto e = [&m](int row, int col) noexcept { return m.at(col, row); };
    return e(r0, c0) * (e(r1, c1) * e(r2, c2) - e(r1, c2) * e(r2, c1)) -
           e(r0, c1) * (e(r1, c0) * e(r2, c2) - e(r1, c2) * e(r2, c0)) +
           e(r0, c2) * (e(r1, c0) * e(r2, c1) - e(r1, c1) * e(r2, c0));
}

} // namespace

Mat4 Mat4::rotationX(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = identity();
    out.at(1, 1) = c;
    out.at(1, 2) = s;  // M(2,1) = +s
    out.at(2, 1) = -s; // M(1,2) = -s
    out.at(2, 2) = c;
    return out;
}

Mat4 Mat4::rotationY(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = identity();
    out.at(0, 0) = c;
    out.at(2, 0) = s;  // M(0,2) = +s
    out.at(0, 2) = -s; // M(2,0) = -s
    out.at(2, 2) = c;
    return out;
}

Mat4 Mat4::rotationZ(float radians) noexcept {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    Mat4 out = identity();
    out.at(0, 0) = c;
    out.at(0, 1) = s;  // M(1,0) = +s
    out.at(1, 0) = -s; // M(0,1) = -s
    out.at(1, 1) = c;
    return out;
}

Mat4 Mat4::perspective(float fovYRadians, float aspect, float nearZ, float farZ) noexcept {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    Mat4 out; // zerada
    out.at(0, 0) = f / aspect;
    out.at(1, 1) = f;
    out.at(2, 3) = -1.0f; // w' = -z (right-handed)
#ifdef ENG_MATH_VULKAN_DEPTH
    out.at(2, 2) = farZ / (nearZ - farZ);
    out.at(3, 2) = nearZ * farZ / (nearZ - farZ);
#else
    out.at(2, 2) = (farZ + nearZ) / (nearZ - farZ);
    out.at(3, 2) = 2.0f * farZ * nearZ / (nearZ - farZ);
#endif
    return out;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top,
                        float nearZ, float farZ) noexcept {
    Mat4 out; // zerada
    out.at(0, 0) = 2.0f / (right - left);
    out.at(1, 1) = 2.0f / (top - bottom);
    out.at(3, 0) = -(right + left) / (right - left);
    out.at(3, 1) = -(top + bottom) / (top - bottom);
    out.at(3, 3) = 1.0f;
#ifdef ENG_MATH_VULKAN_DEPTH
    out.at(2, 2) = 1.0f / (nearZ - farZ);
    out.at(3, 2) = nearZ / (nearZ - farZ);
#else
    out.at(2, 2) = -2.0f / (farZ - nearZ);
    out.at(3, 2) = -(farZ + nearZ) / (farZ - nearZ);
#endif
    return out;
}

Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
    const Vec3 forward = (target - eye).normalized();
    const Vec3 side = forward.cross(up).normalized();
    const Vec3 trueUp = side.cross(forward);

    Mat4 out; // zerada
    out.at(0, 0) = side.x;
    out.at(1, 0) = side.y;
    out.at(2, 0) = side.z;
    out.at(0, 1) = trueUp.x;
    out.at(1, 1) = trueUp.y;
    out.at(2, 1) = trueUp.z;
    out.at(0, 2) = -forward.x;
    out.at(1, 2) = -forward.y;
    out.at(2, 2) = -forward.z;
    out.at(3, 0) = -side.dot(eye);
    out.at(3, 1) = -trueUp.dot(eye);
    out.at(3, 2) = forward.dot(eye);
    out.at(3, 3) = 1.0f;
    return out;
}

Mat4 Mat4::operator*(const Mat4& rhs) const noexcept {
    Mat4 out; // zerada
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += at(k, row) * rhs.at(col, k);
            }
            out.at(col, row) = sum;
        }
    }
    return out;
}

Vec4 Mat4::operator*(const Vec4& v) const noexcept {
    return {
        at(0, 0) * v.x + at(1, 0) * v.y + at(2, 0) * v.z + at(3, 0) * v.w,
        at(0, 1) * v.x + at(1, 1) * v.y + at(2, 1) * v.z + at(3, 1) * v.w,
        at(0, 2) * v.x + at(1, 2) * v.y + at(2, 2) * v.z + at(3, 2) * v.w,
        at(0, 3) * v.x + at(1, 3) * v.y + at(2, 3) * v.z + at(3, 3) * v.w,
    };
}

Vec3 Mat4::transformPoint(const Vec3& p) const noexcept {
    const Vec4 h = (*this) * Vec4{p.x, p.y, p.z, 1.0f};
    if (h.w == 0.0f || h.w == 1.0f) {
        return {h.x, h.y, h.z};
    }
    const float invW = 1.0f / h.w;
    return {h.x * invW, h.y * invW, h.z * invW};
}

Vec3 Mat4::transformDirection(const Vec3& d) const noexcept {
    const Vec4 h = (*this) * Vec4{d.x, d.y, d.z, 0.0f};
    return {h.x, h.y, h.z};
}

Mat4 Mat4::transposed() const noexcept {
    Mat4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out.at(row, col) = at(col, row);
        }
    }
    return out;
}

float Mat4::determinant() const noexcept {
    // Expansão de cofatores ao longo da primeira linha (linha 0):
    // det = M(0,0)·C(0,0) − M(0,1)·C(0,1) + M(0,2)·C(0,2) − M(0,3)·C(0,3)
    return at(0, 0) * subDet(*this, 1, 2, 3, 1, 2, 3) -
           at(1, 0) * subDet(*this, 1, 2, 3, 0, 2, 3) +
           at(2, 0) * subDet(*this, 1, 2, 3, 0, 1, 3) -
           at(3, 0) * subDet(*this, 1, 2, 3, 0, 1, 2);
}

std::optional<Mat4> Mat4::inverted() const noexcept {
    const float det = determinant();
    if (std::fabs(det) < 1e-12f) {
        return std::nullopt;
    }

    // Inversa via adjugata: (M⁻¹)(i,j) = C(j,i)/det — o cofator da TRANSPOSTA.
    Mat4 out;
    for (int invRow = 0; invRow < 4; ++invRow) {
        for (int invCol = 0; invCol < 4; ++invCol) {
            // Cofator C(srcRow=invCol, srcCol=invRow): remove a linha invCol
            // e a coluna invRow da matriz original.
            int keepRows[3];
            int rowCount = 0;
            for (int i = 0; i < 4; ++i) {
                if (i != invCol) {
                    keepRows[rowCount++] = i;
                }
            }
            int keepCols[3];
            int colCount = 0;
            for (int j = 0; j < 4; ++j) {
                if (j != invRow) {
                    keepCols[colCount++] = j;
                }
            }
            const float minor = subDet(*this, keepRows[0], keepRows[1], keepRows[2],
                                       keepCols[0], keepCols[1], keepCols[2]);
            const float sign = ((invRow + invCol) % 2 == 0) ? 1.0f : -1.0f;
            out.at(invCol, invRow) = sign * minor / det;
        }
    }
    return out;
}

} // namespace eng::math
