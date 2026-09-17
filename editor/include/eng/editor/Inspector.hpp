#pragma once

/// eng::editor::Inspector — acesso genérico a componentes/campos via
/// reflection (FASE 8, missão §8.4).
///
/// - Catálogo: `eng::scene::detail::componentEntries()` (ÚNICO registry —
///   auditoria D2; inclui os built-ins Name/Transform e tudo que as fases
///   futuras registrarem — física/animação/etc aparecem automaticamente).
/// - Campos: `TypeInfo::properties` (offset + typeName). Valores trafegam
///   como STRING (boundary neutra para JNI — audit §4):
///     bool → "true"/"false"; inteiros → decimal; f32/f64 → %g;
///     string → como está; enum → NOME do enumerador;
///     struct conhecida (Vec3/Quat) → subcampos por caminho "position.x".
/// - Escrita: parse por tipo → escrita por offset. Erros precisos (campo
///   desconhecido, valor inválido, entidade obsoleta, componente ausente).
///
/// Nada aqui conhece componentes específicos: Transform é lido como
/// QUALQUER struct refletida — o Inspector não tem um único if de
/// "position" (missão §8.4: não hard-code).

#include <string>
#include <string_view>
#include <vector>

#include "eng/core/Result.hpp"
#include "eng/ecs/Ecs.hpp"
#include "eng/scene/Scene.hpp"

namespace eng::editor {

class Inspector final {
public:
    Inspector() = delete;

    /// Um campo achatado do Inspector (primitivo folha).
    struct Field {
        std::string path;     ///< "position.x", "value", "rotation.w"
        std::string typeName; ///< "f32", "string", nome do enum...
        std::string value;    ///< representação textual
    };

    /// Catálogo completo de componentes registrados (ordenado por nome).
    [[nodiscard]] static std::vector<std::string> catalog();

    /// Componentes PRESENTES na entidade (ordenados por nome).
    [[nodiscard]] static std::vector<std::string> componentsOf(
        const eng::scene::Scene& scene, eng::ecs::Entity entity);

    /// Campos achatados do componente na entidade. Erro: componente ausente.
    [[nodiscard]] static eng::core::Result<std::vector<Field>> fieldsOf(
        const eng::scene::Scene& scene, eng::ecs::Entity entity,
        std::string_view component);

    /// Lê um campo por caminho ("position.x"). Erros precisos.
    [[nodiscard]] static eng::core::Result<std::string> getField(
        const eng::scene::Scene& scene, eng::ecs::Entity entity,
        std::string_view component, std::string_view fieldPath);

    /// Escreve um campo por caminho (parse por tipo). Marca a cena suja.
    /// Erros precisos; NUNCA escreve parcialmente.
    [[nodiscard]] static eng::core::Result<void> setField(
        eng::scene::Scene& scene, eng::ecs::Entity entity,
        std::string_view component, std::string_view fieldPath,
        std::string_view value);

    /// Componentes protegidos de remoção (integridade da cena/serializer).
    /// Transform é obrigatório (nó); Name é o rótulo mínimo do editor.
    [[nodiscard]] static bool isRemovable(std::string_view component);

    /// Adiciona componente default-construído via catálogo (D2).
    [[nodiscard]] static eng::core::Result<void> addComponent(
        eng::scene::Scene& scene, eng::ecs::Entity entity,
        std::string_view component);

    /// Remove componente (recusa protegidos).
    [[nodiscard]] static eng::core::Result<void> removeComponent(
        eng::scene::Scene& scene, eng::ecs::Entity entity,
        std::string_view component);
};

} // namespace eng::editor
