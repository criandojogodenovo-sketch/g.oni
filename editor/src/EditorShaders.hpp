#pragma once

/// EditorShaders — camada de COMPATIBILIDADE (P3 §2): as fontes de shader
/// mudaram-se para o módulo eng::render (Shader Core do bloco P3); este
/// header mantém os nomes históricos (tests/hosts antigos) sem duplicar
/// NENHUM byte — apenas reexporta.

#include "eng/render/RenderShaders.hpp"

namespace eng::editor {

using eng::render::kEditorVertexGlsl;
using eng::render::kEditorFragmentGlsl;
using eng::render::kEditorVertexSpirv;
using eng::render::kEditorFragmentSpirv;
using eng::render::kEditorVertexSpirvBytes;
using eng::render::kEditorFragmentSpirvBytes;
using eng::render::kSpriteVertexGlsl;
using eng::render::kSpriteFragmentGlsl;
using eng::render::kSpriteVertexSpirv;
using eng::render::kSpriteFragmentSpirv;
using eng::render::kSpriteVertexSpirvBytes;
using eng::render::kSpriteFragmentSpirvBytes;

}  // namespace eng::editor
