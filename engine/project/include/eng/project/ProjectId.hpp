#pragma once

/// eng::project::ProjectId — identidade de projeto (FASE 3; ADR-032).
/// Tipo FORTE sobre core::Uuid128 — não conversível com AssetId/
/// SceneEntityId (mesma decisão de ADR-028).
#include <array>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "eng/core/Result.hpp"
#include "eng/core/Uuid.hpp"

namespace eng::project {

struct ProjectId {
    eng::core::Uuid128 uuid{};

    [[nodiscard]] bool isNil() const noexcept { return uuid.isNil(); }

    [[nodiscard]] static ProjectId generate()
    {
        return ProjectId{eng::core::Uuid128::generate()};
    }

    [[nodiscard]] static eng::core::Result<ProjectId> fromString(
        std::string_view text)
    {
        const auto uuid = eng::core::Uuid128::fromString(text);
        if (uuid.isError()) {
            return eng::core::makeUnexpected(uuid.error());
        }
        return ProjectId{uuid.value()};
    }

    [[nodiscard]] std::string toString() const { return uuid.toString(); }

    [[nodiscard]] friend bool operator==(const ProjectId&,
                                         const ProjectId&) noexcept = default;
    [[nodiscard]] friend std::strong_ordering operator<=>(
        const ProjectId& a, const ProjectId& b) noexcept
    {
        return a.uuid <=> b.uuid;
    }
};

} // namespace eng::project

template<>
struct std::hash<eng::project::ProjectId> {
    [[nodiscard]] std::size_t operator()(
        const eng::project::ProjectId& id) const noexcept
    {
        return std::hash<eng::core::Uuid128>{}(id.uuid);
    }
};
