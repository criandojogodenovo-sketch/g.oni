#include "eng/assets/AssetResolver.hpp"

namespace eng::assets {

namespace {

using eng::core::Error;
using eng::core::StatusCode;
using eng::core::makeUnexpected;

} // namespace

AssetResolver::AssetResolver(const AssetRegistry& registry,
                             eng::fs::Path assetsRoot)
    : registry_(&registry), assetsRoot_(std::move(assetsRoot))
{
}

eng::core::Result<eng::fs::Path> AssetResolver::resolve(AssetId id) const
{
    const AssetMeta* meta = registry_->find(id);
    if (meta == nullptr) {
        return makeUnexpected(
            Error{StatusCode::NotFound,
                  "AssetResolver: asset " + id.toString() +
                      " não está no registry"});
    }

    if (meta->sourcePath.isAbsolute()) {
        return makeUnexpected(
            Error{StatusCode::InvalidArgument,
                  "AssetResolver: sourcePath absoluto é proibido ('" +
                      meta->sourcePath.str() + "') — paths do registry são "
                      "relativos à raiz de assets"});
    }

    // Anti-traversal (missão §4.6): join + normalize tem que ficar DENTRO
    // da raiz — verificação por componente (fs::Path::isWithin).
    const eng::fs::Path resolved =
        (assetsRoot_ / meta->sourcePath).normalized();
    if (!resolved.isWithin(assetsRoot_.normalized())) {
        return makeUnexpected(
            Error{StatusCode::InvalidArgument,
                  "AssetResolver: sourcePath escapa da raiz de assets ('" +
                      meta->sourcePath.str() + "')"});
    }
    return resolved;
}

eng::core::Result<std::vector<std::byte>> AssetResolver::read(
    AssetId id, const eng::fs::FileSystem& fs) const
{
    const auto resolved = resolve(id);
    if (resolved.isError()) {
        return makeUnexpected(resolved.error());
    }
    return fs.readAllBytes(resolved.value());
}

} // namespace eng::assets
