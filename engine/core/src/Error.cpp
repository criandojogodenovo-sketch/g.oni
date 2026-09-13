#include "eng/core/Error.hpp"

namespace eng::core {

std::string_view Error::codeName() const noexcept {
    switch (code) {
    case StatusCode::Ok:              return "Ok";
    case StatusCode::Unknown:         return "Unknown";
    case StatusCode::InvalidArgument: return "InvalidArgument";
    case StatusCode::OutOfMemory:     return "OutOfMemory";
    case StatusCode::NotFound:        return "NotFound";
    case StatusCode::AlreadyExists:   return "AlreadyExists";
    case StatusCode::ParseError:      return "ParseError";
    case StatusCode::NotSupported:    return "NotSupported";
    case StatusCode::IOError:         return "IOError";
    }
    return "InvalidCode";
}

} // namespace eng::core
