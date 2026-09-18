#include "eng/fs/RootedFileSystem.hpp"

/// Implementação — ver contrato no header (RECOVERY P0: fronteira do
/// workspace; os paths do editor são relativos, o absoluto morre AQUI).

#include <utility>

namespace eng::fs {

namespace {

using eng::core::Error;
using eng::core::Result;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

[[nodiscard]] Error rootedError(StatusCode code, std::string message)
{
    return Error{code, "RootedFileSystem: " + std::move(message)};
}

/// O path normalizado começa com ".." (escape do root)?
/// (".." no MEIO já foi colapsado por normalized(); sobrevive no topo
/// apenas quando de fato escapa. Path::str() é SEMPRE forma genérica
/// ('/' separador) — checagem por componente via prefixo "../".)
[[nodiscard]] bool escapesRoot(const Path& normalized) noexcept
{
    const std::string text = normalized.str();
    return text == ".." || text.rfind("../", 0) == 0;
}

}  // namespace

RootedFileSystem::RootedFileSystem(FileSystem& base, const Path& root)
    : base_(&base), root_(root.normalized())
{
}

Result<Path> RootedFileSystem::mapIn(const Path& path) const
{
    if (!path.valid()) {
        return makeUnexpected(rootedError(
            StatusCode::InvalidArgument,
            "path inválido (vazio ou com NUL)"));
    }
    if (path.isAbsolute()) {
        return makeUnexpected(rootedError(
            StatusCode::InvalidArgument,
            "path absoluto proibido na fronteira do workspace ('" +
                path.str() + "') — converta para relativo ao root"));
    }
    const Path normalizedInput = path.normalized();
    if (escapesRoot(normalizedInput)) {
        return makeUnexpected(rootedError(
            StatusCode::InvalidArgument,
            "path escapa do workspace ('" + path.str() + "')"));
    }
    // "." entra aqui: (root / ".").normalized() == root — o próprio
    // workspace (ex.: exists(".") pergunta "o workspace existe?").
    return (root_ / normalizedInput).normalized();
}

Result<bool> RootedFileSystem::exists(const Path& path) const
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->exists(mapped.value());
}

Result<std::vector<std::byte>> RootedFileSystem::readAllBytes(
    const Path& path) const
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->readAllBytes(mapped.value());
}

Result<std::string> RootedFileSystem::readAllText(const Path& path) const
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->readAllText(mapped.value());
}

Result<void> RootedFileSystem::writeAllBytes(const Path& path,
                                             std::span<const std::byte> bytes)
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->writeAllBytes(mapped.value(), bytes);
}

Result<void> RootedFileSystem::writeAllText(const Path& path,
                                            std::string_view text)
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->writeAllText(mapped.value(), text);
}

Result<bool> RootedFileSystem::remove(const Path& path)
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->remove(mapped.value());
}

Result<void> RootedFileSystem::rename(const Path& from, const Path& to)
{
    auto mappedFrom = mapIn(from);
    if (mappedFrom.isError()) {
        return makeUnexpected(mappedFrom.error());
    }
    auto mappedTo = mapIn(to);
    if (mappedTo.isError()) {
        return makeUnexpected(mappedTo.error());
    }
    return base_->rename(mappedFrom.value(), mappedTo.value());
}

Result<void> RootedFileSystem::mkdirs(const Path& path)
{
    auto mapped = mapIn(path);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    return base_->mkdirs(mapped.value());
}

Result<std::vector<ListEntry>> RootedFileSystem::list(
    const Path& dir, bool recursive) const
{
    auto mapped = mapIn(dir);
    if (mapped.isError()) {
        return makeUnexpected(mapped.error());
    }
    auto listed = base_->list(mapped.value(), recursive);
    if (listed.isError()) {
        return makeUnexpected(listed.error());
    }
    // Re-relativiza: o chamador vê o MESMO path que passaria para voltar
    // ao arquivo (forma normalizada relativa ao root — nunca o absoluto).
    std::vector<ListEntry> entries;
    entries.reserve(listed.value().size());
    for (ListEntry& entry : listed.value()) {
        std::filesystem::path rel =
            entry.path.native().lexically_relative(root_.native());
        if (rel.empty() || escapesRoot(Path::fromNative(rel))) {
            return makeUnexpected(rootedError(
                StatusCode::Internal,
                "entrada de listagem fora do root ('" + entry.path.str() +
                    "')"));
        }
        entries.push_back(
            ListEntry{Path::fromNative(std::move(rel)), entry.isDirectory});
    }
    return entries;
}

} // namespace eng::fs
