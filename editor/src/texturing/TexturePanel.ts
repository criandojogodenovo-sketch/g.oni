/**
 * G.oni Llumni — Editor de Texturização
 *
 * Pintura direta no modelo (raycast → UV → canvas 2D → textura),
 * cor/tamanho de pincel, preenchimento e camadas PBR (parâmetros).
 */

import { el, toast } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { MeshComponent } from '@core/goni/Objetos';
import { Color } from '@core/math';
import { Vec3, Mat4 } from '@core/math';

export class TexturePanel {
  brushColor = '#ffb020';
  brushSize = 24;
  painting = false;
  private canvas: HTMLCanvasElement | null = null;
  private ctx2d: CanvasRenderingContext2D | null = null;

  constructor(private app: EditorApp) {
    // intercepta ponteiro do viewport em modo pintura
    this.app.onViewportPaint = (x: number, y: number, w: number, h: number, isStart: boolean) => this.paintAt(x, y, w, h, isStart);
  }

  render(body: HTMLElement): void {
    body.innerHTML = '';
    const sel = this.app.viewport.selected;
    const mesh = sel?.getComponent<MeshComponent>('mesh');

    if (!sel || !mesh) {
      body.append(el('p', { class: 'hint' }, 'Selecione um objeto com malha para pintar texturas.'));
      return;
    }

    // garante canvas de pintura
    if (!mesh.material.paintCanvas) {
      const c = document.createElement('canvas');
      c.width = 512;
      c.height = 512;
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, 512, 512);
      mesh.material.paintCanvas = c;
    }
    this.canvas = mesh.material.paintCanvas;
    this.ctx2d = this.canvas.getContext('2d');

    // modo
    const modeBtn = el('button', {
      class: this.painting ? 'primary' : '',
      style: 'width:100%;margin-bottom:10px',
      onclick: () => {
        this.painting = !this.painting;
        modeBtn.className = this.painting ? 'primary' : '';
        modeBtn.textContent = this.painting ? '🖌 Pintura ATIVA — arraste no viewport' : '🖌 Ativar pintura no modelo';
      },
    }, '🖌 Ativar pintura no modelo');
    body.append(modeBtn);

    // preview do canvas
    const preview = el('canvas', {
      style: 'width:100%;border-radius:8px;border:1px solid var(--border);display:block;margin-bottom:10px;image-rendering:pixelated',
    }) as HTMLCanvasElement;
    body.append(preview);
    this.drawPreview(preview);

    // pincel
    const colorInput = el('input', { type: 'color', value: this.brushColor }) as HTMLInputElement;
    colorInput.addEventListener('input', () => { this.brushColor = colorInput.value; });
    body.append(el('div', { class: 'insp-row' }, el('label', {}, 'Cor'), colorInput));

    const sizeInput = el('input', { type: 'range', min: 4, max: 80, value: this.brushSize }) as HTMLInputElement;
    sizeInput.addEventListener('input', () => { this.brushSize = parseInt(sizeInput.value); });
    body.append(el('div', { class: 'insp-row' }, el('label', {}, 'Pincel'), sizeInput));

    // ações
    const actions = el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;margin-top:10px' },
      el('button', {
        class: 'small',
        onclick: () => {
          if (!this.ctx2d) return;
          this.ctx2d.fillStyle = this.brushColor;
          this.ctx2d.fillRect(0, 0, 512, 512);
          this.markTextureDirty(mesh);
          this.drawPreview(preview);
        },
      }, 'Preencher'),
      el('button', {
        class: 'small',
        onclick: () => {
          if (!this.ctx2d) return;
          this.ctx2d.fillStyle = '#ffffff';
          this.ctx2d.fillRect(0, 0, 512, 512);
          this.markTextureDirty(mesh);
          this.drawPreview(preview);
        },
      }, 'Limpar'),
    );
    body.append(actions);

    // camadas PBR
    body.append(el('div', { class: 'section-title' }, 'Camadas de material (PBR)'));
    const mat = mesh.material;
    const g = el('div', { class: 'insp-group' }, el('h3', {}, 'Albedo + parâmetros'));
    const alb = el('input', { type: 'color', value: mat.albedo.toCSS() }) as HTMLInputElement;
    alb.addEventListener('input', () => { mat.albedo = Color.hex(alb.value); });
    g.append(el('div', { class: 'insp-row' }, el('label', {}, 'Tint'), alb));
    g.append(this.slider('Rugosidade', mat.roughness, 0, 1, 0.01, (v) => { mat.roughness = v; }));
    g.append(this.slider('Metálico', mat.metallic, 0, 1, 0.01, (v) => { mat.metallic = v; }));
    g.append(this.slider('Emiss. força', mat.emissiveIntensity, 0, 4, 0.05, (v) => { mat.emissiveIntensity = v; }));
    const emColor = el('input', { type: 'color', value: mat.emissive.toCSS() }) as HTMLInputElement;
    emColor.addEventListener('input', () => { mat.emissive = Color.hex(emColor.value); });
    g.append(el('div', { class: 'insp-row' }, el('label', {}, 'Emissivo'), emColor));
    body.append(g);

    body.append(el('p', { class: 'hint' }, 'A pintura é mesclada com a cor base do material e atualiza em tempo real no viewport.'));
  }

  private slider(label: string, value: number, min: number, max: number, step: number, onChange: (v: number) => void): HTMLElement {
    const val = el('span', { style: 'font-size:11px;color:var(--text-3);min-width:38px;text-align:right;font-family:var(--font-mono)' }, value.toFixed(2));
    const r = el('input', { type: 'range', min, max, step, value }) as HTMLInputElement;
    r.addEventListener('input', () => {
      const v = parseFloat(r.value);
      val.textContent = v.toFixed(2);
      onChange(v);
    });
    return el('div', { class: 'insp-row' }, el('label', {}, label), el('div', { class: 'val' }, el('div', { style: 'display:flex;align-items:center;gap:8px' }, r, val)));
  }

  private drawPreview(preview: HTMLCanvasElement): void {
    if (!this.canvas) return;
    const pctx = preview.getContext('2d')!;
    preview.width = 512;
    preview.height = 512;
    pctx.drawImage(this.canvas, 0, 0);
  }

  private markTextureDirty(mesh: MeshComponent): void {
    (mesh.material as unknown as { paintDirty?: boolean }).paintDirty = true;
    this.app.markDirty();
  }

  /** Chamado pelo viewport quando o usuário arrasta com pintura ativa. */
  private paintAt(px: number, py: number, w: number, h: number, _isStart: boolean): void {
    if (!this.painting) return;
    const sel = this.app.viewport.selected;
    const mesh = sel?.getComponent<MeshComponent>('mesh');
    if (!sel || !mesh) return;
    const entry = this.app.engine.renderer.meshRegistry.get(mesh.meshId);
    if (!entry) return;

    const uv = rayMeshUV(px, py, w, h, sel, entry.data, this.app);
    if (!uv) return;

    if (!this.ctx2d || !this.canvas) {
      this.canvas = mesh.material.paintCanvas;
      this.ctx2d = this.canvas?.getContext('2d') ?? null;
    }
    if (!this.ctx2d || !this.canvas) return;

    const cx = uv.u * this.canvas.width;
    const cy = uv.v * this.canvas.height;
    this.ctx2d.fillStyle = this.brushColor;
    this.ctx2d.beginPath();
    this.ctx2d.arc(cx, cy, this.brushSize / 2, 0, Math.PI * 2);
    this.ctx2d.fill();
    this.markTextureDirty(mesh);
  }
}

/**
 * Interseção raio→triângulo com UV interpolado (baricêntrico).
 */
function rayMeshUV(px: number, py: number, w: number, h: number, obj: MeshComponent extends never ? never : import('@core/goni/Objetos').GOniObject, data: import('@core/render/Mesh').MeshData, app: import('@editor/EditorApp').EditorApp): { u: number; v: number } | null {
  const camera = app.viewport.camera;
  const ray = camera.screenRay(px, py, w, h);
  const inv = Mat4.invert(obj.worldMatrix);
  const lo = inv.transformPoint(ray.origin);
  const ld = inv.transformDir(ray.dir);
  const p = data.positions;
  const uv = data.uvs;
  const idx = data.indices;
  const triCount = idx.length ? idx.length / 3 : p.length / 9;
  let bestT = Infinity;
  let bestUV: { u: number; v: number } | null = null;

  for (let i = 0; i < triCount; i++) {
    const a = idx.length ? idx[i * 3] : i * 3;
    const b = idx.length ? idx[i * 3 + 1] : i * 3 + 1;
    const c = idx.length ? idx[i * 3 + 2] : i * 3 + 2;

    const ax = p[a * 3], ay = p[a * 3 + 1], az = p[a * 3 + 2];
    const e1x = p[b * 3] - ax, e1y = p[b * 3 + 1] - ay, e1z = p[b * 3 + 2] - az;
    const e2x = p[c * 3] - ax, e2y = p[c * 3 + 1] - ay, e2z = p[c * 3 + 2] - az;

    const hx = ld.y * e2z - ld.z * e2y;
    const hy = ld.z * e2x - ld.x * e2z;
    const hz = ld.x * e2y - ld.y * e2x;
    const det = e1x * hx + e1y * hy + e1z * hz;
    if (Math.abs(det) < 1e-9) continue;
    const invDet = 1 / det;

    const sx = lo.x - ax, sy = lo.y - ay, sz = lo.z - az;
    const u = (sx * hx + sy * hy + sz * hz) * invDet;
    if (u < -1e-6 || u > 1 + 1e-6) continue;

    const qx = sy * e1z - sz * e1y;
    const qy = sz * e1x - sx * e1z;
    const qz = sx * e1y - sy * e1x;
    const v = (ld.x * qx + ld.y * qy + ld.z * qz) * invDet;
    if (v < -1e-6 || u + v > 1 + 1e-6) continue;

    const t = (e2x * qx + e2y * qy + e2z * qz) * invDet;
    if (t > 1e-5 && t < bestT) {
      bestT = t;
      const wv = 1 - u - v;
      const ua = uv[a * 2] ?? 0, va = uv[a * 2 + 1] ?? 0;
      const ub = uv[b * 2] ?? 0, vb = uv[b * 2 + 1] ?? 0;
      const uc = uv[c * 2] ?? 0, vc = uv[c * 2 + 1] ?? 0;
      bestUV = {
        u: ua * wv + ub * u + uc * v,
        v: va * wv + vb * u + vc * v,
      };
    }
  }
  void Vec3;
  return bestUV;
}
