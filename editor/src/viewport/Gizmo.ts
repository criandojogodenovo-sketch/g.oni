/**
 * G.oni Llumni — Gizmos de transformação
 *
 * Mover: 3 eixos (linha-a-linha em 3D via ponto mais próximo raio↔eixo)
 * Rotacionar: anéis por eixo (ângulo em espaço de tela)
 * Escalar: pontas de eixo + centro (uniforme)
 * Hit-test em espaço de tela (≤ 22px).
 */

import { Vec3, Color, MathUtils, Mat4, Quat } from '@core/math';
import { Viewport } from './Viewport';
import { GOniObject } from '@core/goni/Objetos';

export type GizmoMode = 'select' | 'translate' | 'rotate' | 'scale';

const AXIS_COLORS = {
  x: new Color(1, 0.3, 0.3),
  y: new Color(0.3, 1, 0.45),
  z: new Color(0.35, 0.55, 1),
};

type Axis = 'x' | 'y' | 'z';
type DragKind = { type: 'axis'; axis: Axis } | { type: 'center' } | { type: 'ring'; axis: Axis } | null;

const AXIS_DIRS: Record<Axis, Vec3> = {
  x: new Vec3(1, 0, 0),
  y: new Vec3(0, 1, 0),
  z: new Vec3(0, 0, 1),
};

export class Gizmo {
  mode: GizmoMode = 'select';
  dragging = false;
  /** callback para registrar undo antes da edição */
  onBeginEdit: (() => void) | null = null;

  private drag: DragKind = null;
  private startScreen = { x: 0, y: 0 };
  private startPos = new Vec3();
  private startRot = new Vec3();
  private startScale = new Vec3();
  private startAngle = 0;
  private startDist = 0;
  private vp: Viewport;

  constructor(vp: Viewport) {
    this.vp = vp;
  }

  // ---------------- desenho ----------------

  draw(): void {
    const obj = this.vp.selected;
    if (!obj || this.mode === 'select') return;
    const pos = obj.getWorldPosition();
    const lines = this.vp.overlay;
    const size = this.gizmoSize();

    if (this.mode === 'translate') {
      for (const axis of ['x', 'y', 'z'] as Axis[]) {
        const d = AXIS_DIRS[axis].clone().mul(size);
        const color = this.drag?.type === 'axis' && this.drag.axis === axis ? new Color(1, 0.85, 0.2) : AXIS_COLORS[axis];
        lines.line(pos.x, pos.y, pos.z, pos.x + d.x, pos.y + d.y, pos.z + d.z, color);
        // seta (2 segmentos)
        const tip = pos.clone().add(d);
        const perp1 = axis === 'y' ? new Vec3(0, 0, size * 0.12) : new Vec3(0, size * 0.12, 0);
        const perp2 = axis === 'x' ? new Vec3(0, 0, size * 0.12) : new Vec3(size * 0.12, 0, 0);
        lines.line(tip.x, tip.y, tip.z, tip.x - perp1.x - perp2.x, tip.y - perp1.y - perp2.y, tip.z - perp1.z - perp2.z, color);
        lines.line(tip.x, tip.y, tip.z, tip.x + perp1.x - perp2.x, tip.y + perp1.y - perp2.y, tip.z + perp1.z - perp2.z, color);
      }
      // cubo central (XY da tela)
      const c = size * 0.12;
      lines.box(
        [pos.x - c, pos.y - c, pos.z - c], [pos.x + c, pos.y + c, pos.z + c],
        this.drag?.type === 'center' ? new Color(1, 0.85, 0.2) : new Color(0.9, 0.9, 0.95)
      );
    } else if (this.mode === 'rotate') {
      for (const axis of ['x', 'y', 'z'] as Axis[]) {
        const color = this.drag?.type === 'ring' && this.drag.axis === axis ? new Color(1, 0.85, 0.2) : AXIS_COLORS[axis];
        lines.circle(pos, size, axis, color, 40);
      }
      // eixo central
      lines.line(pos.x, pos.y, pos.z - size * 0.4, pos.x, pos.y, pos.z + size * 0.4, new Color(0.9, 0.9, 0.95));
    } else if (this.mode === 'scale') {
      const tipC = size * 0.08;
      for (const axis of ['x', 'y', 'z'] as Axis[]) {
        const d = AXIS_DIRS[axis].clone().mul(size);
        const color = this.drag?.type === 'axis' && this.drag.axis === axis ? new Color(1, 0.85, 0.2) : AXIS_COLORS[axis];
        lines.line(pos.x, pos.y, pos.z, pos.x + d.x, pos.y + d.y, pos.z + d.z, color);
        const tip = pos.clone().add(d);
        lines.box([tip.x - tipC, tip.y - tipC, tip.z - tipC], [tip.x + tipC, tip.y + tipC, tip.z + tipC], color);
      }
      const c = size * 0.15;
      lines.box([pos.x - c, pos.y - c, pos.z - c], [pos.x + c, pos.y + c, pos.z + c],
        this.drag?.type === 'center' ? new Color(1, 0.85, 0.2) : new Color(0.9, 0.9, 0.95));
    }
  }

  private gizmoSize(): number {
    // tamanho constante em tela: proporcional à distância da câmera
    const dist = this.vp.camera.eye.distanceTo(this.vp.selected?.getWorldPosition() ?? new Vec3());
    return Math.max(dist * 0.14, 0.2);
  }

  // ---------------- hit-test ----------------

  /** Tenta iniciar o arrasto nas coordenadas de tela (px locais do canvas). */
  tryBeginDrag(px: number, py: number, w: number, h: number): boolean {
    const obj = this.vp.selected;
    if (!obj || this.mode === 'select') return false;
    const cam = this.vp.camera;
    const pos = obj.getWorldPosition();
    const size = this.gizmoSize();

    const toScreen = (p: Vec3) => cam.worldToScreen(p, w, h);
    const center = toScreen(pos);

    if (this.mode === 'translate') {
      for (const axis of ['x', 'y', 'z'] as Axis[]) {
        const end = toScreen(pos.clone().add(AXIS_DIRS[axis].clone().mul(size)));
        if (distToSegment(px, py, center.x, center.y, end.x, end.y) < 18) {
          this.beginDrag({ type: 'axis', axis }, obj, px, py);
          return true;
        }
      }
      if (Math.hypot(px - center.x, py - center.y) < 22) {
        this.beginDrag({ type: 'center' }, obj, px, py);
        return true;
      }
    } else if (this.mode === 'rotate') {
      const r = screenRadius(pos, size, cam, w, h);
      const d = Math.hypot(px - center.x, py - center.y);
      if (Math.abs(d - r) < 20) {
        // escolhe o eixo cujo anel está mais "de frente"
        const viewDir = cam.getViewDir();
        let bestAxis: Axis = 'y';
        let bestAlign = -2;
        for (const axis of ['x', 'y', 'z'] as Axis[]) {
          const align = Math.abs(AXIS_DIRS[axis].dot(viewDir));
          // o anel "clicado" deve ser perpendicular à visão (círculo visível)
          const vis = 1 - align;
          if (vis > bestAlign) { bestAlign = vis; bestAxis = axis; }
        }
        this.beginDrag({ type: 'ring', axis: bestAxis }, obj, px, py);
        return true;
      }
    } else if (this.mode === 'scale') {
      for (const axis of ['x', 'y', 'z'] as Axis[]) {
        const end = toScreen(pos.clone().add(AXIS_DIRS[axis].clone().mul(size)));
        if (distToSegment(px, py, center.x, center.y, end.x, end.y) < 20) {
          this.beginDrag({ type: 'axis', axis }, obj, px, py);
          return true;
        }
      }
      if (Math.hypot(px - center.x, py - center.y) < 24) {
        this.beginDrag({ type: 'center' }, obj, px, py);
        return true;
      }
    }
    return false;
  }

  private beginDrag(kind: DragKind, obj: GOniObject, px: number, py: number): void {
    this.dragging = true;
    this.drag = kind;
    this.startScreen = { x: px, y: py };
    this.startPos = obj.transform.position.clone();
    this.startRot = obj.transform.rotation.clone();
    this.startScale = obj.transform.scale.clone();
    const c = this.screenCenter(obj);
    this.startAngle = Math.atan2(py - c.y, px - c.x);
    this.startDist = Math.hypot(px - c.x, py - c.y);
    this.axisStartT = null;
    this.onBeginEdit?.();
  }

  endDrag(): void {
    this.dragging = false;
    this.drag = null;
    this.axisStartT = null;
  }

  // ---------------- arrasto ----------------

  updateDrag(px: number, py: number, w: number, h: number): void {
    const obj = this.vp.selected;
    if (!obj || !this.drag) return;
    const cam = this.vp.camera;
    const world = obj.getWorldPosition();

    if (this.drag.type === 'axis' && this.mode === 'translate') {
      // ponto mais próximo entre o raio do mouse e a reta do eixo (3D)
      const ray = cam.screenRay(px, py, w, h);
      const axisDir = AXIS_DIRS[this.drag.axis];
      const t = closestPointRayLine(ray.origin, ray.dir, world, axisDir);
      if (t === null) return;
      if (this.axisStartT === null) {
        this.axisStartT = t;
        return;
      }
      const delta = t - this.axisStartT;
      const localAxis = worldAxisToLocal(obj, axisDir);
      obj.transform.position.copy(this.startPos.clone().addScaled(localAxis, delta));
      obj.markDirty();
    } else if (this.drag.type === 'center' && this.mode === 'translate') {
      // plano perpendicular à câmera passando pelo objeto
      const ray = cam.screenRay(px, py, w, h);
      const startRay = cam.screenRay(this.startScreen.x, this.startScreen.y, w, h);
      const viewDir = cam.getViewDir();
      const pNow = intersectPlane(ray.origin, ray.dir, world, viewDir);
      const pStart = intersectPlane(startRay.origin, startRay.dir, world, viewDir);
      if (pNow && pStart) {
        const delta = pNow.clone().sub(pStart);
        obj.transform.position.copy(this.startPos.clone().add(delta));
        obj.markDirty();
      }
    } else if (this.drag.type === 'ring') {
      const angle = this.angleTo(obj, px, py);
      let d = angle - this.startAngle;
      while (d > Math.PI) d -= Math.PI * 2;
      while (d < -Math.PI) d += Math.PI * 2;
      // converte para radianos locais: eixo do gizmo é mundo; aplica em espaço local
      const axis = AXIS_DIRS[this.drag.axis];
      const q = new Quat().setFromAxisAngle(axis, d);
      const startQ = Quat.fromEuler(this.startRot.x, this.startRot.y, this.startRot.z);
      // rotação em mundo = q * rotação inicial
      const worldQ = q.multiply(startQ).norm();
      obj.transform.rotation.set(
        Math.atan2(2 * (worldQ.w * worldQ.x + worldQ.y * worldQ.z), 1 - 2 * (worldQ.x * worldQ.x + worldQ.y * worldQ.y)),
        Math.asin(MathUtils.clamp(2 * (worldQ.w * worldQ.y - worldQ.z * worldQ.x), -1, 1)),
        Math.atan2(2 * (worldQ.w * worldQ.z + worldQ.x * worldQ.y), 1 - 2 * (worldQ.y * worldQ.y + worldQ.z * worldQ.z))
      );
      obj.markDirty();
    } else if (this.mode === 'scale') {
      const d0 = this.startDist || 1;
      const d = this.distTo(obj, px, py);
      const factor = Math.max(0.05, d / d0);
      if (this.drag.type === 'center') {
        obj.transform.scale.copy(this.startScale.clone().mul(factor));
      } else {
        const axis = this.drag.axis;
        obj.transform.scale.copy(this.startScale.clone());
        if (axis === 'x') obj.transform.scale.x = this.startScale.x * factor;
        if (axis === 'y') obj.transform.scale.y = this.startScale.y * factor;
        if (axis === 'z') obj.transform.scale.z = this.startScale.z * factor;
      }
      obj.markDirty();
    }
  }

  private axisStartT: number | null = null;

  /** Centro do objeto em coordenadas de tela. */
  private screenCenter(obj: GOniObject): { x: number; y: number } {
    const rect = this.vp.canvas.getBoundingClientRect();
    return this.vp.camera.worldToScreen(obj.getWorldPosition(), rect.width, rect.height);
  }

  private angleTo(obj: GOniObject, px: number, py: number): number {
    const c = this.screenCenter(obj);
    return Math.atan2(py - c.y, px - c.x);
  }

  private distTo(obj: GOniObject, px: number, py: number): number {
    const c = this.screenCenter(obj);
    return Math.hypot(px - c.x, py - c.y);
  }
}

// ---------------- helpers ----------------

function distToSegment(px: number, py: number, ax: number, ay: number, bx: number, by: number): number {
  const dx = bx - ax, dy = by - ay;
  const lenSq = dx * dx + dy * dy;
  if (lenSq < 1e-6) return Math.hypot(px - ax, py - ay);
  let t = ((px - ax) * dx + (py - ay) * dy) / lenSq;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (ax + t * dx), py - (ay + t * dy));
}

function screenRadius(worldPos: Vec3, worldSize: number, cam: import('@core/render/Camera').Camera, w: number, h: number): number {
  const center = cam.worldToScreen(worldPos, w, h);
  const edge = cam.worldToScreen(worldPos.clone().add(new Vec3(worldSize, 0, 0)), w, h);
  return Math.hypot(edge.x - center.x, edge.y - center.y);
}

/** Ponto mais próximo raio ∩ reta: retorna t ao longo da RETA. */
function closestPointRayLine(rayOrigin: Vec3, rayDir: Vec3, linePoint: Vec3, lineDir: Vec3): number | null {
  const w = linePoint.clone().sub(rayOrigin);
  const a = rayDir.dot(rayDir);      // |rd|² = 1
  const b = rayDir.dot(lineDir);
  const c = lineDir.dot(lineDir);
  const d = rayDir.dot(w);
  const e = lineDir.dot(w);
  const denom = a * c - b * b;
  if (Math.abs(denom) < 1e-9) return null;
  const t = (b * d - a * e) / denom; // parâmetro na RETA? (sinal conforme derivação)
  // fórmula clássica: s = (b*e - c*d)/denom (raio), t = (a*e - b*d)/denom (linha)
  return (a * e - b * d) / denom;
}

/** Interseção raio-plano (plano com normal n passando por p0). */
function intersectPlane(origin: Vec3, dir: Vec3, p0: Vec3, n: Vec3): Vec3 | null {
  const denom = dir.dot(n);
  if (Math.abs(denom) < 1e-9) return null;
  const t = p0.clone().sub(origin).dot(n) / denom;
  if (t < 0) return null;
  return origin.clone().addScaled(dir, t);
}

/** Converte direção de mundo para o espaço local do objeto (rotação do pai). */
function worldAxisToLocal(obj: GOniObject, worldDir: Vec3): Vec3 {
  // para objetos-raiz sem rotação de pai: identidade
  if (!obj.parent) return worldDir.clone();
  const parentWorld = obj.parent.worldMatrix;
  const inv = Mat4.invert(parentWorld);
  return inv.transformDir(worldDir).norm();
}
