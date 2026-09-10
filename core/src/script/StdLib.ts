/**
 * G.oni Llumni — Biblioteca padrão do G.oni Script
 * Funções globais + bridge de objetos de cena.
 */

import { Vec3, MathUtils } from '../math';
import { Environment, Interpreter, NativeFunc, FuncValue, ObjectBridge, toStr, GOniRuntimeError } from './Interpreter';
import { GOniObject } from '../goni/Objetos';
import { CharacterController } from '../physics/CharacterController';

/** Interface implementada pela Engine (evita dependência circular). */
export interface EngineAPI {
  scene: { resolvePath(p: string): GOniObject | null; all(): GOniObject[]; uniqueName(b: string): string };
  print(msg: string): void;
  waitTime(t: number): Promise<void>;
  spawnObject(prefabName: string, pos?: Vec3): GOniObject | null;
  destroyObject(obj: GOniObject, pool: boolean): void;
  input: {
    axis(name: string): number;
    isPressed(name: string): boolean;
    touchX(): number;
    touchY(): number;
    touchDX(): number;
    touchDY(): number;
  };
  time: { elapsed: number; delta: number };
  raycast(origin: Vec3, dir: Vec3, maxDist: number): { obj: GOniObject; point: Vec3; normal: Vec3; distance: number } | null;
  playAnimation(obj: GOniObject, name: string): void;
  getController(obj: GOniObject): CharacterController | null;
  getBodyVelocity(obj: GOniObject): Vec3 | null;
  applyImpulse(obj: GOniObject, v: Vec3): void;
  applyForce(obj: GOniObject, v: Vec3): void;
  registerSignal(obj: GOniObject, name: string): void;
}

/** Estado de entrada do jogo (joystick virtual + teclado + toque). */
export class InputState {
  private axesMap = new Map<string, number>();
  private pressedSet = new Set<string>();
  private touch = { x: 0, y: 0, dx: 0, dy: 0 };

  setAxis(name: string, v: number): void { this.axesMap.set(name, v); }
  axis(name: string): number { return this.axesMap.get(name) ?? 0; }
  setPressed(name: string, v: boolean): void {
    if (v) this.pressedSet.add(name);
    else this.pressedSet.delete(name);
  }
  isPressed(name: string): boolean { return this.pressedSet.has(name); }
  setTouch(x: number, y: number): void { this.touch.x = x; this.touch.y = y; }
  addTouchDelta(dx: number, dy: number): void { this.touch.dx += dx; this.touch.dy += dy; }
  consumeTouchDelta(): [number, number] {
    const d: [number, number] = [this.touch.dx, this.touch.dy];
    this.touch.dx = 0;
    this.touch.dy = 0;
    return d;
  }
  reset(): void {
    this.axesMap.clear();
    this.pressedSet.clear();
    this.touch.dx = 0;
    this.touch.dy = 0;
  }
  touchX(): number { return this.touch.x; }
  touchY(): number { return this.touch.y; }
  touchDX(): number { return this.consumeTouchDelta()[0]; }
  touchDY(): number { return this.consumeTouchDelta()[1]; }
}

export function createGlobals(api: EngineAPI): Environment {
  const g = new Environment(null);
  const nf = (name: string, fn: (args: unknown[], env: Environment) => unknown) =>
    g.define(name, new NativeFunc(name, fn));

  // ---- console / conversão ----
  nf('print', (args) => { api.print(args.map(toStr).join(' ')); return null; });
  nf('str', (args) => toStr(args[0]));
  nf('num', (args) => {
    const n = parseFloat(toStr(args[0]));
    return isNaN(n) ? 0 : n;
  });
  nf('int', (args) => Math.trunc(Number(args[0]) || 0));
  nf('bool', (args) => !!args[0]);
  nf('len', (args) => {
    const v = args[0];
    if (typeof v === 'string') return v.length;
    if (Array.isArray(v)) return v.length;
    if (v instanceof Map) return v.size;
    if (v instanceof Vec3) return 3;
    return 0;
  });
  nf('range', (args) => {
    const [a, b, c] = args.map(Number);
    if (args.length === 1) {
      const out: number[] = [];
      for (let i = 0; i < a; i++) out.push(i);
      return out;
    }
    const start = args.length >= 2 ? a : 0;
    const end = args.length >= 2 ? b : a;
    const step = args.length >= 3 ? c : 1;
    const out: number[] = [];
    if (step === 0) return out;
    if (step > 0) for (let i = start; i < end; i += step) out.push(i);
    else for (let i = start; i > end; i += step) out.push(i);
    return out;
  });

  // ---- matemática ----
  const math1 = (name: string, fn: (x: number) => number) => nf(name, (args) => fn(Number(args[0]) || 0));
  math1('abs', Math.abs);
  math1('floor', Math.floor);
  math1('ceil', Math.ceil);
  math1('round', Math.round);
  math1('sqrt', Math.sqrt);
  math1('sin', Math.sin);
  math1('cos', Math.cos);
  math1('tan', Math.tan);
  math1('sign', Math.sign);
  nf('atan2', (args) => Math.atan2(Number(args[0]), Number(args[1])));
  nf('pow', (args) => Math.pow(Number(args[0]), Number(args[1])));
  nf('min', (args) => Math.min(...args.map(Number)));
  nf('max', (args) => Math.max(...args.map(Number)));
  nf('clamp', (args) => MathUtils.clamp(Number(args[0]), Number(args[1]), Number(args[2])));
  nf('lerp', (args) => MathUtils.lerp(Number(args[0]), Number(args[1]), Number(args[2])));
  nf('rand', () => Math.random());
  nf('rand_range', (args) => MathUtils.randRange(Number(args[0]), Number(args[1])));
  nf('vec3', (args) => new Vec3(Number(args[0]) || 0, Number(args[1]) || 0, Number(args[2]) || 0));

  // ---- tempo / corrotinas ----
  nf('time', () => api.time.elapsed);
  nf('delta', () => api.time.delta);
  nf('wait', (args) => api.waitTime(Math.max(0, Number(args[0]) || 0)));
  nf('wait_signal', (args, env) => {
    // await wait_signal("nome", objeto?) — padrão: self
    const name = toStr(args[0]);
    const obj = (args[1] as GOniObject | undefined) ?? getSelf(env);
    if (obj instanceof GOniObject) return obj.signals.waitSignal(name);
    throw new GOniRuntimeError('wait_signal exige um objeto de cena');
  });

  // ---- sinais ----
  nf('signal', (args, env) => {
    const name = toStr(args[0]);
    const self = getSelf(env);
    if (self instanceof GOniObject) return self.signals.waitSignal(name);
    throw new GOniRuntimeError('signal() só pode ser usado em scripts anexados a objetos');
  });
  nf('emit', (args, env) => {
    const self = getSelf(env);
    if (!(self instanceof GOniObject)) throw new GOniRuntimeError('emit() exige self ser objeto');
    const name = toStr(args[0]);
    self.signals.emit(name, args.slice(1));
    return null;
  });
  nf('connect', (args, env) => {
    const self = getSelf(env);
    if (!(self instanceof GOniObject)) throw new GOniRuntimeError('connect() exige self ser objeto');
    const name = toStr(args[0]);
    const callback = args[1];
    const obj = (args[2] as GOniObject | undefined) ?? self;
    if (!(obj instanceof GOniObject)) throw new GOniRuntimeError('connect() exige objeto');
    if (callback instanceof FuncValue) {
      const interpHolder = env.get('__interp__') as Interpreter | undefined;
      obj.signals.connect(name, (...a: unknown[]) => {
        interpHolder?.callFunction(callback, a).catch(() => { });
      });
    } else if (callback instanceof NativeFunc) {
      obj.signals.connect(name, (...a: unknown[]) => {
        try { callback.fn(a, env); } catch { /* protegido */ }
      });
    } else if (typeof callback === 'string') {
      // nome de função do script
      const interp = env.get('__interp__') as Interpreter | undefined;
      if (interp && env.has(callback)) {
        const fnv = env.get(callback);
        if (fnv instanceof FuncValue) {
          obj.signals.connect(name, (...a: unknown[]) => {
            interp.callFunction(fnv, a).catch(() => { });
          });
          return null;
        }
      }
      throw new GOniRuntimeError(`função "${callback}" não encontrada para connect()`);
    }
    return null;
  });
  nf('disconnect', (args, env) => {
    const self = getSelf(env);
    if (!(self instanceof GOniObject)) return null;
    self.signals.disconnect(toStr(args[0]), args[1] as never);
    return null;
  });

  // ---- cena ----
  nf('get_node', (args, env) => {
    const path = toStr(args[0]);
    const self = getSelf(env);
    if (self instanceof GOniObject) {
      const rel = resolveFromObject(self, path);
      if (rel) return rel;
    }
    return api.scene.resolvePath(path);
  });
  nf('find_by_name', (args) => {
    const name = toStr(args[0]);
    return api.scene.all().find((o) => o.name === name) ?? null;
  });
  nf('find_by_tag', (args) => {
    const tag = toStr(args[0]);
    return api.scene.all().filter((o) => o.tags.includes(tag));
  });
  nf('spawn', (args, env) => {
    const prefab = toStr(args[0]);
    const pos = args.length >= 4
      ? new Vec3(Number(args[1]), Number(args[2]), Number(args[3]))
      : undefined;
    const obj = api.spawnObject(prefab, pos);
    if (obj && getSelf(env) instanceof GOniObject) {
      // spawn relativo ao pai? não — spawn é raiz por padrão
    }
    return obj;
  });
  nf('destroy', (args) => {
    const obj = args[0];
    if (!(obj instanceof GOniObject)) throw new GOniRuntimeError('destroy() exige objeto');
    api.destroyObject(obj, args[1] === true);
    return null;
  });
  nf('release', (args) => {
    const obj = args[0];
    if (obj instanceof GOniObject) api.destroyObject(obj, true);
    return null;
  });
  nf('queue_free', (args) => {
    const obj = args[0];
    if (obj instanceof GOniObject) api.destroyObject(obj, false);
    return null;
  });

  // ---- física ----
  nf('raycast', (args) => {
    const o = args[0];
    let origin: Vec3;
    let dir: Vec3;
    let maxDist = 100;
    if (o instanceof Vec3 && args[1] instanceof Vec3) {
      origin = o;
      dir = (args[1] as Vec3).clone().norm();
      if (args.length >= 3) maxDist = Number(args[2]);
    } else {
      origin = new Vec3(Number(args[0]), Number(args[1]), Number(args[2]));
      dir = new Vec3(Number(args[3]), Number(args[4]), Number(args[5])).norm();
      if (args.length >= 7) maxDist = Number(args[6]);
    }
    const hit = api.raycast(origin, dir, maxDist);
    if (!hit) return null;
    return new Map<unknown, unknown>([
      ['object', hit.obj],
      ['point', hit.point],
      ['normal', hit.normal],
      ['distance', hit.distance],
    ]);
  });

  // ---- entrada ----
  nf('input_pressed', (args) => api.input.isPressed(toStr(args[0])));
  nf('input_axis', (args) => api.input.axis(toStr(args[0])));
  nf('touch_x', () => api.input.touchX());
  nf('touch_y', () => api.input.touchY());
  nf('touch_dx', () => api.input.touchDX());
  nf('touch_dy', () => api.input.touchDY());

  // ---- animação ----
  nf('play_animation', (args) => {
    const obj = args[0];
    if (obj instanceof GOniObject) api.playAnimation(obj, toStr(args[1]));
    return null;
  });

  return g;
}

function getSelf(env: Environment): unknown {
  return env.has('self') ? env.get('self') : null;
}

/** Resolve caminho relativo a partir de um objeto (descendentes, ancestrais, global). */
export function resolveFromObject(self: GOniObject, path: string): GOniObject | null {
  let p = path.trim().replace(/^\$/, '');
  if (p.startsWith('../')) {
    // sobe níveis
    let cur: GOniObject | null = self;
    while (p.startsWith('../') && cur) {
      cur = cur.parent;
      p = p.slice(3);
    }
    if (!cur) return null;
    if (!p) return cur;
    return cur.findDescendant(p);
  }
  if (p.startsWith('/')) return null;
  const first = p.split('/')[0];
  // 1. descendentes diretos/sub
  const d = self.children.find((c) => c.name === first) ?? self.findDescendant(first);
  if (d) {
    if (p === first) return d;
    return self.findDescendant(p);
  }
  // 2. ancestrais
  let anc = self.parent;
  while (anc) {
    const f = anc.children.find((c) => c.name === first);
    if (f) return anc.findDescendant(p);
    anc = anc.parent;
  }
  return null;
}

/**
 * Bridge de objetos: expõe G.oni Objetos ao G.oni Script.
 */
export function createObjectBridge(api: EngineAPI, interp: Interpreter): ObjectBridge {
  return {
    isObject(obj: unknown): boolean {
      return obj instanceof GOniObject || obj instanceof CharacterController;
    },

    typeName(obj: unknown): string {
      if (obj instanceof GOniObject) return 'G.oni Objeto';
      if (obj instanceof CharacterController) return 'CharacterController';
      return typeof obj;
    },

    get(obj, name) {
      if (obj instanceof GOniObject) {
        switch (name) {
          case 'position': return obj.transform.position;
          case 'rotation': return obj.transform.rotation;
          case 'scale': return obj.transform.scale;
          case 'world_position': return obj.getWorldPosition();
          case 'name': return obj.name;
          case 'visible': return obj.visible;
          case 'tags': return obj.tags;
          case 'velocity': return api.getBodyVelocity(obj);
          case 'velocity_y': return api.getBodyVelocity(obj)?.y ?? 0;
          case 'grounded': return api.getController(obj)?.grounded ?? false;
          case 'id': return obj.id;
          default: return undefined;
        }
      }
      if (obj instanceof CharacterController) {
        switch (name) {
          case 'grounded': return obj.grounded;
          case 'speed': return obj.speed;
          case 'jump_speed': return obj.jumpSpeed;
          case 'velocity': return obj.velocity;
          default: return undefined;
        }
      }
      return undefined;
    },

    set(obj, name, value) {
      if (obj instanceof GOniObject) {
        switch (name) {
          case 'position':
            if (value instanceof Vec3) { obj.transform.position.copy(value); obj.markDirty(); return true; }
            return false;
          case 'rotation':
            if (value instanceof Vec3) { obj.transform.rotation.copy(value); obj.markDirty(); return true; }
            return false;
          case 'scale':
            if (value instanceof Vec3) { obj.transform.scale.copy(value); obj.markDirty(); return true; }
            return false;
          case 'visible':
            obj.visible = !!value; return true;
          case 'name':
            obj.name = toStr(value); return true;
          default: return false;
        }
      }
      if (obj instanceof CharacterController) {
        switch (name) {
          case 'speed': obj.speed = Number(value); return true;
          case 'jump_speed': obj.jumpSpeed = Number(value); return true;
          default: return false;
        }
      }
      return false;
    },

    call(obj, method, args, line) {
      if (obj instanceof GOniObject) {
        switch (method) {
          case 'translate': {
            const v = args[0];
            if (v instanceof Vec3) obj.transform.position.add(v);
            else obj.transform.position.add(new Vec3(Number(args[0]), Number(args[1]), Number(args[2])));
            obj.markDirty();
            return null;
          }
          case 'set_position': {
            const v = args[0];
            if (v instanceof Vec3) obj.transform.position.copy(v);
            else obj.transform.position.set(Number(args[0]), Number(args[1]), Number(args[2]));
            obj.markDirty();
            return null;
          }
          case 'set_rotation_deg': {
            obj.transform.rotation.set(
              MathUtils.degToRad(Number(args[0])),
              MathUtils.degToRad(Number(args[1])),
              MathUtils.degToRad(Number(args[2]))
            );
            obj.markDirty();
            return null;
          }
          case 'rotate_x': obj.transform.rotation.x += MathUtils.degToRad(Number(args[0])); obj.markDirty(); return null;
          case 'rotate_y': obj.transform.rotation.y += MathUtils.degToRad(Number(args[0])); obj.markDirty(); return null;
          case 'rotate_z': obj.transform.rotation.z += MathUtils.degToRad(Number(args[0])); obj.markDirty(); return null;
          case 'set_scale': {
            const v = args[0];
            if (v instanceof Vec3) obj.transform.scale.copy(v);
            else obj.transform.scale.set(Number(args[0]), Number(args[1]), Number(args[2]));
            obj.markDirty();
            return null;
          }
          case 'look_at': {
            const target = args[0] instanceof Vec3
              ? (args[0] as Vec3)
              : args[0] instanceof GOniObject
                ? (args[0] as GOniObject).getWorldPosition()
                : new Vec3(Number(args[0]), Number(args[1]), Number(args[2]));
            const wp = obj.getWorldPosition();
            const dir = target.clone().sub(wp);
            if (dir.lenSq() < 1e-8) return null;
            // yaw (Y) e pitch (X)
            const yaw = Math.atan2(-dir.x, -dir.z);
            const horizontal = Math.hypot(dir.x, dir.z);
            const pitch = Math.atan2(dir.y, horizontal);
            obj.transform.rotation.set(pitch, yaw, 0);
            obj.markDirty();
            return null;
          }
          case 'distance_to': {
            const other = args[0] instanceof GOniObject
              ? (args[0] as GOniObject).getWorldPosition()
              : args[0] instanceof Vec3 ? (args[0] as Vec3) : new Vec3(Number(args[0]), Number(args[1]), Number(args[2]));
            return obj.getWorldPosition().distanceTo(other);
          }
          case 'get_world_position': return obj.getWorldPosition();
          case 'apply_impulse': {
            const v = args[0] instanceof Vec3 ? (args[0] as Vec3) : new Vec3(Number(args[0]), Number(args[1]), Number(args[2]));
            api.applyImpulse(obj, v);
            return null;
          }
          case 'apply_force': {
            const v = args[0] instanceof Vec3 ? (args[0] as Vec3) : new Vec3(Number(args[0]), Number(args[1]), Number(args[2]));
            api.applyForce(obj, v);
            return null;
          }
          case 'get_controller': return api.getController(obj);
          case 'play_animation': api.playAnimation(obj, toStr(args[0])); return null;
          case 'destroy': api.destroyObject(obj, args[0] === true); return null;
          case 'emit_signal': obj.signals.emit(toStr(args[0]), args.slice(1)); return null;
          case 'connect_signal': {
            const name = toStr(args[0]);
            const cb = args[1];
            if (cb instanceof FuncValue) {
              obj.signals.connect(name, (...a: unknown[]) => {
                interp.callFunction(cb, a).catch(() => { });
              });
            }
            return null;
          }
          case '__get_node': return resolveFromObject(obj, toStr(args[0]));
          case '__declare_signal': api.registerSignal(obj, toStr(args[0])); return null;
          case '__call__': throw new GOniRuntimeError('objetos não são chamáveis', line);
          default:
            return null;
        }
      }
      if (obj instanceof CharacterController) {
        switch (method) {
          case 'move': {
            // cc.move(vx, vz, delta?, pular?)
            const dt = args.length >= 3 ? Number(args[2]) : api.time.delta;
            obj.move(Number(args[0] || 0), Number(args[1] ?? 0) || 0, dt || 1 / 60, args[3] === true);
            return null;
          }
          case 'jump': {
            const dt = api.time.delta;
            obj.move(0, 0, dt, true);
            return null;
          }
          case 'teleport': obj.teleport(Number(args[0]), Number(args[1]), Number(args[2])); return null;
          case 'set_gravity': return null;
          default: return null;
        }
      }
      return null;
    },
  };
}
