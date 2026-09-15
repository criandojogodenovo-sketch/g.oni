#include "eng/fs/MemoryFileSystem.hpp"

#include <algorithm>

namespace eng::fs {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

[[nodiscard]] Error invalidPath(const Path& path, const char* op)
{
    (void)path;
    return Error{StatusCode::InvalidArgument,
                 std::string(op) + ": path inválido (vazio ou com NUL)"};
}

[[nodiscard]] Error notFound(const Path& path, const char* op)
{
    return Error{StatusCode::NotFound,
                 std::string(op) + ": '" + path.str() + "' não existe"};
}

} // namespace

std::string MemoryFileSystem::keyOf(const Path& path)
{
    return path.normalized().str();
}

bool MemoryFileSystem::parentDirExists(const std::string& key) const
{
    const std::string::size_type slash = key.rfind('/');
    if (slash == std::string::npos) {
        return true; // relativo à raiz lógica: pai sempre presente
    }
    if (slash == 0) {
        return true; // pai é a raiz absoluta "/"
    }
    return dirs_.count(key.substr(0, slash)) != 0;
}

Result<bool> MemoryFileSystem::exists(const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "exists"));
    }
    const std::string key = keyOf(path);
    if (files_.count(key) != 0 || dirs_.count(key) != 0) {
        return true;
    }
    // Raiz lógica ("." ou "/") sempre existe.
    const std::string normalized = path.normalized().str();
    return normalized == "." || normalized == "/" || normalized.empty();
}

Result<std::vector<std::byte>> MemoryFileSystem::readAllBytes(
    const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "readAllBytes"));
    }
    const auto it = files_.find(keyOf(path));
    if (it == files_.end()) {
        return makeUnexpected(notFound(path, "readAllBytes"));
    }
    return it->second;
}

Result<std::string> MemoryFileSystem::readAllText(const Path& path) const
{
    Result<std::vector<std::byte>> bytes = readAllBytes(path);
    if (bytes.isError()) {
        return makeUnexpected(bytes.error());
    }
    const auto& raw = bytes.value();
    return std::string(reinterpret_cast<const char*>(raw.data()), raw.size());
}

Result<void> MemoryFileSystem::writeAllBytes(const Path& path,
                                             std::span<const std::byte> bytes)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "writeAllBytes"));
    }
    const std::string key = keyOf(path);
    if (dirs_.count(key) != 0) {
        return makeUnexpected(Error{
            StatusCode::IOError,
            "writeAllBytes: '" + path.str() + "' é um diretório"});
    }
    if (!parentDirExists(key)) {
        return makeUnexpected(Error{
            StatusCode::NotFound,
            "writeAllBytes: diretório pai de '" + path.str() +
                "' não existe (writeAll não cria pais)"});
    }
    files_[key] = std::vector<std::byte>(bytes.begin(), bytes.end());
    return {};
}

Result<void> MemoryFileSystem::writeAllText(const Path& path,
                                            std::string_view text)
{
    const std::span<const std::byte> bytes{
        reinterpret_cast<const std::byte*>(text.data()), text.size()};
    return writeAllBytes(path, bytes);
}

Result<bool> MemoryFileSystem::remove(const Path& path)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "remove"));
    }
    const std::string key = keyOf(path);
    if (files_.erase(key) != 0) {
        return true;
    }
    if (dirs_.count(key) != 0) {
        // Diretório vazio = sem arquivos nem subdiretórios com este prefixo.
        const std::string prefix = key + "/";
        const auto dirIt = dirs_.lower_bound(prefix);
        const bool hasDirChildren =
            dirIt != dirs_.end() && dirIt->rfind(prefix, 0) == 0;
        const auto fileIt = files_.lower_bound(prefix);
        const bool hasFileChildren =
            fileIt != files_.end() && fileIt->first.rfind(prefix, 0) == 0;
        if (!hasDirChildren && !hasFileChildren) {
            dirs_.erase(key);
            return true;
        }
        return false; // não-vazio: nada removido, não é erro (paridade native)
    }
    return false;
}

Result<void> MemoryFileSystem::rename(const Path& from, const Path& to)
{
    if (!from.valid() || !to.valid()) {
        return makeUnexpected(invalidPath(from, "rename"));
    }
    const std::string fromKey = keyOf(from);
    const std::string toKey = keyOf(to);

    if (dirs_.count(fromKey) != 0) {
        // Move a árvore: re-chaveia arquivos e subdiretórios com o prefixo.
        const std::string prefix = fromKey + "/";
        std::map<std::string, std::vector<std::byte>> movedFiles;
        std::set<std::string> movedDirs;
        for (auto it = files_.begin(); it != files_.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                movedFiles.emplace(toKey + "/" + it->first.substr(prefix.size()),
                                   std::move(it->second));
                it = files_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = dirs_.begin(); it != dirs_.end();) {
            if (it->rfind(prefix, 0) == 0) {
                movedDirs.insert(toKey + "/" + it->substr(prefix.size()));
                it = dirs_.erase(it);
            } else {
                ++it;
            }
        }
        dirs_.erase(fromKey);
        dirs_.insert(toKey);
        files_.merge(movedFiles);
        dirs_.merge(movedDirs);
        return {};
    }

    const auto it = files_.find(fromKey);
    if (it == files_.end()) {
        return makeUnexpected(notFound(from, "rename"));
    }
    if (dirs_.count(toKey) != 0) {
        return makeUnexpected(Error{
            StatusCode::IOError,
            "rename: destino '" + to.str() + "' é um diretório"});
    }
    std::vector<std::byte> payload = std::move(it->second);
    files_.erase(it);
    files_[toKey] = std::move(payload);
    return {};
}

Result<void> MemoryFileSystem::mkdirs(const Path& path)
{
    if (!path.valid()) {
        return makeUnexpected(invalidPath(path, "mkdirs"));
    }
    const std::string key = keyOf(path);
    if (files_.count(key) != 0) {
        return makeUnexpected(Error{
            StatusCode::IOError,
            "mkdirs: '" + path.str() + "' existe e é arquivo"});
    }
    if (key == "." || key == "/" || key.empty()) {
        return {}; // raiz lógica
    }
    // Cria todos os componentes.
    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        pos = key.find('/', pos + 1);
        const std::string component =
            pos == std::string::npos ? key : key.substr(0, pos);
        if (!component.empty() && component != "/" && component != ".") {
            dirs_.insert(component);
        }
    }
    return {};
}

Result<std::vector<ListEntry>> MemoryFileSystem::list(const Path& dir,
                                                      bool recursive) const
{
    if (!dir.valid()) {
        return makeUnexpected(invalidPath(dir, "list"));
    }
    const std::string key = keyOf(dir);
    const bool rootLike =
        key == "." || key == "/" || key.empty();
    const std::string prefix = rootLike ? std::string() : key + "/";

    // Diretório precisa existir (raiz lógica sempre existe).
    if (!rootLike && dirs_.count(key) == 0) {
        return makeUnexpected(notFound(dir, "list"));
    }

    std::vector<ListEntry> entries;
    const auto consider = [&](const std::string& full, bool isDir) {
        if (!rootLike && full.rfind(prefix, 0) != 0) {
            return;
        }
        if (full.empty() || full == key) {
            return;
        }
        if (recursive) {
            entries.push_back(
                ListEntry{Path(std::string_view{full}), isDir});
            return;
        }
        // Raso: componente imediatamente abaixo do diretório listado.
        const std::string rest = full.substr(prefix.size());
        if (rest.find('/') == std::string::npos) {
            entries.push_back(
                ListEntry{Path(std::string_view{full}), isDir});
        }
    };
    for (const auto& [full, payload] : files_) {
        (void)payload;
        consider(full, false);
    }
    for (const auto& full : dirs_) {
        consider(full, true);
    }
    std::sort(entries.begin(), entries.end(),
              [](const ListEntry& a, const ListEntry& b) {
                  return a.path.str() < b.path.str();
              });
    return entries;
}

} // namespace eng::fs
