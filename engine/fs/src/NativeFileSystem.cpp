#include "eng/fs/NativeFileSystem.hpp"

#include <algorithm>
#include <system_error>

#include "eng/fs/File.hpp"

namespace eng::fs {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

/// Erro comum de path inválido — checagem centralizada do contrato.
[[nodiscard]] Error invalidPath(const Path& path, const char* op)
{
    (void)path;
    return Error{StatusCode::InvalidArgument,
                 std::string(op) + ": path inválido (vazio ou com NUL)"};
}

[[nodiscard]] Error fsError(const char* op, const Path& path,
                            const std::error_code& ec)
{
    const StatusCode code =
        ec == std::errc::no_such_file_or_directory
            ? StatusCode::NotFound
            : (ec == std::errc::invalid_argument ? StatusCode::InvalidArgument
                                                 : StatusCode::IOError);
    return Error{code, std::string(op) + ": '" + path.str() +
                           "' falhou: " + ec.message()};
}

} // namespace

Result<bool> NativeFileSystem::exists(const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "exists"));
    }
    std::error_code ec;
    const bool found = std::filesystem::exists(path.native(), ec);
    if (ec) {
        return makeUnexpected(fsError("exists", path, ec));
    }
    return found;
}

Result<std::vector<std::byte>> NativeFileSystem::readAllBytes(
    const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "readAllBytes"));
    }
    Result<File> opened = File::open(path, FileOpenMode::Read);
    if (opened.isError()) {
        return makeUnexpected(opened.error());
    }
    Result<std::uint64_t> bytes = opened.value().size();
    if (bytes.isError()) {
        return makeUnexpected(bytes.error());
    }
    std::vector<std::byte> out;
    out.resize(static_cast<std::size_t>(bytes.value()));
    if (!out.empty()) {
        Result<std::size_t> got = opened.value().read(out);
        if (got.isError()) {
            return makeUnexpected(got.error());
        }
        out.resize(got.value());
    }
    return out;
}

Result<std::string> NativeFileSystem::readAllText(const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "readAllText"));
    }
    Result<std::vector<std::byte>> bytes = readAllBytes(path);
    if (bytes.isError()) {
        return makeUnexpected(bytes.error());
    }
    const auto& raw = bytes.value();
    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

Result<void> NativeFileSystem::writeAllBytes(const Path& path,
                                             std::span<const std::byte> bytes)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "writeAllBytes"));
    }
    Result<File> opened = File::open(path, FileOpenMode::Write);
    if (opened.isError()) {
        return makeUnexpected(opened.error());
    }
    if (!bytes.empty()) {
        Result<std::size_t> put = opened.value().write(bytes);
        if (put.isError()) {
            return makeUnexpected(put.error());
        }
    }
    return {};
}

Result<void> NativeFileSystem::writeAllText(const Path& path,
                                            std::string_view text)
{
    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(text.data()), text.size()};
    return writeAllBytes(path, bytes);
}

Result<bool> NativeFileSystem::remove(const Path& path)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "remove"));
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(path.native(), ec);
    if (ec && ec != std::errc::directory_not_empty) {
        return makeUnexpected(fsError("remove", path, ec));
    }
    // Diretório não-vazio: não é erro — nada foi removido.
    return ec == std::errc::directory_not_empty ? false : removed;
}

Result<void> NativeFileSystem::rename(const Path& from, const Path& to)
{
    if (!from.valid() || !to.valid()) {
        return makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "rename: path inválido (vazio ou com NUL)"});
    }
    std::error_code ec;
    std::filesystem::rename(from.native(), to.native(), ec);
    if (ec) {
        return makeUnexpected(fsError("rename", from, ec));
    }
    return {};
}

Result<void> NativeFileSystem::mkdirs(const Path& path)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "mkdirs"));
    }
    std::error_code ec;
    const bool created = std::filesystem::create_directories(path.native(), ec);
    if (ec) {
        return makeUnexpected(fsError("mkdirs", path, ec));
    }
    (void)created; // false = já existia: Ok por contrato
    if (!std::filesystem::is_directory(path.native(), ec) || ec) {
        return makeUnexpected(
            Error{StatusCode::IOError,
                  "mkdirs: '" + path.str() +
                      "' existe e não é diretório"});
    }
    return {};
}

Result<std::vector<ListEntry>> NativeFileSystem::list(const Path& dir,
                                                      bool recursive) const
{
    if (!dir.valid()) {
        return makeUnexpected(invalidPath(dir, "list"));
    }
    std::error_code ec;
    if (!std::filesystem::is_directory(dir.native(), ec) || ec) {
        return makeUnexpected(Error{StatusCode::NotFound,
                                    "list: '" + dir.str() +
                                        "' não é um diretório"});
    }

    std::vector<ListEntry> entries;
    const auto push = [&](const std::filesystem::path& p, bool isDir) {
        entries.push_back(ListEntry{Path::fromNative(p), isDir});
    };

    if (recursive) {
        std::filesystem::recursive_directory_iterator it(dir.native(), ec);
        while (!ec && it != std::filesystem::recursive_directory_iterator()) {
            const bool isDir = it->is_directory(ec);
            if (!ec) {
                push(it->path(), isDir);
            }
            it.increment(ec);
        }
    } else {
        std::filesystem::directory_iterator it(dir.native(), ec);
        while (!ec && it != std::filesystem::directory_iterator()) {
            const bool isDir = it->is_directory(ec);
            if (!ec) {
                push(it->path(), isDir);
            }
            it.increment(ec);
        }
    }
    if (ec) {
        return makeUnexpected(fsError("list", dir, ec));
    }
    std::sort(entries.begin(), entries.end(),
              [](const ListEntry& a, const ListEntry& b) {
                  return a.path.str() < b.path.str();
              });
    return entries;
}

} // namespace eng::fs
