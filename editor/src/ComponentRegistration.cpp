#include "eng/animation/Animation.hpp"

#include "eng/editor/AudioSource.hpp"
#include "eng/editor/EditorDocument.hpp"
#include "eng/editor/NiRuntime.hpp"
#include "eng/editor/NiScriptComponent.hpp"
#include "eng/editor/SpriteData.hpp"
#include "eng/particles/Particles.hpp"
#include "eng/render/Light2D.hpp"
#include "eng/physics/Physics.hpp"
#include "eng/scene/Layers.hpp"
#include "eng/scene/SceneSerializer.hpp"
#include "eng/tick/Camera.hpp"

/// Registro dos componentes de GAMEPLAY (FASE 10, missão §8 integração):
/// physics/animation/particles entram no CATÁLOGO ÚNICO do serializer —
/// aparecem no inspector do editor e persistem em cena (ADR-043). O
/// registro vive no CONSUMIDOR (editor) porque engine/scene não pode
/// depender das camadas de gameplay (grafo 00-overview).

namespace eng::editor {

namespace {

const bool goni_editor_components_registered = [] {
    using eng::scene::SceneSerializer;
    (void)SceneSerializer::registerComponentType<eng::physics::RigidBody>(
        "eng::physics::RigidBody");
    (void)SceneSerializer::registerComponentType<eng::physics::Collider>(
        "eng::physics::Collider");
    (void)SceneSerializer::registerComponentType<
        eng::physics::CharacterBody>("eng::physics::CharacterBody");
    (void)SceneSerializer::registerComponentType<eng::animation::Animator>(
        "eng::animation::Animator");
    (void)SceneSerializer::registerComponentType<
        eng::particles::ParticleEmitter>("eng::particles::ParticleEmitter");
    // Evolução P0-3: sprite com textura real (workflow importar→ver na cena).
    (void)SceneSerializer::registerComponentType<eng::editor::SpriteData>(
        "eng::editor::SpriteData");
    // FASE 11: scripts NI-Script anexados a nós (ADR-043 — mesmo catálogo)
    (void)SceneSerializer::registerComponentType<
        eng::editor::NiScriptComponent>("eng::editor::NiScriptComponent");
    // Evolução P0-5 (ADR-051): câmera de jogo como cidadã da cena +
    // membro de camada (GAME/SUBGAME/nomeadas) — Inspector/persistência/clone.
    (void)SceneSerializer::registerComponentType<eng::tick::CameraData>(
        "eng::tick::CameraData");
    (void)SceneSerializer::registerComponentType<eng::scene::LayerMember>(
        "eng::scene::LayerMember");
    // P2 (§12): AudioSource — áudio authorável dirigindo o AudioMixer
    // REAL (AudioTick do documento no Play).
    (void)SceneSerializer::registerComponentType<eng::editor::AudioSource>(
        "eng::editor::AudioSource");
    // P3: Light2D — luz 2D real alimentando o bloco PerFrame do sprite.lit
    // (renderiza no Edit e no Play; clone via serializacao).
    (void)SceneSerializer::registerComponentType<eng::render::Light2D>(
        "eng::render::Light2D");
    return true;
}();

}  // namespace

// (Símbolo exported para garantir que o TU entre no link — o registro é
// efeito colateral da inicialização estática; sem isso o linker pode
// descartar o objeto.)
void ensureEditorComponentsRegistered() noexcept
{
    (void)goni_editor_components_registered;
}

}  // namespace eng::editor
