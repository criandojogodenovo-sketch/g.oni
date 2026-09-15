#include "eng/platform/PlatformInfo.hpp"

#if !defined(__linux__)
#error "eng::platform: SO não suportado na FASE 3 (Linux apenas; ver ADR-026)"
#endif

#include <cstdio>

namespace eng::platform {

PlatformInfo PlatformInfo::current()
{
    PlatformInfo info;

#if defined(__x86_64__)
    info.name = "Linux";
    info.arch = "x86_64";
#elif defined(__aarch64__)
    info.name = "Linux";
    info.arch = "aarch64";
#elif defined(__arm__)
    info.name = "Linux";
    info.arch = "arm";
#else
    info.name = "Linux";
    info.arch = "unknown";
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    info.endianness = Endianness::Little;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    info.endianness = Endianness::Big;
#endif
#endif

#if defined(NDEBUG)
    info.buildType = BuildType::Release;
#else
    info.buildType = BuildType::Debug;
#endif

#if defined(__SANITIZE_ADDRESS__)
    info.addressSanitizer = true;
#endif
#if defined(__SANITIZE_UNDEFINED__)
    info.undefinedSanitizer = true;
#endif

    return info;
}

} // namespace eng::platform
