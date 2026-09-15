#include "eng/assets/AssetType.hpp"

namespace eng::assets {

std::string_view assetTypeName(AssetType type) noexcept
{
    switch (type) {
    case AssetType::Scene: return "Scene";
    case AssetType::Prefab: return "Prefab";
    case AssetType::Json: return "Json";
    case AssetType::Texture: return "Texture";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Shader: return "Shader";
    case AssetType::Audio: return "Audio";
    case AssetType::Script: return "Script";
    case AssetType::Unknown: break;
    }
    return "Unknown";
}

eng::core::Result<AssetType> assetTypeFromName(std::string_view name)
{
    for (const auto type : {AssetType::Scene, AssetType::Prefab,
                            AssetType::Json, AssetType::Texture,
                            AssetType::Mesh, AssetType::Material,
                            AssetType::Shader, AssetType::Audio,
                            AssetType::Script, AssetType::Unknown}) {
        if (name == assetTypeName(type)) {
            return type;
        }
    }
    return eng::core::makeUnexpected(eng::core::Error{
        eng::core::StatusCode::ParseError,
        "AssetType desconhecido: '" + std::string(name) + "'"});
}

} // namespace eng::assets
