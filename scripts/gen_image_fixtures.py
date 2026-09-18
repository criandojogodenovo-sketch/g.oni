#!/usr/bin/env python3
"""Gera fixtures de bytes de imagem (PNG/JPEG) para os testes de eng::image.

Saída: tests/image_fixtures.hpp no repositório (bytes determinísticos).
"""
import io
from PIL import Image

HDR = """#pragma once
/// GERADO por scripts/gen_image_fixtures.py (PIL/zlib determinísticos).
/// PNG 2x2 com 4 cores conhecidas + JPEG 4x4 cor sólida. NÃO editar à mão.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace eng::image::testing {
"""

def cpp_array(name, data, comment):
    lines = [f"/// {comment} ({len(data)} bytes)"]
    lines.append(f"inline constexpr std::array<std::uint8_t, {len(data)}> {name} = {{")
    for i in range(0, len(data), 16):
        lines.append("    " + ",".join(str(b) for b in data[i:i+16]) + ",")
    lines.append("};")
    return "\n".join(lines)

# --- PNG 2x2: vermelho, verde, azul, branco ------------------------------
png = Image.new("RGBA", (2, 2))
png.putpixel((0, 0), (255, 0, 0, 255))
png.putpixel((1, 0), (0, 255, 0, 255))
png.putpixel((0, 1), (0, 0, 255, 255))
png.putpixel((1, 1), (255, 255, 255, 255))
buf = io.BytesIO()
png.save(buf, format="PNG", optimize=False, compress_level=0)
png_bytes = buf.getvalue()

# --- JPEG 4x4: coral sólido (250, 120, 80) --------------------------------
jpg = Image.new("RGB", (4, 4), (250, 120, 80))
buf = io.BytesIO()
jpg.save(buf, format="JPEG", quality=90)
jpg_bytes = buf.getvalue()

out = HDR + "\n" + \
    cpp_array("kPng2x2", png_bytes, "PNG 2x2 RGBA: RG/BW conhecidos") + "\n\n" + \
    cpp_array("kJpeg4x4", jpg_bytes, "JPEG 4x4 RGB coral") + "\n\n" + \
    """inline std::span<const std::byte> png2x2Bytes() {
    return {reinterpret_cast<const std::byte*>(kPng2x2.data()), kPng2x2.size()};
}
inline std::span<const std::byte> jpeg4x4Bytes() {
    return {reinterpret_cast<const std::byte*>(kJpeg4x4.data()), kJpeg4x4.size()};
}

} // namespace eng::image::testing
"""

with open("engine/image/tests/ImageFixtures.hpp", "w") as f:
    f.write(out)
print(f"PNG: {len(png_bytes)} bytes, JPEG: {len(jpg_bytes)} bytes")
print("written engine/image/tests/ImageFixtures.hpp")
