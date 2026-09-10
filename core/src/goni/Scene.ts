/**
 * G.oni Llumni — Cena: grafo de objetos, matrizes de mundo,
 * busca por caminho (suporta `$NomeDoNo` do G.oni Script).
 */

import { GOniObject } from './Objetos';
import { Mat4 } from '../math';
import { RenderSettings } from '../render/Renderer';

export class Scene {
  name = 'Cena';
  /** objetos-raiz (sem pai) */
  roots: GOniObject[] = [];
  environment = new RenderSettings();
  /** gravidade do mundo (m/s²) */
  gravity = -9.81;

  add(obj: GOniObject, parent?: GOniObject | null): GOniObject {
    if (parent) parent.addChild(obj);
    else {
      if (obj.parent) obj.parent.removeChild(obj);
      this.roots.push(obj);
    }
    obj.markDirty();
    return obj;
  }

  remove(obj: GOniObject): boolean {
    if (obj.parent) return obj.parent.removeChild(obj);
    const i = this.roots.indexOf(obj);
    if (i >= 0) {
      this.roots.splice(i, 1);
      return true;
    }
    return false;
  }

  /** Percorre todos os objetos (profundidade). */
  all(): GOniObject[] {
    const out: GOniObject[] = [];
    const walk = (o: GOniObject) => {
      out.push(o);
      for (const c of o.children) walk(c);
    };
    for (const r of this.roots) walk(r);
    return out;
  }

  findById(id: string): GOniObject | null {
    for (const o of this.all()) if (o.id === id) return o;
    return null;
  }

  findByName(name: string): GOniObject | null {
    for (const o of this.all()) if (o.name === name) return o;
    return null;
  }

  /** Resolve caminho hierárquico: "Pai/Filho" ou "$Pai/Filho" (estilo Godot). */
  resolvePath(path: string): GOniObject | null {
    let p = path.trim();
    if (p.startsWith('$')) p = p.slice(1);
    if (p.startsWith('/')) {
      // caminho absoluto a partir da raiz
      const parts = p.slice(1).split('/').filter(Boolean);
      if (!parts.length) return null;
      const root = this.roots.find((r) => r.name === parts[0]);
      if (!root) return null;
      let cur: GOniObject | null = root;
      for (let i = 1; i < parts.length && cur; i++) {
        cur = cur.children.find((c) => c.name === parts[i]) ?? null;
      }
      return cur;
    }
    const parts = p.split('/').filter(Boolean);
    if (!parts.length) return null;
    for (const root of this.roots) {
      if (root.name === parts[0]) {
        let cur: GOniObject | null = root;
        for (let i = 1; i < parts.length && cur; i++) {
          cur = cur.children.find((c) => c.name === parts[i]) ?? null;
        }
        if (cur) return cur;
      }
      const found = root.findDescendant(parts[0]);
      if (found) {
        let cur: GOniObject | null = found;
        for (let i = 1; i < parts.length && cur; i++) {
          cur = cur.children.find((c) => c.name === parts[i]) ?? null;
        }
        if (cur) return cur;
      }
    }
    return null;
  }

  /** Computa matrizes de mundo (pais antes dos filhos). */
  updateWorldMatrices(): void {
    const walk = (o: GOniObject, parentMatrix: Mat4 | null) => {
      if (o.worldDirty || parentMatrix) {
        const local = o.transform.toMatrix();
        if (parentMatrix) o.worldMatrix.copy(parentMatrix).multiply(local);
        else o.worldMatrix.copy(local);
        o.worldDirty = false;
        for (const c of o.children) walk(c, o.worldMatrix);
      } else {
        for (const c of o.children) walk(c, null);
      }
    };
    for (const r of this.roots) walk(r, null);
  }

  /** Nome único para novo objeto ("Cubo", "Cubo2", ...). */
  uniqueName(base: string): string {
    const names = new Set(this.all().map((o) => o.name));
    if (!names.has(base)) return base;
    let i = 2;
    while (names.has(`${base}${i}`)) i++;
    return `${base}${i}`;
  }

  serialize(): Record<string, unknown> {
    return {
      name: this.name,
      gravity: this.gravity,
      environment: this.environment.serialize(),
      roots: this.roots.map((r) => r.serialize()),
    };
  }

  static deserialize(d: Record<string, unknown>): Scene {
    const s = new Scene();
    s.name = String(d.name ?? 'Cena');
    s.gravity = Number(d.gravity ?? -9.81);
    if (d.environment) s.environment = RenderSettings.deserialize(d.environment as Record<string, unknown>);
    s.roots = ((d.roots as Record<string, unknown>[]) ?? []).map(GOniObject.deserialize);
    return s;
  }

  clear(): void {
    this.roots = [];
  }
}
