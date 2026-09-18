#include "eng/image/Image.hpp"

/// eng::image — decodificação PNG/JPEG em memória (evolução P0-2).

#include <cstring>
#include <string>

// Declaração da API do stb (a DEFINIÇÃO vive em src/StbImageImpl.cpp).
#define STBI_NO_STDIO
#include <stb_image.h>

namespace eng::image {

namespace {

using eng::core::Error;
using eng::core::StatusCode;

[[nodiscard]] Error imageError(StatusCode code, std::string message) {
    return Error{code, "image: " + std::move(message)};
}

}  // namespace

ImageFormat detectFormat(std::span<const std::byte> data) noexcept {
    const auto u = [&data](std::size_t i) -> unsigned {
        return i < data.size()
                   ? static_cast<unsigned>(static_cast<std::uint8_t>(data[i]))
                   : 0u;
    };
    // PNG: 89 50 4E 47 0D 0A 1A 0A
    if (data.size() >= 8 && u(0) == 0x89 && u(1) == 0x50 && u(2) == 0x4E &&
        u(3) == 0x47 && u(4) == 0x0D && u(5) == 0x0A && u(6) == 0x1A &&
        u(7) == 0x0A) {
        return ImageFormat::Png;
    }
    // JPEG: FF D8 FF
    if (data.size() >= 3 && u(0) == 0xFF && u(1) == 0xD8 && u(2) == 0xFF) {
        return ImageFormat::Jpeg;
    }
    return ImageFormat::Unknown;
}

eng::core::Result<Image> decode(std::span<const std::byte> data) {
    if (data.empty()) {
        return eng::core::makeUnexpected(
            imageError(StatusCode::InvalidArgument, "dados vazios"));
    }
    const ImageFormat format = detectFormat(data);
    if (format == ImageFormat::Unknown) {
        return eng::core::makeUnexpected(imageError(
            StatusCode::InvalidArgument,
            "formato não reconhecido (magic bytes) — suportados: PNG, JPEG"));
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    // FORÇA 4 canais na saída (RGBA8 uniforme — contrato do TextureDesc).
    constexpr int kForceChannels = 4;
    const auto* bytes = reinterpret_cast<const stbi_uc*>(data.data());
    stbi_uc* decoded =
        stbi_load_from_memory(bytes, static_cast<int>(data.size()), &width,
                              &height, &sourceChannels, kForceChannels);
    if (decoded == nullptr) {
        const char* reason = stbi_failure_reason();
        return eng::core::makeUnexpected(imageError(
            StatusCode::InvalidArgument,
            std::string{"decodificação falhou: "} +
                (reason != nullptr ? reason : "motivo desconhecido")));
    }
    if (width <= 0 || height <= 0) {
        stbi_image_free(decoded);
        return eng::core::makeUnexpected(imageError(
            StatusCode::InvalidArgument, "dimensões decodificadas inválidas"));
    }

    Image image;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.sourceChannels = static_cast<std::uint32_t>(sourceChannels);
    image.alpha = sourceChannels == 2 || sourceChannels == 4;  // gray+A / RGBA
    const std::size_t size = image.expectedSize();
    image.pixels.resize(size);
    std::memcpy(image.pixels.data(), decoded, size);
    stbi_image_free(decoded);
    return image;
}

}  // namespace eng::image
