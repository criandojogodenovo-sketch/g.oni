#pragma once

/// eng::image — decodificação de imagens em memória (evolução P0-2).
///
/// O caminho crítico "importar imagem → ver na cena" precisa decodificar
/// PNG/JPEG para pixels RGBA8 ANTES de subir para a GPU (RHI TextureDesc).
/// Este módulo é propositalmente MINÚSCULO e puro:
///   - entrada: bytes em memória (std::span) — NADA de filesystem aqui
///     (a leitura é papel do chamador via eng::fs);
///   - saída: sempre RGBA8 tight-packed (row-major) — formato EXATO do
///     `TextureDesc::initialData`, sem conversões espalhadas;
///   - decodificador: stb_image v2.30 (public domain) confinada a UM TU
///     (src/StbImageImpl.cpp) — nenhum header de terceiro vaza para os
///     consumidores do módulo.
///
/// Formatos suportados (honesto): PNG e JPEG. (WebP/BMP/GIF/TGA são
/// decodificáveis pelo stb mas NÃO são suportados/expostos ainda — a
/// política é não prometer o que não é testado.)

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "eng/core/Result.hpp"

namespace eng::image {

/// Pixels decodificados — sempre RGBA8.
struct Image {
    std::uint32_t width{0};
    std::uint32_t height{0};
    /// Canais do ARQUIVO original (1-4; metadada honesta).
    std::uint32_t sourceChannels{0};
    /// O arquivo tinha canal alfa com dados?
    bool alpha{false};
    /// RGBA8 (4 bytes/pixel), tamanho exato `width * height * 4`.
    std::vector<std::byte> pixels{};

    [[nodiscard]] std::size_t expectedSize() const noexcept {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

/// Formato detectado pelos magic bytes.
enum class ImageFormat : std::uint8_t {
    Png,       ///< 89 50 4E 47 0D 0A 1A 0A
    Jpeg,      ///< FF D8 FF
    Unknown,
};

/// Detecção por magic bytes (sem decodificar — barato e sem efeitos).
[[nodiscard]] ImageFormat detectFormat(std::span<const std::byte> data) noexcept;

/// Decodifica PNG/JPEG → RGBA8. Erros precisos: vazio, formato
/// desconhecido, decodificação falhou (motivo do stb incluído).
[[nodiscard]] eng::core::Result<Image> decode(std::span<const std::byte> data);

}  // namespace eng::image
