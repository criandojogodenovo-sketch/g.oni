/**
 * G.oni Objetos — Sistema de entidades da cena.
 *
 * Cada G.oni Objeto é uma entidade com:
 *  - Transform (posição, rotação, escala)
 *  - Componentes (mesh, collider, rigidbody, script, luz, câmera, animação)
 *  - Hierarquia pai-filho
 *  - Metadados personalizados (tags, dados livres)
 *  - Barramento de sinais próprio (G.oni Signal)
 */

import { Transform, Mat4, Vec3 } from '../math';
import { Material } from '../render/Material';
import { Light } from '../render/Light';
import { SignalBus } from './Signal';
import { Collider, RigidBody } from '../physics/RigidBody';
import { AnimationClip } from './Animation';

let objectCounter = 0;

export type ComponentType =
  | 'mesh' | 'light' | 'camera' | 'collider'
  | 'rigidbody' | 'script' | 'animation';

export abstract class Component {
  abstract readonly type: ComponentType;
  /** id do objeto dono — preenchido por GOniObject.addComponent. */
  ownerId = '';
  enabled = true;

  abstract serialize(): Record<string, unknown>;
}

export class MeshComponent extends Component {
  readonly type = 'mesh' as const;
  /** id no MeshRegistry: "prim:cube", "asset:<id>" */
  meshId = 'prim:cube';
  material = new Material();
  /** LOD automático por distância (0 = desligado). */
  lodDistance = 0;
  lodMeshId: string | null = null;

  serialize(): Record<string, unknown> {
    return {
      meshId: this.meshId,
      material: this.material.serialize(),
      lodDistance: this.lodDistance,
      lodMeshId: this.lodMeshId,
    };
  }

  static deserialize(d: Record<string, unknown>): MeshComponent {
    const c = new MeshComponent();
    c.meshId = String(d.meshId ?? 'prim:cube');
    if (d.material) c.material = Material.deserialize(d.material as Record<string, unknown>);
    c.lodDistance = Number(d.lodDistance ?? 0);
    c.lodMeshId = (d.lodMeshId as string | null) ?? null;
    return c;
  }
}

export class LightComponent extends Component {
  readonly type = 'light' as const;
  light = new Light();

  serialize(): Record<string, unknown> { return { light: this.light.serialize() }; }

  static deserialize(d: Record<string, unknown>): LightComponent {
    const c = new LightComponent();
    if (d.light) c.light = Light.deserialize(d.light as Record<string, unknown>);
    return c;
  }
}

export class CameraComponent extends Component {
  readonly type = 'camera' as const;
  perspective = true;
  fovDegrees = 60;
  near = 0.1;
  far = 500;
  /** câmera principal usada no play mode */
  primary = false;

  serialize(): Record<string, unknown> {
    return {
      perspective: this.perspective,
      fovDegrees: this.fovDegrees,
      near: this.near,
      far: this.far,
      primary: this.primary,
    };
  }

  static deserialize(d: Record<string, unknown>): CameraComponent {
    const c = new CameraComponent();
    c.perspective = Boolean(d.perspective ?? true);
    c.fovDegrees = Number(d.fovDegrees ?? 60);
    c.near = Number(d.near ?? 0.1);
    c.far = Number(d.far ?? 500);
    c.primary = Boolean(d.primary ?? false);
    return c;
  }
}

export class ColliderComponent extends Component {
  readonly type = 'collider' as const;
  collider = new Collider();

  serialize(): Record<string, unknown> { return { collider: this.collider.serialize() }; }

  static deserialize(d: Record<string, unknown>): ColliderComponent {
    const c = new ColliderComponent();
    if (d.collider) c.collider = Collider.deserialize(d.collider as Record<string, unknown>);
    return c;
  }
}

export class RigidBodyComponent extends Component {
  readonly type = 'rigidbody' as const;
  body = new RigidBody();

  serialize(): Record<string, unknown> { return { body: this.body.serialize() }; }

  static deserialize(d: Record<string, unknown>): RigidBodyComponent {
    const c = new RigidBodyComponent();
    if (d.body) c.body = RigidBody.deserialize(d.body as Record<string, unknown>);
    return c;
  }
}

export class ScriptComponent extends Component {
  readonly type = 'script' as const;
  /** nome do script no projeto (scripts/<nome>) */
  scriptName = '';
  /** instância ativa em play mode (não serializada) */
  runtime: unknown = null;

  serialize(): Record<string, unknown> { return { scriptName: this.scriptName }; }

  static deserialize(d: Record<string, unknown>): ScriptComponent {
    const c = new ScriptComponent();
    c.scriptName = String(d.scriptName ?? '');
    return c;
  }
}

export class AnimationComponent extends Component {
  readonly type = 'animation' as const;
  clips: AnimationClip[] = [];
  playing = '';
  autoplay = false;
  loop = false;

  serialize(): Record<string, unknown> {
    return {
      clips: this.clips.map((c) => c.serialize()),
      playing: this.playing,
      autoplay: this.autoplay,
      loop: this.loop,
    };
  }

  static deserialize(d: Record<string, unknown>): AnimationComponent {
    const c = new AnimationComponent();
    c.clips = ((d.clips as Record<string, unknown>[]) ?? []).map(AnimationClip.deserialize);
    c.playing = String(d.playing ?? '');
    c.autoplay = Boolean(d.autoplay ?? false);
    c.loop = Boolean(d.loop ?? false);
    return c;
  }
}

export class GOniObject {
  readonly id = `obj_${++objectCounter}_${Math.random().toString(36).slice(2, 7)}`;
  name = 'Objeto';
  transform = new Transform();
  visible = true;
  tags: string[] = [];
  metadata: Record<string, unknown> = {};

  parent: GOniObject | null = null;
  children: GOniObject[] = [];

  components: Component[] = [];

  /** Barramento de sinais do objeto (G.oni Signal). */
  signals = new SignalBus();

  /** Matriz de mundo (computada pela Scene a cada frame). */
  worldMatrix = new Mat4();
  worldDirty = true;

  // -------- Componentes --------
  addComponent<T extends Component>(c: T): T {
    c.ownerId = this.id;
    this.components.push(c);
    this.markDirty();
    return c;
  }

  getComponent<T extends Component>(type: ComponentType): T | undefined {
    return this.components.find((c) => c.type === type) as T | undefined;
  }

  getComponents<T extends Component>(type: ComponentType): T[] {
    return this.components.filter((c) => c.type === type) as T[];
  }

  removeComponent(c: Component): void {
    const i = this.components.indexOf(c);
    if (i >= 0) this.components.splice(i, 1);
    this.markDirty();
  }

  // -------- Hierarquia --------
  addChild(child: GOniObject): void {
    if (child.parent) child.parent.removeChild(child);
    child.parent = this;
    this.children.push(child);
    child.markDirty();
  }

  removeChild(child: GOniObject): boolean {
    const i = this.children.indexOf(child);
    if (i >= 0) {
      this.children.splice(i, 1);
      child.parent = null;
      child.markDirty();
      return true;
    }
    return false;
  }

  /** Desanexa mantendo transformação de mundo aproximada. */
  detachPreservingWorld(scene: { updateWorldMatrices: () => void; findRoot: (o: GOniObject) => GOniObject }): void {
    const world = this.worldMatrix;
    void world;
    void scene;
    if (this.parent) this.parent.removeChild(this);
    this.markDirty();
  }

  isAncestorOf(o: GOniObject | null): boolean {
    let p = o?.parent ?? null;
    while (p) {
      if (p === this) return true;
      p = p.parent;
    }
    return false;
  }

  markDirty(): void {
    this.worldDirty = true;
    for (const c of this.children) c.markDirty();
  }

  /** Posição de mundo (computada a partir da matriz de mundo). */
  getWorldPosition(): Vec3 {
    const m = this.worldMatrix.m;
    return new Vec3(m[12], m[13], m[14]);
  }

  /** Caminho hierárquico ("Pai/Filho/Neto"). */
  path(): string {
    if (!this.parent) return this.name;
    return `${this.parent.path()}/${this.name}`;
  }

  findDescendant(name: string): GOniObject | null {
    for (const c of this.children) {
      if (c.name === name) return c;
      const found = c.findDescendant(name);
      if (found) return found;
    }
    return null;
  }

  /** Deleta a subárvore inteira (G.oni Eliminação Profissional). */
  traverse(cb: (o: GOniObject) => void): void {
    cb(this);
    for (const c of this.children) c.traverse(cb);
  }

  serialize(): Record<string, unknown> {
    return {
      id: this.id,
      name: this.name,
      transform: {
        position: this.transform.position.toArray(),
        rotation: this.transform.rotation.toArray(),
        scale: this.transform.scale.toArray(),
      },
      visible: this.visible,
      tags: this.tags,
      metadata: this.metadata,
      components: this.components.map((c) => ({ type: c.type, ...c.serialize() })),
      children: this.children.map((c) => c.serialize()),
    };
  }

  static deserialize(d: Record<string, unknown>): GOniObject {
    const o = new GOniObject();
    if (typeof d.id === 'string' && d.id) {
      // mantém o id original para preservar links ao recarregar
      (o as { id: string }).id = d.id;
      // garante que novos objetos não colidam com ids carregados
      const n = parseInt(String(d.id).split('_')[1] ?? '0', 10);
      if (!isNaN(n) && n > objectCounter) objectCounter = n;
    }
    o.name = String(d.name ?? 'Objeto');
    const t = (d.transform as Record<string, number[]>) ?? {};
    o.transform.position.fromArray(t.position ?? [0, 0, 0]);
    o.transform.rotation.fromArray(t.rotation ?? [0, 0, 0]);
    o.transform.scale.fromArray(t.scale ?? [1, 1, 1]);
    o.visible = Boolean(d.visible ?? true);
    o.tags = (d.tags as string[]) ?? [];
    o.metadata = (d.metadata as Record<string, unknown>) ?? {};

    for (const cd of ((d.components as Record<string, unknown>[]) ?? [])) {
      const type = String(cd.type);
      if (type === 'mesh') o.addComponent(MeshComponent.deserialize(cd));
      else if (type === 'light') o.addComponent(LightComponent.deserialize(cd));
      else if (type === 'camera') o.addComponent(CameraComponent.deserialize(cd));
      else if (type === 'collider') o.addComponent(ColliderComponent.deserialize(cd));
      else if (type === 'rigidbody') o.addComponent(RigidBodyComponent.deserialize(cd));
      else if (type === 'script') o.addComponent(ScriptComponent.deserialize(cd));
      else if (type === 'animation') o.addComponent(AnimationComponent.deserialize(cd));
    }

    for (const chd of ((d.children as Record<string, unknown>[]) ?? [])) {
      o.addChild(GOniObject.deserialize(chd));
    }
    return o;
  }
}
