#include "eng/fs/File.hpp"

#include <cerrno>
#include <cstring>

namespace eng::fs {

namespace {

[[nodiscard]] const char* modeString(FileOpenMode mode) noexcept
{
    return mode == FileOpenMode::Read ? "rb" : "wb";
}

} // namespace

File::File(std::FILE* handle) noexcept : handle_(handle) {}

File::~File()
{
    close();
}

File::File(File&& other) noexcept : handle_(other.handle_)
{
    other.handle_ = nullptr;
}

File& File::operator=(File&& other) noexcept
{
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

Result<File> File::open(const Path& path, FileOpenMode mode)
{
    if (!path.valid()) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::InvalidArgument,
            "File::open: path inválido (vazio ou com NUL)"});
    }
    std::FILE* handle = std::fopen(path.str().c_str(), modeString(mode));
    if (handle == nullptr) {
        const eng::core::StatusCode code =
            errno == ENOENT ? eng::core::StatusCode::NotFound
                            : eng::core::StatusCode::IOError;
        return eng::core::makeUnexpected(eng::core::Error{
            code, "File::open: falha ao abrir '" + path.str() +
                      "' (errno " + std::to_string(errno) + ": " +
                      std::strerror(errno) + ")"});
    }
    return File(handle);
}

Result<std::size_t> File::read(std::span<std::byte> out)
{
    if (handle_ == nullptr) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::InvalidArgument,
            "File::read: arquivo não está aberto"});
    }
    if (out.empty()) {
        return std::size_t{0};
    }
    const std::size_t got = std::fread(out.data(), 1, out.size(), handle_);
    if (got < out.size() && std::ferror(handle_) != 0) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError, "File::read: erro de leitura"});
    }
    return got;
}

Result<std::size_t> File::write(std::span<const std::byte> in)
{
    if (handle_ == nullptr) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::InvalidArgument,
            "File::write: arquivo não está aberto"});
    }
    if (in.empty()) {
        return std::size_t{0};
    }
    const std::size_t put = std::fwrite(in.data(), 1, in.size(), handle_);
    if (put != in.size()) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError, "File::write: escrita incompleta"});
    }
    return put;
}

Result<std::uint64_t> File::size()
{
    if (handle_ == nullptr) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::InvalidArgument,
            "File::size: arquivo não está aberto"});
    }
    const long here = std::ftell(handle_);
    if (here < 0 || std::fseek(handle_, 0, SEEK_END) != 0) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError, "File::size: ftell/fseek falhou"});
    }
    const long end = std::ftell(handle_);
    std::fseek(handle_, here, SEEK_SET);
    if (end < 0) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::IOError, "File::size: ftell falhou"});
    }
    return static_cast<std::uint64_t>(end);
}

void File::close() noexcept
{
    if (handle_ != nullptr) {
        std::fclose(handle_);
        handle_ = nullptr;
    }
}

} // namespace eng::fs
