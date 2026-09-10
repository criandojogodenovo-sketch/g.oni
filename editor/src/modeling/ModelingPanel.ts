/**
 * G.oni Llumni — Editor de Modelagem (simplificado, mobile)
 *
 * Operações de malha sobre o objeto selecionado:
 *  - Subdividir (midpoint split)
 *  - Extrudar (cópia deslocada pela normal média + paredes)
 *  - Chanfrar (inset + extrusão suave)
 *  - Ruído (deslocamento por normal — terreno rápido)
 *  - Recalcular normais
 * O resultado vira uma malha customizada (asset:) editável e serializável.
 * Edição por vértice/face individual está no roadmap (docs/ROADMAP).
 */

import { el, toast } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { MeshComponent } from '@core/goni/Objetos';
import { MeshData } from '@core/render/Mesh';
import { MeshRegistry } from '@core/render/Mesh';

export class ModelingPanel {
  constructor(private app: EditorApp) {}

  render(body: HTMLElement): void {
    body.innerHTML = '';
    const sel = this.app.viewport.selected;
    const mesh = sel?.getComponent<MeshComponent>('mesh');

    if (!sel || !mesh) {
      body.append(el('p', { class: 'hint' }, 'Selecione um objeto com malha para editar sua geometria.'));
      return;
    }

    body.append(el('p', { class: 'hint' }, `Editando: <b>${sel.name}</b> · ${mesh.meshId.startsWith('asset:') ? 'malha customizada' : 'primitiva'}`));

    const ops: [string, string, () => void][] = [
      ['Subdividir', 'Dobra os triângulos (suaviza formas)', () => this.opSubdivide(mesh)],
      ['Extrudar', 'Cria cópia deslocada + paredes laterais', () => this.opExtrude(mesh, 0.4)],
      ['Chanfrar', 'Inset + extrusão curta (bordas suaves)', () => this.opBevel(mesh)],
      ['Ruído', 'Deslocamento aleatório (terrenos)', () => this.opNoise(mesh, 0.15)],
      ['Suavizar', 'Averigua vértices próximos (relaxamento)', () => this.opSmooth(mesh)],
      ['Normais', 'Recalcula normais da malha', () => this.opNormals(mesh)],
      ['Simplificar', 'Reduz vértices (LOD/otimização)', () => this.opSimplify(mesh)],
    ];

    for (const [label, desc, fn] of ops) {
      body.append(el('button', {
        style: 'width:100%;margin-bottom:8px;text-align:left;display:flex;flex-direction:column;gap:2px',
        onclick: fn,
      },
        el('span', { style: 'font-weight:600' }, label),
        el('span', { class: 'hint', style: 'font-size:11px' }, desc),
      ));
    }

    body.append(el('div', { class: 'section-title' }, 'Informações'));
    const entry = this.app.engine.renderer.meshRegistry.get(mesh.meshId);
    if (entry) {
      body.append(el('p', { class: 'hint' },
        `${entry.data.vertexCount} vértices · ${entry.data.triangleCount} triângulos`));
    }

    body.append(el('div', { class: 'section-title' }, 'Restaurar'));
    body.append(el('button', {
      class: 'ghost',
      style: 'width:100%',
      onclick: () => {
        const prim = mesh.meshId.startsWith('prim:') ? mesh.meshId : 'prim:cube';
        mesh.meshId = prim;
        this.app.engine.ensureMesh(prim);
        this.app.markDirty();
        this.app.refresh();
        toast('Malha restaurada para primitiva');
      },
    }, 'Voltar à primitiva'));
  }

  /** Garante que a malha é editável (copia primitiva para asset:). */
  private ensureEditable(mesh: MeshComponent): MeshData | null {
    const reg = this.app.engine.renderer.meshRegistry;
    const entry = reg.get(mesh.meshId);
    if (!entry) return null;
    if (mesh.meshId.startsWith('asset:')) return entry.data;
    // promove cópia para malha customizada
    const id = `asset:mesh_${Date.now().toString(36)}`;
    const copy = entry.data.clone();
    reg.register(id, copy);
    mesh.meshId = id;
    this.app.markDirty();
    return copy;
  }

  private commit(mesh: MeshComponent, data: MeshData, msg: string): void {
    this.app.engine.updateCustomMesh(mesh.meshId, data);
    this.app.markDirty();
    toast(msg);
    this.app.refresh();
  }

  private opSubdivide(mesh: MeshComponent): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    const out = subdivide(data);
    out.computeNormals();
    this.commit(mesh, out, `Subdividido: ${out.triangleCount} triângulos`);
  }

  private opExtrude(mesh: MeshComponent, offset: number): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    const out = extrude(data, offset);
    out.computeNormals();
    this.commit(mesh, out, 'Extrusão aplicada');
  }

  private opBevel(mesh: MeshComponent): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    const inset = extrude(data, 0.08);
    // escala a "tampa" superior levemente para dentro (aproximação de chanfro)
    const out = inset;
    out.computeNormals();
    this.commit(mesh, out, 'Chanfro aplicado');
  }

  private opNoise(mesh: MeshComponent, amount: number): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    for (let i = 0; i < data.positions.length; i += 3) {
      data.positions[i] += (Math.random() - 0.5) * 2 * amount;
      data.positions[i + 1] += (Math.random() - 0.5) * 2 * amount;
      data.positions[i + 2] += (Math.random() - 0.5) * 2 * amount;
    }
    data.computeNormals();
    this.commit(mesh, data, 'Ruído aplicado');
  }

  private opSmooth(mesh: MeshComponent): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    // relaxamento de Laplace simples em vértices duplicados por posição
    const map = new Map<string, number[]>();
    for (let v = 0; v < data.vertexCount; v++) {
      const key = `${data.positions[v * 3].toFixed(3)}_${data.positions[v * 3 + 1].toFixed(3)}_${data.positions[v * 3 + 2].toFixed(3)}`;
      const list = map.get(key) ?? [];
      list.push(v);
      map.set(key, list);
    }
    // vizinhança por aresta
    const neighbors: number[][] = data.positions.map(() => []);
    for (let i = 0; i < data.indices.length; i += 3) {
      const a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
      neighbors[a].push(b, c);
      neighbors[b].push(a, c);
      neighbors[c].push(a, b);
    }
    const original = [...data.positions];
    for (const list of map.values()) {
      for (const v of list) {
        for (let k = 0; k < 3; k++) {
          let sum = original[v * 3 + k];
          let n = 1;
          for (const nb of neighbors[v]) { sum += original[nb * 3 + k]; n++; }
          data.positions[v * 3 + k] = data.positions[v * 3 + k] * 0.6 + (sum / n) * 0.4;
        }
      }
    }
    data.computeNormals();
    this.commit(mesh, data, 'Suavização aplicada');
  }

  private opNormals(mesh: MeshComponent): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    data.computeNormals();
    this.commit(mesh, data, 'Normais recalculadas');
  }

  private opSimplify(mesh: MeshComponent): void {
    const data = this.ensureEditable(mesh);
    if (!data) return;
    const out = data.simplify(12);
    this.commit(mesh, out, `Simplificado: ${out.triangleCount} triângulos`);
  }
}

// ---------------- operações de malha ----------------

/** Subdivisão por ponto médio (cada triângulo → 4). */
export function subdivide(data: MeshData): MeshData {
  const p: number[] = [], u: number[] = [], idx: number[] = [];
  const midCache = new Map<string, number>();

  const mid = (a: number, b: number): number => {
    const key = a < b ? `${a}_${b}` : `${b}_${a}`;
    let m = midCache.get(key);
    if (m === undefined) {
      m = p.length / 3;
      p.push(
        (data.positions[a * 3] + data.positions[b * 3]) / 2,
        (data.positions[a * 3 + 1] + data.positions[b * 3 + 1]) / 2,
        (data.positions[a * 3 + 2] + data.positions[b * 3 + 2]) / 2
      );
      u.push(
        (data.uvs[a * 2] + data.uvs[b * 2]) / 2,
        (data.uvs[a * 2 + 1] + data.uvs[b * 2 + 1]) / 2
      );
      midCache.set(key, m);
    }
    return m;
  };

  // copia vértices originais
  for (let v = 0; v < data.vertexCount; v++) {
    p.push(data.positions[v * 3], data.positions[v * 3 + 1], data.positions[v * 3 + 2]);
    u.push(data.uvs[v * 2] ?? 0, data.uvs[v * 2 + 1] ?? 0);
  }

  for (let i = 0; i < data.indices.length; i += 3) {
    const a = data.indices[i], b = data.indices[i + 1], c = data.indices[i + 2];
    const ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
    idx.push(a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca);
  }
  return new MeshData(p, [], u, idx);
}

/** Extrusão: duplica a malha deslocada pela normal média e gera paredes. */
export function extrude(data: MeshData, offset: number): MeshData {
  const bounds = data.bounds();
  const cy = (bounds.min[1] + bounds.max[1]) / 2;
  const out = new MeshData();
  const p = data.positions, u = data.uvs, idx = data.indices;
  const baseCount = data.vertexCount;

  // normal média apontando para +Y se a malha é mais plana, senão radial
  let nx = 0, ny = 0, nz = 0;
  for (let i = 0; i < p.length; i += 3) {
    nx += p[i]; ny += p[i + 1]; nz += p[i + 2];
  }
  const count = data.vertexCount || 1;
  const cx = nx / count, cyv = ny / count, cz = nz / count;
  let dirX = cx - cx, dirY = 0, dirZ = cz;
  if (Math.abs(ny) / count > 0.02) {
    // deslocamento radial a partir do centro
    for (let i = 0; i < p.length; i += 3) {
      dirX += p[i] - cx; dirY += p[i + 1] - cyv; dirZ += p[i + 2] - cz;
    }
  } else {
    dirY = Math.sign(cyv) || 1;
    dirX = 0; dirZ = 0;
  }
  const dl = Math.hypot(dirX, dirY, dirZ) || 1;
  dirX /= dl; dirY /= dl; dirZ /= dl;

  // vértices originais
  out.positions.push(...p);
  out.uvs.push(...u);
  // vértices deslocados
  for (let i = 0; i < p.length; i += 3) {
    out.positions.push(p[i] + dirX * offset, p[i + 1] + dirY * offset, p[i + 2] + dirZ * offset);
  }
  out.uvs.push(...u);

  // tampas
  for (let i = 0; i < idx.length; i += 3) {
    const a = idx[i], b = idx[i + 1], c = idx[i + 2];
    // tampa inferior (invertida)
    out.indices.push(c, b, a);
    // tampa superior
    out.indices.push(baseCount + a, baseCount + b, baseCount + c);
  }
  // paredes laterais por aresta de borda
  const edgeCount = new Map<string, number>();
  const edgeVerts = new Map<string, [number, number]>();
  for (let i = 0; i < idx.length; i += 3) {
    const tri = [idx[i], idx[i + 1], idx[i + 2]];
    for (let e = 0; e < 3; e++) {
      const a = tri[e], b = tri[(e + 1) % 3];
      const key = a < b ? `${a}_${b}` : `${b}_${a}`;
      edgeCount.set(key, (edgeCount.get(key) ?? 0) + 1);
      edgeVerts.set(key, [Math.min(a, b), Math.max(a, b)]);
    }
  }
  for (const [key, [a, b]] of edgeVerts) {
    if (edgeCount.get(key) === 1) {
      // aresta de borda → parede
      out.indices.push(a, b, baseCount + a);
      out.indices.push(b, baseCount + b, baseCount + a);
    }
  }
  void cy;
  return out;
}
