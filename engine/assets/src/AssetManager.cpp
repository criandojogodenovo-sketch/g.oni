#include "eng/assets/AssetManager.hpp"

#include "eng/serial/Json.hpp"

namespace eng::assets {

AssetManager::AssetManager(const AssetRegistry& registry,
                           AssetResolver resolver,
                           const eng::fs::FileSystem& fs)
    : registry_(registry), resolver_(std::move(resolver)), fs_(fs)
{
}

bool JsonAssetLoader::supports(AssetType type) const
{
    // O CONTEÚDO dos três é JSON — a interpretação estrutural é do
    // consumidor (desvio D4 da auditoria: assets não depende de scene).
    return type == AssetType::Scene || type == AssetType::Prefab ||
           type == AssetType::Json;
}

eng::core::Result<std::shared_ptr<eng::serial::JsonValue>>
JsonAssetLoader::load(const AssetMeta& meta,
                      std::span<const std::byte> bytes) const
{
    const std::string text(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
    const auto parsed = eng::serial::parseJson(text);
    if (parsed.isError()) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::ParseError,
            "JsonAssetLoader: asset " + meta.id.toString() + ": " +
                parsed.error().message});
    }
    return std::make_shared<eng::serial::JsonValue>(std::move(parsed.value()));
}

} // namespace eng::assets
