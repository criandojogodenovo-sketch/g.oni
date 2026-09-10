/**
 * G.oni Construt — Sistema de construção de cenas e prefabs.
 *
 * Permite:
 *  - registro de prefabs a partir de objetos (clonagem profunda)
 *  - instanciação em tempo de execução (spawn)
 *  - snap a grid configurável
 *  - agrupamento (transforma seleção em filho de um grupo)
 */

import { GOniObject } from './Objetos';
import { Scene } from './Scene';
import { Vec3 } from '../math';

export interface Prefab {
  name: string;
  template: Record<string, unknown>; // GOniObject serializado
  createdAt: number;
}

export class Construt {
  prefabs = new Map<string, Prefab>();
  /** tamanho da célula de snap (0 = desligado) */
  gridSnap = 0.5;

  /** Cria um prefab a partir da subárvore de um objeto. */
  registerFromObject(obj: GOniObject, name?: string): Prefab {
    const pname = name ?? obj.name;
    const tpl = obj.serialize();
    const p: Prefab = { name: pname, template: tpl, createdAt: Date.now() };
    this.prefabs.set(pname, p);
    return p;
  }

  removePrefab(name: string): void {
    this.prefabs.delete(name);
  }

  /** Instancia o prefab com ids novos. position local opcional. */
  instantiate(prefabName: string, scene: Scene, parent?: GOniObject | null, position?: Vec3): GOniObject {
    const p = this.prefabs.get(prefabName);
    if (!p) throw new Error(`G.oni Construt: prefab "${prefabName}" não encontrado`);
    const obj = GOniObject.deserialize(this.cloneTemplate(p.template));
    if (position) obj.transform.position.copy(position);
    scene.add(obj, parent ?? null);
    return obj;
  }

  /** Re-id: gera cópia profunda do template com ids únicos. */
  private cloneTemplate(tpl: Record<string, unknown>): Record<string, unknown> {
    const clone: Record<string, unknown> = { ...tpl, id: undefined };
    const children = tpl.children as Record<string, unknown>[] | undefined;
    if (children) clone.children = children.map((c) => this.cloneTemplate(c));
    return clone;
  }

  /** Aplica snap a grid na posição (se ativo). */
  snap(p: Vec3): Vec3 {
    if (this.gridSnap <= 0) return p;
    p.x = Math.round(p.x / this.gridSnap) * this.gridSnap;
    p.y = Math.round(p.y / this.gridSnap) * this.gridSnap;
    p.z = Math.round(p.z / this.gridSnap) * this.gridSnap;
    return p;
  }

  /** Agrupa objetos sob um novo objeto vazio (preserva mundo aproximado). */
  group(scene: Scene, objects: GOniObject[], name = 'Grupo'): GOniObject {
    const group = new GOniObject();
    group.name = scene.uniqueName(name);
    // centro médio das posições
    const center = new Vec3();
    objects.forEach((o) => center.add(o.transform.position));
    if (objects.length) center.mul(1 / objects.length);
    group.transform.position.copy(center);
    scene.add(group, null);
    for (const o of objects) {
      // ajusta posição local para manter posição de mundo
      const world = o.transform.position.clone();
      if (o.parent) {
        scene.add(o, group);
      } else {
        scene.add(o, group);
      }
      o.transform.position.copy(world.clone().sub(center));
    }
    return group;
  }

  serialize(): Record<string, unknown> {
    return {
      gridSnap: this.gridSnap,
      prefabs: [...this.prefabs.values()],
    };
  }

  static deserialize(d: Record<string, unknown>): Construt {
    const c = new Construt();
    c.gridSnap = Number(d.gridSnap ?? 0.5);
    for (const p of ((d.prefabs as Prefab[]) ?? [])) {
      c.prefabs.set(p.name, p);
    }
    return c;
  }
}
