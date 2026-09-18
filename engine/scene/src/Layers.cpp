#include "eng/scene/Layers.hpp"

/// LayerRegistry — implementação (evolução P0-5; ADR-051).

#include <algorithm>

namespace eng::scene {

namespace {

/// Definição default de uma camada recém-criada.
[[nodiscard]] LayerDefinition makeDefault(std::string name)
{
    LayerDefinition definition;
    definition.name = std::move(name);
    return definition;
}

}  // namespace

LayerRegistry::LayerRegistry()
{
    layers_.push_back(makeDefault(std::string(kGame)));
    layers_.push_back(makeDefault(std::string(kSubgame)));
}

const LayerDefinition* LayerRegistry::find(std::string_view name) const
{
    for (const LayerDefinition& layer : layers_) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

LayerDefinition* LayerRegistry::findMutable(std::string_view name)
{
    for (LayerDefinition& layer : layers_) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

eng::core::Result<void> LayerRegistry::addLayer(std::string_view name)
{
    using eng::core::Error;
    using eng::core::StatusCode;

    if (name.empty()) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument, "LayerRegistry::addLayer: vazio"});
    }
    if (has(name)) {
        return eng::core::makeUnexpected(Error{
            StatusCode::AlreadyExists,
            "LayerRegistry::addLayer: camada '" + std::string(name) +
                "' já existe"});
    }
    layers_.push_back(makeDefault(std::string(name)));
    return {};
}

eng::core::Result<void> LayerRegistry::remove(std::string_view name)
{
    using eng::core::Error;
    using eng::core::StatusCode;

    if (name == kGame || name == kSubgame) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "LayerRegistry::remove: built-in '" + std::string(name) +
                "' é permanente"});
    }
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                  [name](const LayerDefinition& layer) {
                                      return layer.name == name;
                                  });
    if (it == layers_.end()) {
        return eng::core::makeUnexpected(Error{
            StatusCode::NotFound,
            "LayerRegistry::remove: camada '" + std::string(name) +
                "' não existe"});
    }
    layers_.erase(it);
    return {};
}

eng::core::Result<void> LayerRegistry::setParticipation(
    std::string_view name, LayerParticipation participation)
{
    LayerDefinition* layer = findMutable(name);
    if (layer == nullptr) {
        return eng::core::makeUnexpected(eng::core::Error{
            eng::core::StatusCode::NotFound,
            "LayerRegistry::setParticipation: camada '" + std::string(name) +
                "' não existe"});
    }
    layer->participation = participation;
    return {};
}

eng::core::Result<void> LayerRegistry::setTimeScale(std::string_view name,
                                                    float timeScale)
{
    using eng::core::Error;
    using eng::core::StatusCode;

    if (!(timeScale >= 0.f) || timeScale > 1000.f) {
        return eng::core::makeUnexpected(Error{
            StatusCode::InvalidArgument,
            "LayerRegistry::setTimeScale: valor inválido (>= 0)"});
    }
    LayerDefinition* layer = findMutable(name);
    if (layer == nullptr) {
        return eng::core::makeUnexpected(Error{
            StatusCode::NotFound,
            "LayerRegistry::setTimeScale: camada '" + std::string(name) +
                "' não existe"});
    }
    layer->timeScale = timeScale;
    return {};
}

bool LayerRegistry::isDefault(const LayerDefinition& definition)
{
    return definition.timeScale == 1.f &&
           definition.participation == LayerParticipation{};
}

void LayerRegistry::clear() noexcept
{
    layers_.clear();
    layers_.push_back(makeDefault(std::string(kGame)));
    layers_.push_back(makeDefault(std::string(kSubgame)));
}

}  // namespace eng::scene
