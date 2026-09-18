/// eng::image — TU de implementação do stb_image (confinado aqui).
///
/// stb_image.h v2.30 (public domain — nothings/stb, commit
/// 2c980bb59875b0d32144a71867fbdebb2f77cd20) compilada em UM único TU
/// com warnings de terceiro desligados (política ADR-020: os warnings do
/// projeto aplicam-se ao NOSSO código; dependências compilam com seus
/// flags nativos — o CMake desliga -Wall/-Wextra/-Werror PARA ESTE ARQUIVO).

// stb: C puro, sem exceptions/RTTI — compatível com -fno-exceptions/-fno-rtti.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO    // SEM fopen/fwrite — entrada é memória (segurança: sem FS no decode)
#define STBI_ASSERT(x) ((void)0)  // sem assert de terceiro (nós validamos o resultado)

#include <stb_image.h>
