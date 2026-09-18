#pragma once

/// eng::editor::NiScriptComponent — script NI-Script anexado a um nó
/// (FASE 11, missão: integração com o PLAY do editor).
///
/// Componente de GAMEPLAY registrado no catálogo ÚNICO do SceneSerializer
/// (ADR-043 — mesmo padrão de RigidBody/Animator/ParticleEmitter: o
/// registro vive no CONSUMIDOR, engine/scene não conhece scripting).
///
/// - `source` é o texto .nis COMPLETO (editável pelo Inspector existente —
///   campo string; UI dedicada de script é FUTURO planejado, ver
///   docs/ni-script/08-tools.md — adiada e declarada);
/// - em PLAY, o EditorDocument compila o source de cada instância no
///   CLONE (ADR-044: a edição nunca é tocada), instancia o NiScriptState
///   e roda @init → up start → up update (por tick) → up destroy.
///
/// Semântica de falha (honestidade): erro de COMPILAÇÃO → script
/// desabilitado com log (a cena continua); FAULT de runtime → registrado
/// em lastFault (design §5.3.5 — o script não morre).

#include <string>

#include "eng/reflect/Reflect.hpp"

namespace eng::editor {

struct NiScriptComponent {
    std::string source; ///< fonte .nis completa
};

} // namespace eng::editor

ENG_REFLECT_BEGIN(eng::editor::NiScriptComponent)
    ENG_REFLECT_FIELD(source)
ENG_REFLECT_END()
