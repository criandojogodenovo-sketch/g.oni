#include "eng/project/ProjectFile.hpp"

#include "eng/serial/Json.hpp"

namespace eng::project {

namespace {

using eng::core::Error;
using eng::core::StatusCode;
using eng::core::makeUnexpected;
using eng::serial::JsonValue;

[[nodiscard]] Error bad(const std::string& what)
{
    return Error{StatusCode::ParseError, "ProjectFile: " + what};
}

/// Valida que `value` é um path RELATIVO válido (regra dura §2.6).
[[nodiscard]] eng::core::Result<eng::fs::Path> relativePathField(
    const JsonValue& value, const char* fieldName)
{
    if (!value.isString()) {
        return makeUnexpected(
            bad(std::string(fieldName) + " ausente ou não-string"));
    }
    const eng::fs::Path path{value.asString()};
    if (path.isAbsolute()) {
        return makeUnexpected(Error{
            StatusCode::InvalidArgument,
            std::string("ProjectFile: ") + fieldName +
                " ABSOLUTO é proibido ('" + path.str() +
                "') — paths do projeto são relativos"});
    }
    if (!path.valid()) {
        return makeUnexpected(
            bad(std::string(fieldName) + " inválido (vazio/NUL)"));
    }
    return path;
}

} // namespace

eng::core::Result<ProjectConfig> ProjectFile::configFromJson(
    const JsonValue& value)
{
    if (!value.isObject()) {
        return makeUnexpected(bad("raiz não é objeto"));
    }

    const auto format = value.find("formatVersion");
    if (!format.has_value() || !format->isUnsigned() ||
        format->asU64() > kProjectFormatVersion) {
        return makeUnexpected(Error{
            StatusCode::NotSupported,
            "ProjectFile: formatVersion ausente/inválida/maior que a "
            "suportada (" +
                std::to_string(kProjectFormatVersion) + ")"});
    }

    const auto id = value.find("projectId");
    if (!id.has_value() || !id->isString()) {
        return makeUnexpected(bad("projectId ausente ou não-string"));
    }
    const auto projectId = ProjectId::fromString(id->asString());
    if (projectId.isError()) {
        return makeUnexpected(projectId.error());
    }

    const auto name = value.find("name");
    if (!name.has_value() || !name->isString() || name->asString().empty()) {
        return makeUnexpected(bad("name ausente, não-string ou vazio"));
    }

    const auto engine = value.find("engineVersion");
    if (!engine.has_value() || !engine->isString()) {
        return makeUnexpected(
            bad("engineVersion ausente ou não-string"));
    }
    const auto engineVersion =
        eng::core::Version::parse(engine->asString());
    if (engineVersion.isError()) {
        return makeUnexpected(engineVersion.error());
    }

    const auto registry = value.find("assetRegistryPath");
    if (!registry.has_value()) {
        return makeUnexpected(bad("assetRegistryPath ausente"));
    }
    const auto assetRegistryPath =
        relativePathField(*registry, "assetRegistryPath");
    if (assetRegistryPath.isError()) {
        return makeUnexpected(assetRegistryPath.error());
    }

    std::vector<eng::fs::Path> sceneRoots;
    const auto roots = value.find("sceneRoots");
    if (roots.has_value()) {
        if (!roots->isArray()) {
            return makeUnexpected(bad("sceneRoots não é array"));
        }
        for (std::size_t i = 0; i < roots->size(); ++i) {
            const auto root =
                relativePathField(roots->at(i), "sceneRoots[]");
            if (root.isError()) {
                return makeUnexpected(root.error());
            }
            sceneRoots.push_back(root.value());
        }
    }

    ProjectConfig config;
    config.projectId = projectId.value();
    config.name = name->asString();
    config.engineVersion = engineVersion.value();
    config.assetRegistryPath = assetRegistryPath.value();
    config.sceneRoots = std::move(sceneRoots);
    return config;
}

eng::core::Result<eng::serial::JsonValue> ProjectFile::toJson(
    const ProjectConfig& config)
{
    JsonValue roots = JsonValue::array();
    for (const eng::fs::Path& root : config.sceneRoots) {
        roots.append(JsonValue::string(root.str()));
    }

    JsonValue value = JsonValue::object();
    value.set("formatVersion", JsonValue::uinteger(kProjectFormatVersion));
    value.set("projectId", JsonValue::string(config.projectId.toString()));
    value.set("name", JsonValue::string(config.name));
    value.set("engineVersion",
              JsonValue::string(config.engineVersion.toString()));
    value.set("assetRegistryPath",
              JsonValue::string(config.assetRegistryPath.str()));
    value.set("sceneRoots", std::move(roots));
    return value;
}

eng::core::Result<ProjectFile> ProjectFile::parse(
    const eng::fs::Path& filePath, std::string_view text)
{
    const auto parsed = eng::serial::parseJson(text);
    if (parsed.isError()) {
        return makeUnexpected(parsed.error());
    }
    const auto config = configFromJson(parsed.value());
    if (config.isError()) {
        return makeUnexpected(config.error());
    }
    ProjectFile file;
    file.config = config.value();
    file.filePath = filePath;
    return file;
}

eng::core::Result<ProjectFile> ProjectFile::readFrom(
    const eng::fs::FileSystem& fs, const eng::fs::Path& filePath)
{
    const auto text = fs.readAllText(filePath);
    if (text.isError()) {
        return makeUnexpected(text.error());
    }
    return parse(filePath, text.value());
}

eng::core::Result<std::string> ProjectFile::serialize() const
{
    const auto json = toJson(config);
    if (json.isError()) {
        return makeUnexpected(json.error());
    }
    return eng::serial::dumpJson(json.value());
}

eng::core::Result<void> ProjectFile::writeTo(eng::fs::FileSystem& fs) const
{
    const auto text = serialize();
    if (text.isError()) {
        return makeUnexpected(text.error());
    }
    return fs.writeAllText(filePath, text.value());
}

} // namespace eng::project
