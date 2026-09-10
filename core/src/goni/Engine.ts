/**
 * G.oni Llumni — Engine (orquestrador central)
 *
 * Integra: Renderer (WebGL2), PhysicsWorld, VM G.oni Script,
 * G.oni Signal/Construt/Eliminação/Links/Funciona, animação, entrada
 * e o ciclo de vida play/stop.
 */

import { Renderer, RenderItem, LineBatch } from '../render/Renderer';
import { Camera } from '../render/Camera';
import { LightBatch } from '../render/Light';
import { PRIMITIVES } from '../render/primitives';
import { MeshData } from '../render/Mesh';
import { Texture } from '../render/Texture';
import { Mat4, Vec3, Color, Quat, MathUtils } from '../math';
import { Scene } from './Scene';
import { GOniObject, MeshComponent, LightComponent, CameraComponent, ScriptComponent, AnimationComponent, ColliderComponent, RigidBodyComponent } from './Objetos';
import { Construt } from './Construt';
import { Eliminacao } from './Eliminacao';
import { LinkGraph } from './Links';
import { Funciona } from './Funciona';
import { GOniProject, GOniScriptDef, GOniFormat } from './Format';
import { PhysicsWorld, PhysBody } from '../physics/PhysicsWorld';
import { JointSystem } from '../physics/Joints';
import { CharacterController } from '../physics/CharacterController';
import { Interpreter, Environment, GOniRuntimeError, FuncValue } from '../script/Interpreter';
import { parse } from '../script/Parser';
import { createGlobals, createObjectBridge, InputState, EngineAPI, resolveFromObject } from '../script/StdLib';
import { AnimationClip } from './Animation';

interface ScriptInstance {
  comp: ScriptComponent;
  obj: GOniObject;
  env: Environment;
  pending: boolean;
}

interface WaitTimer {
  resolveAt: number;
  resolve: () => void;
}

export interface EngineCallbacks {
  onLog?: (msg: string) => void;
  onScriptError?: (scriptName: string, err: Error) => void;
  onPlayStateChange?: (playing: boolean) => void;
}

export class Engine implements EngineAPI {
  renderer = new Renderer();
  scene = new Scene();
  physics = new PhysicsWorld();
  joints = new JointSystem();
  construt = new Construt();
  eliminacao: Eliminacao;
  links = new LinkGraph();
  funciona = new Funciona();
  input = new InputState();

  interpreter!: Interpreter;
  scriptSources = new Map<string, string>();
  private astCache = new Map<string, ReturnType<typeof parse>>();
  /** instâncias de script ativas (públicas para painéis/overlay lerem stats) */
  scriptInstances: ScriptInstance[] = [];

  time = { elapsed: 0, delta: 0 };
  playing = false;

  /** câmera ativa do jogo (em play) */
  activeCamera: Camera | null = null;

  console: string[] = [];
  private waitTimers: WaitTimer[] = [];

  private playSnapshot: string | null = null;

  // caches de render
  private renderItems: RenderItem[] = [];
  private lightBatch = new LightBatch();
  private controllers = new Map<string, CharacterController>();

  constructor(public callbacks: EngineCallbacks = {}) {
    this.eliminacao = new Eliminacao(this.construt);
    this.physics.onCollisionEnter = (a, b, c) => this.fireCollision(a, b, c, true);
    this.physics.onCollisionExit = (a, b) => this.fireCollision(a, b, null, false);
  }

  // ---------------- inicialização ----------------

  init(canvas: HTMLCanvasElement): void {
    this.renderer.init(canvas);
    this.interpreter = new Interpreter(createGlobals(this));
    this.interpreter.objectBridge = createObjectBridge(this, this.interpreter);
    this.registerNativeFuncs();
  }

  private registerNativeFuncs(): void {
    // G.oni Funciona: funções nativas da engine expostas a scripts e plugins
    this.funciona.registerNative('print', (args) => { this.log(args.join(' ')); return null; }, { desc: 'Imprime no console' });
    this.funciona.registerNative('time', () => this.time.elapsed, { desc: 'Tempo de jogo (s)' });
    this.funciona.registerNative('spawn', (args) => this.spawnObject(String(args[0]), args[1] as Vec3), { desc: 'Instancia prefab' });
    this.funciona.registerNative('destroy', (args) => { const o = args[0]; if (o instanceof GOniObject) this.destroyObject(o, false); return null; });
  }

  log(msg: string): void {
    this.console.push(msg);
    if (this.console.length > 500) this.console.shift();
    this.callbacks.onLog?.(msg);
  }

  /** Invalida o cache de AST (após edição de script). */
  astCacheTouch(): void {
    this.astCache.clear();
  }

  /** Recria as instâncias de script (hot-reload em play mode). */
  scriptInstancesTouch(): void {
    if (!this.playing) return;
    this.scriptInstances = [];
    for (const obj of this.scene.all()) {
      for (const comp of obj.getComponents<ScriptComponent>('script')) {
        this.createScriptInstance(comp, obj);
      }
    }
    for (const inst of [...this.scriptInstances]) {
      void this.callScriptMethod(inst, 'on_ready');
    }
  }

  // ---------------- projeto ----------------

  loadProject(project: GOniProject): void {
    this.scriptSources.clear();
    this.astCache.clear();
    for (const s of project.scripts) {
      this.scriptSources.set(s.name, s.source);
    }
    this.construt = Construt.deserialize(project.settings.construt ?? {});
    this.eliminacao = new Eliminacao(this.construt);
    this.links = LinkGraph.deserialize(project.settings.links ?? {});

    this.scene = project.scenes.length
      ? Scene.deserialize(project.scenes[0] as Record<string, unknown>)
      : new Scene();
    this.scene.gravity = (project.scenes[0] as { gravity?: number })?.gravity ?? -9.81;
    this.physics.gravity = new Vec3(0, this.scene.gravity, 0);

    // registra meshes customizadas
    for (const [id, md] of Object.entries(project.assets.meshes ?? {})) {
      this.renderer.meshRegistry.register(id, MeshData.deserialize(md));
    }

    // registra texturas (async, mas ids já ficam ligados)
    for (const [id, dataUrl] of Object.entries(project.assets.textures ?? {})) {
      void this.registerTextureFromDataUrl(id, dataUrl);
    }

    // resolve materiais → texturas
    for (const obj of this.scene.all()) {
      const meshComp = obj.getComponent<MeshComponent>('mesh');
      if (meshComp) this.ensureMesh(meshComp.meshId);
    }

    this.renderer.settings = this.scene.environment;
    this.log(`Projeto "${project.name}" carregado (${this.scene.all().length} objetos).`);
  }

  toProject(): GOniProject {
    // garante meshes customizadas serializadas
    const meshes: Record<string, { p: number[]; n: number[]; u: number[]; i: number[] }> = {};
    for (const id of this.renderer.meshRegistry.listIds()) {
      if (id.startsWith('asset:')) {
        const entry = this.renderer.meshRegistry.get(id);
        if (entry) meshes[id] = entry.data.serialize();
      }
    }
    const scripts: GOniScriptDef[] = [...this.scriptSources.entries()].map(([name, source]) => ({ name, source }));
    return {
      version: GOniFormat.newProject('x').version,
      engine: 'G.oni Llumni',
      name: this.projectName,
      createdAt: this.projectCreatedAt,
      updatedAt: Date.now(),
      scenes: [this.scene.serialize()],
      scripts,
      assets: { meshes, textures: this.texturesToPersist() },
      settings: {
        render: this.scene.environment.serialize(),
        physics: { gravity: this.scene.gravity },
        construt: this.construt.serialize(),
        links: this.links.serialize(),
      },
    };
  }

  projectName = 'Projeto';
  private projectCreatedAt = Date.now();
  private textureAssets = new Map<string, string>(); // id → dataURL

  private texturesToPersist(): Record<string, string> {
    return Object.fromEntries(this.textureAssets);
  }

  async registerTextureFromDataUrl(id: string, dataUrl: string): Promise<void> {
    try {
      const blob = await (await fetch(dataUrl)).blob();
      const bmp = await createImageBitmap(blob);
      const tex = new Texture(this.renderer.gl);
      tex.id = id;
      tex.upload(bmp);
      this.applyTextureToMaterials(id, tex);
      this.textureAssets.set(id, dataUrl);
    } catch {
      this.log(`Aviso: falha ao carregar textura "${id}"`);
    }
  }

  private applyTextureToMaterials(id: string, tex: Texture): void {
    for (const obj of this.scene.all()) {
      const meshComp = obj.getComponent<MeshComponent>('mesh');
      if (meshComp) {
        const matAny = meshComp.material as unknown as { pendingAlbedoId?: string; pendingNormalId?: string };
        if (matAny.pendingAlbedoId === id) {
          meshComp.material.albedoTexture = tex;
          delete matAny.pendingAlbedoId;
        }
        if (matAny.pendingNormalId === id) {
          meshComp.material.normalTexture = tex;
          delete matAny.pendingNormalId;
        }
      }
    }
  }

  /** Registra textura do editor (canvas de pintura ou imagem importada). */
  registerTexture(id: string, source: HTMLCanvasElement | ImageBitmap): Texture {
    const tex = new Texture(this.renderer.gl);
    tex.id = id;
    tex.upload(source as HTMLCanvasElement);
    this.textureAssets.set(id, texToDataUrl(source));
    return tex;
  }

  // ---------------- meshes ----------------

  ensureMesh(meshId: string): void {
    if (this.renderer.meshRegistry.get(meshId)) return;
    if (meshId.startsWith('prim:')) {
      const name = meshId.slice(5);
      const gen = PRIMITIVES[name];
      if (gen) this.renderer.meshRegistry.register(meshId, gen());
      return;
    }
    // asset: já deve ter sido registrado em loadProject
  }

  registerCustomMesh(data: MeshData, meshId?: string): string {
    const id = meshId ?? `asset:mesh_${Date.now().toString(36)}`;
    this.renderer.meshRegistry.register(id, data);
    return id;
  }

  updateCustomMesh(id: string, data: MeshData): void {
    this.renderer.meshRegistry.update(id, data);
  }

  // ---------------- ciclo de vida play/stop ----------------

  play(): void {
    if (this.playing) return;
    this.playing = true;
    this.time.elapsed = 0;
    this.waitTimers = [];
    this.console.length = 0;
    this.input.reset();
    this.controllers.clear();

    // snapshot para restaurar
    this.playSnapshot = JSON.stringify(this.scene.serialize());

    // física
    this.scene.updateWorldMatrices();
    this.physics.syncFromScene(this.scene);
    this.joints.joints.length = 0;

    // controllers para objetos com rigidbody do tipo character
    for (const b of this.physics.bodies) {
      const comp = b.obj.getComponent<ColliderComponent>('collider');
      if (b.collider.shape === 'capsule' && comp?.enabled) {
        this.controllers.set(b.obj.id, new CharacterController(b, this.physics));
      }
    }

    // instâncias de script
    this.scriptInstances = [];
    for (const obj of this.scene.all()) {
      for (const comp of obj.getComponents<ScriptComponent>('script')) {
        this.createScriptInstance(comp, obj);
      }
    }

    // auto-play de animações
    for (const obj of this.scene.all()) {
      const anim = obj.getComponent<AnimationComponent>('animation');
      if (anim?.autoplay && anim.clips.length) {
        anim.playing = anim.clips[0].name;
      }
    }

    // on_ready
    for (const inst of [...this.scriptInstances]) {
      void this.callScriptMethod(inst, 'on_ready');
    }

    this.callbacks.onPlayStateChange?.(true);
    this.log('▶ Play iniciado.');
  }

  stop(): void {
    if (!this.playing) return;
    this.playing = false;
    this.eliminacao.flushDestroyQueue(this.scene);
    this.scriptInstances = [];
    this.activeCamera = null;
    this.controllers.clear();

    if (this.playSnapshot) {
      this.scene = Scene.deserialize(JSON.parse(this.playSnapshot));
      this.scene.updateWorldMatrices();
      this.playSnapshot = null;
    }
    this.callbacks.onPlayStateChange?.(false);
    this.log('■ Play parado — cena restaurada.');
  }

  private createScriptInstance(comp: ScriptComponent, obj: GOniObject): void {
    const source = this.scriptSources.get(comp.scriptName);
    if (!source) {
      this.log(`Aviso: script "${comp.scriptName}" não encontrado no projeto.`);
      return;
    }
    try {
      let ast = this.astCache.get(comp.scriptName);
      if (!ast) {
        ast = parse(source);
        this.astCache.set(comp.scriptName, ast);
      }
      const env = new Environment(this.interpreter.globals);
      env.define('self', obj);
      env.define('__interp__', this.interpreter);
      const inst: ScriptInstance = { comp, obj, env, pending: false };
      void this.interpreter.run(ast, env).then(() => {
        comp.runtime = inst;
        // auto-conecta métodos de evento aos sinais nativos
        this.autoConnectSignals(inst);
      }).catch((err) => {
        this.reportScriptError(comp.scriptName, err);
      });
      this.scriptInstances.push(inst);
      // registra no Funciona como funções do script
      this.registerScriptFunctions(comp.scriptName, env);
    } catch (err) {
      this.reportScriptError(comp.scriptName, err as Error);
    }
  }

  private autoConnectSignals(inst: ScriptInstance): void {
    const events = ['on_collision_enter', 'on_collision_exit', 'on_input', 'on_destroy'];
    for (const ev of events) {
      if (inst.env.has(ev)) {
        const fn = inst.env.get(ev);
        if (fn instanceof FuncValue) {
          inst.obj.signals.connect(ev, (...args: unknown[]) => {
            void this.interpreter.callFunction(fn, args).catch(() => { });
          });
        }
      }
    }
  }

  private registerScriptFunctions(name: string, env: Environment): void {
    // expõe top-level funcs ao G.oni Funciona como "<script>.<func>"
    for (const [k, v] of env.vars) {
      if (v instanceof FuncValue) {
        const func = v;
        this.funciona.registerScript(`${name}.${k}`, (args) => {
          return this.interpreter.callFunction(func, args);
        }, { module: name });
      }
    }
  }

  private reportScriptError(scriptName: string, err: unknown): void {
    const msg = err instanceof Error ? err.message : String(err);
    this.log(`✖ [${scriptName}] ${msg}`);
    this.callbacks.onScriptError?.(scriptName, err instanceof Error ? err : new Error(String(err)));
  }

  private async callScriptMethod(inst: ScriptInstance, method: string, args: unknown[] = []): Promise<void> {
    if (inst.pending) return;
    inst.pending = true;
    try {
      await this.interpreter.callByName(inst.env, method, args);
    } catch (err) {
      if (!(err instanceof GOniRuntimeError) && !(err instanceof Error)) {
        this.reportScriptError(inst.comp.scriptName, err);
      } else {
        this.reportScriptError(inst.comp.scriptName, err);
      }
    } finally {
      inst.pending = false;
    }
  }

  // ---------------- update ----------------

  update(dt: number): void {
    this.time.delta = dt;
    if (!this.playing) return;
    this.time.elapsed += dt;

    // timers de wait()
    for (let i = this.waitTimers.length - 1; i >= 0; i--) {
      if (this.time.elapsed >= this.waitTimers[i].resolveAt) {
        this.waitTimers[i].resolve();
        this.waitTimers.splice(i, 1);
      }
    }

    // scripts: on_process
    for (const inst of [...this.scriptInstances]) {
      void this.callScriptMethod(inst, 'on_process', [dt]);
    }

    // juntas aplicam forças
    this.joints.applyForces(dt);

    // física
    this.physics.gravity = new Vec3(0, this.scene.gravity, 0);
    this.scene.updateWorldMatrices();
    this.physics.update(dt);

    // controllers (character)
    for (const cc of this.controllers.values()) {
      void cc;
    }

    // animação
    this.updateAnimations(dt);

    // câmera do jogo
    this.updateActiveCamera();

    // destruição diferida
    this.eliminacao.flushDestroyQueue(this.scene);
  }

  private animState = new Map<string, number>();

  private updateAnimations(dt: number): void {
    for (const obj of this.scene.all()) {
      const anim = obj.getComponent<AnimationComponent>('animation');
      if (!anim || !anim.playing) continue;
      const clip = anim.clips.find((c) => c.name === anim.playing);
      if (!clip) continue;
      const key = `${obj.id}:${clip.name}`;
      const t = (this.animState.get(key) ?? 0) + dt;
      this.animState.set(key, t);
      for (const track of clip.tracks) {
        const v = clip.sample(track.property, t, anim.loop);
        if (v === null) continue;
        applyTrackValue(obj, track.property, v);
      }
      if (!anim.loop && t >= clip.duration) {
        anim.playing = '';
        obj.signals.emit('on_animation_finished', [clip.name]);
      }
    }
  }

  private updateActiveCamera(): void {
    if (!this.activeCamera) return;
    for (const obj of this.scene.all()) {
      const camComp = obj.getComponent<CameraComponent>('camera');
      if (camComp && camComp.primary) {
        const wp = obj.getWorldPosition();
        const q = Quat.fromEuler(obj.transform.rotation.x, obj.transform.rotation.y, obj.transform.rotation.z);
        const forward = q.applyToVec3(new Vec3(0, 0, -1));
        this.activeCamera.eye = wp;
        this.activeCamera.target = wp.clone().add(forward.norm());
        this.activeCamera.setFovDegrees(camComp.fovDegrees);
        this.activeCamera.near = camComp.near;
        this.activeCamera.far = camComp.far;
        this.activeCamera.markViewDirty();
        return;
      }
    }
  }

  // ---------------- render ----------------

  renderFrame(camera: Camera, lines?: LineBatch, overlayLines?: LineBatch): void {
    this.scene.updateWorldMatrices();
    this.renderer.settings = this.scene.environment;

    // coleta de itens
    this.renderItems.length = 0;
    for (const obj of this.scene.all()) {
      if (!obj.visible) continue;
      const meshComp = obj.getComponent<MeshComponent>('mesh');
      if (!meshComp || !meshComp.enabled) continue;
      this.ensureMesh(meshComp.meshId);
      const entry = this.renderer.meshRegistry.get(meshComp.meshId);
      if (!entry) continue;

      // atualiza textura de pintura se suja
      const mat = meshComp.material;
      if (mat.paintCanvas && (mat as unknown as { paintDirty?: boolean }).paintDirty) {
        if (!mat.albedoTexture) {
          mat.albedoTexture = new Texture(this.renderer.gl);
          mat.albedoTexture.id = `paint_${mat.id}`;
        }
        mat.albedoTexture.upload(mat.paintCanvas);
        (mat as unknown as { paintDirty?: boolean }).paintDirty = false;
      }

      // LOD
      let meshId = meshComp.meshId;
      if (meshComp.lodDistance > 0 && meshComp.lodMeshId) {
        const d = obj.getWorldPosition().distanceTo(camera.eye);
        if (d > meshComp.lodDistance) meshId = meshComp.lodMeshId;
      }
      const useEntry = meshId === meshComp.meshId ? entry : this.renderer.meshRegistry.get(meshId) ?? entry;

      // esfera envolvente no mundo
      const scale = obj.transform.scale;
      const maxScale = Math.max(Math.abs(scale.x), Math.abs(scale.y), Math.abs(scale.z));
      const center = obj.worldMatrix.transformPoint(new Vec3(
        useEntry.gpu.bounding.cx, useEntry.gpu.bounding.cy, useEntry.gpu.bounding.cz
      ));
      this.renderItems.push({
        model: obj.worldMatrix,
        mesh: useEntry.gpu,
        material: mat,
        worldCenter: center,
        worldRadius: useEntry.gpu.bounding.radius * maxScale + 0.001,
        transparent: mat.opacity < 1 && mat.alphaCutoff <= 0,
        ownerId: obj.id,
      });
    }

    // luzes
    this.lightBatch.clear();
    for (const obj of this.scene.all()) {
      if (!obj.visible) continue;
      const lightComp = obj.getComponent<LightComponent>('light');
      if (!lightComp || !lightComp.enabled) continue;
      const l = lightComp.light;
      l.worldPos = obj.getWorldPosition();
      if (l.type === 'spot' || l.type === 'directional') {
        // direção: -Z do objeto aplicado via quaternion
        const q = Quat.fromEuler(obj.transform.rotation.x, obj.transform.rotation.y, obj.transform.rotation.z);
        l.direction = q.applyToVec3(new Vec3(0, 0, -1)).norm();
      }
      this.lightBatch.add(l);
    }

    this.renderer.render(camera, this.renderItems, this.lightBatch, lines, overlayLines);
  }

  // ---------------- EngineAPI (script bridge) ----------------

  print(msg: string): void { this.log(msg); }

  waitTime(t: number): Promise<void> {
    return new Promise<void>((resolve) => {
      this.waitTimers.push({ resolveAt: this.time.elapsed + t, resolve });
    });
  }

  spawnObject(prefabName: string, pos?: Vec3): GOniObject | null {
    try {
      const obj = this.construt.instantiate(prefabName, this.scene, null, pos);
      // registra física + scripts
      this.scene.updateWorldMatrices();
      const rb = obj.getComponent<RigidBodyComponent>('rigidbody');
      const col = obj.getComponent<ColliderComponent>('collider');
      if (rb && col) this.physics.addBody(obj, col.collider, rb.body);
      for (const comp of obj.getComponents<ScriptComponent>('script')) {
        this.createScriptInstance(comp, obj);
      }
      const inst = this.scriptInstances.filter((i) => i.obj === obj);
      for (const i of inst) void this.callScriptMethod(i, 'on_ready');
      return obj;
    } catch {
      this.log(`Aviso: falha ao spawnar prefab "${prefabName}"`);
      return null;
    }
  }

  destroyObject(obj: GOniObject, pool: boolean): void {
    this.eliminacao.queueDestroy(obj, 'script', pool);
    this.physics.removeBody(obj);
    this.joints.removeJointsOf(obj.id);
    this.scriptInstances = this.scriptInstances.filter((i) => i.obj !== obj);
    this.controllers.delete(obj.id);
  }

  input_event(kind: string, x: number, y: number): void {
    // emite on_input nos objetos com script
    for (const inst of this.scriptInstances) {
      inst.obj.signals.emit('on_input', [kind, x, y]);
    }
  }

  raycast(origin: Vec3, dir: Vec3, maxDist = 100): { obj: GOniObject; point: Vec3; normal: Vec3; distance: number } | null {
    return this.physics.raycast(origin, dir, maxDist, true);
  }

  playAnimation(obj: GOniObject, name: string): void {
    const anim = obj.getComponent<AnimationComponent>('animation');
    if (!anim) return;
    if (!anim.clips.find((c) => c.name === name)) {
      // se nome vazio, usa o primeiro clipe
      if (name && anim.clips.length) {
        anim.playing = anim.clips[0].name;
      }
    } else {
      anim.playing = name;
    }
    if (anim.playing) this.animState.set(`${obj.id}:${anim.playing}`, 0);
  }

  getController(obj: GOniObject): CharacterController | null {
    return this.controllers.get(obj.id) ?? null;
  }

  getBodyVelocity(obj: GOniObject): Vec3 | null {
    const b = this.physics.getBody(obj);
    return b ? b.velocity : null;
  }

  applyImpulse(obj: GOniObject, v: Vec3): void {
    const b = this.physics.getBody(obj);
    if (b) {
      b.velocity.add(v);
      if (b.rigid.sleeping) b.rigid.sleeping = false;
    }
  }

  applyForce(obj: GOniObject, v: Vec3): void {
    const b = this.physics.getBody(obj);
    if (b) {
      b.force.add(v);
      if (b.rigid.sleeping) b.rigid.sleeping = false;
    }
  }

  registerSignal(obj: GOniObject, name: string): void {
    obj.signals.declare(name);
  }

  // ---------------- eventos de colisão ----------------

  private fireCollision(a: PhysBody, b: PhysBody, contact: { normal: Vec3; point: Vec3 } | null, enter: boolean): void {
    const ev = enter ? 'on_collision_enter' : 'on_collision_exit';
    const argA = b.obj;
    const argB = a.obj;
    const extra = contact
      ? {
        point: contact.point,
        normal: contact.normal,
      }
      : null;
    a.obj.signals.emit(ev, [argA, extra]);
    b.obj.signals.emit(ev, [argB, extra ? { point: extra.point, normal: extra.normal } : null]);
  }

  // ---------------- input ----------------

  scene_api() {
    return this.scene;
  }

  // implementação da interface (scene/input/time já são propriedades)
  get apiScene() { return this.scene; }
}

// helpers
function await0(): unknown { return null; }

function applyTrackValue(obj: GOniObject, property: string, v: number): void {
  const t = obj.transform;
  if (property.startsWith('position.')) {
    const axis = property.split('.')[1];
    if (axis === 'x') t.position.x = v;
    if (axis === 'y') t.position.y = v;
    if (axis === 'z') t.position.z = v;
  } else if (property.startsWith('rotation.')) {
    const axis = property.split('.')[1];
    if (axis === 'x') t.rotation.x = v;
    if (axis === 'y') t.rotation.y = v;
    if (axis === 'z') t.rotation.z = v;
  } else if (property.startsWith('scale.')) {
    const axis = property.split('.')[1];
    if (axis === 'x') t.scale.x = v;
    if (axis === 'y') t.scale.y = v;
    if (axis === 'z') t.scale.z = v;
  }
  obj.markDirty();
}

function texToDataUrl(source: HTMLCanvasElement | ImageBitmap): string {
  if (source instanceof HTMLCanvasElement) {
    return source.toDataURL('image/png');
  }
  const c = document.createElement('canvas');
  c.width = source.width;
  c.height = source.height;
  c.getContext('2d')?.drawImage(source, 0, 0);
  return c.toDataURL('image/png');
}

/** Cena inicial padrão (template "Novo Projeto"). */
export function createDefaultScene(): Scene {
  const scene = new Scene();
  scene.name = 'main';

  // luz direcional
  const sun = new GOniObject();
  sun.name = 'Sol';
  sun.transform.position.set(4, 6, 3);
  sun.transform.rotation.set(MathUtils.degToRad(55), MathUtils.degToRad(35), 0);
  const lightComp = new LightComponent();
  lightComp.light.type = 'directional';
  lightComp.light.color = new Color(1, 0.96, 0.9);
  lightComp.light.intensity = 1.6;
  sun.addComponent(lightComp);
  scene.add(sun);

  // chão
  const ground = new GOniObject();
  ground.name = 'Chão';
  const groundMesh = new MeshComponent();
  groundMesh.meshId = 'prim:plane';
  groundMesh.material.albedo = new Color(0.32, 0.36, 0.42);
  groundMesh.material.roughness = 0.9;
  groundMesh.material.metallic = 0.0;
  ground.transform.scale.set(12, 1, 12);
  ground.addComponent(groundMesh);
  const groundCollider = new ColliderComponent();
  groundCollider.collider.shape = 'box';
  groundCollider.collider.size = new Vec3(6, 0.1, 6);
  groundCollider.collider.offset = new Vec3(0, -0.1, 0);
  ground.addComponent(groundCollider);
  const groundBody = new RigidBodyComponent();
  groundBody.body.type = 'static';
  ground.addComponent(groundBody);
  scene.add(ground);

  // cubo de exemplo
  const cube = new GOniObject();
  cube.name = 'Cubo';
  cube.transform.position.set(0, 1.5, 0);
  const cubeMesh = new MeshComponent();
  cubeMesh.meshId = 'prim:cube';
  cubeMesh.material.albedo = new Color(0.9, 0.62, 0.1);
  cubeMesh.material.metallic = 0.1;
  cubeMesh.material.roughness = 0.35;
  cubeMesh.material.emissive = new Color(0.9, 0.45, 0.05);
  cubeMesh.material.emissiveIntensity = 0.25;
  cube.addComponent(cubeMesh);
  const cubeCollider = new ColliderComponent();
  cubeCollider.collider.shape = 'box';
  cubeCollider.collider.size = new Vec3(0.5, 0.5, 0.5);
  cube.addComponent(cubeCollider);
  const cubeBody = new RigidBodyComponent();
  cubeBody.body.type = 'dynamic';
  cubeBody.body.mass = 1;
  cubeBody.body.restitution = 0.35;
  cube.addComponent(cubeBody);
  scene.add(cube);

  // câmera do jogo
  const camObj = new GOniObject();
  camObj.name = 'Câmera';
  camObj.transform.position.set(0, 2.2, 6);
  const camComp = new CameraComponent();
  camComp.primary = true;
  camComp.fovDegrees = 60;
  camObj.addComponent(camComp);
  scene.add(camObj);

  return scene;
}

function MathUtils_degToRad(d: number): number {
  return MathUtils.degToRad(d);
}
void MathUtils_degToRad;
