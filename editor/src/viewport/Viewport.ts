/**
 * G.oni Llumni — Viewport 3D do editor (mobile-first)
 *
 * Controles:
 *   Toque:  1 dedo = orbitar (toque curto = selecionar)
 *           2 dedos = pinça (zoom) + arrastar (pan)
 *   Mouse:  arraste esq. = orbitar · clique = selecionar
 *           arraste dir./meio = pan · roda = zoom
 *           WASD/QE = voar · F = focar seleção
 * Câmera: órbita (padrão), topo.
 * Picking: raio → esfera envolvente → refinamento por triângulo (Möller–Trumbore).
 */

import { Camera } from '@core/render/Camera';
import { Vec3, Mat4, MathUtils, Color } from '@core/math';
import { LineBatch } from '@core/render/Renderer';
import { GOniObject, MeshComponent } from '@core/goni/Objetos';
import { Engine } from '@core/goni/Engine';
import { Gizmo, GizmoMode } from './Gizmo';

export type ViewMode = 'orbit' | 'top';

export class Viewport {
  canvas: HTMLCanvasElement;
  camera = new Camera();
  engine: Engine;
  gizmo: Gizmo;

  // estado da órbita
  orbitYaw = MathUtils.degToRad(-35);
  orbitPitch = MathUtils.degToRad(22);
  orbitDist = 9;
  orbitTarget = new Vec3(0, 0.5, 0);
  viewMode: ViewMode = 'orbit';

  selected: GOniObject | null = null;
  onSelectionChange: ((obj: GOniObject | null) => void) | null = null;

  lines: LineBatch;
  overlay: LineBatch;

  private raf = 0;
  private lastTime = performance.now();
  private pointers = new Map<number, { x: number; y: number; startX: number; startY: number; button: number }>();
  private lastPinchDist = 0;
  private moved = false;
  private keys = new Set<string>();

  showColliders = false;
  private fps = 0;
  private fpsFrames = 0;
  private fpsTime = 0;
  hudElement: HTMLElement | null = null;

  constructor(parent: HTMLElement, engine: Engine) {
    this.engine = engine;
    this.canvas = document.createElement('canvas');
    parent.append(this.canvas);

    this.lines = new LineBatch(engine.renderer.gl);
    this.overlay = new LineBatch(engine.renderer.gl);
    this.gizmo = new Gizmo(this);

    this.bindEvents();
    this.updateCamera();
    void this.loop();
  }

  // ---------------- câmera ----------------

  updateCamera(): void {
    if (this.viewMode === 'top') {
      this.camera.eye = new Vec3(this.orbitTarget.x, this.orbitTarget.y + this.orbitDist, this.orbitTarget.z + 0.001);
      this.camera.target = this.orbitTarget.clone();
    } else {
      const cp = Math.cos(this.orbitPitch);
      const eye = new Vec3(
        this.orbitTarget.x + Math.sin(this.orbitYaw) * cp * this.orbitDist,
        this.orbitTarget.y + Math.sin(this.orbitPitch) * this.orbitDist,
        this.orbitTarget.z + Math.cos(this.orbitYaw) * cp * this.orbitDist
      );
      this.camera.eye = eye;
      this.camera.target = this.orbitTarget.clone();
    }
    this.camera.markViewDirty();
  }

  setViewMode(mode: ViewMode): void {
    this.viewMode = mode;
    this.camera.perspective = mode !== 'top' || true;
    this.updateCamera();
  }

  focusSelected(): void {
    if (!this.selected) return;
    const wp = this.selected.getWorldPosition();
    this.orbitTarget = wp.clone();
    const mesh = this.selected.getComponent<MeshComponent>('mesh');
    if (mesh) {
      const entry = this.engine.renderer.meshRegistry.get(mesh.meshId);
      if (entry) this.orbitDist = Math.max(2, entry.gpu.bounding.radius * 3.5);
    }
    this.updateCamera();
  }

  // ---------------- eventos ----------------

  private bindEvents(): void {
    const c = this.canvas;

    c.addEventListener('pointerdown', (e) => {
      c.setPointerCapture(e.pointerId);
      this.pointers.set(e.pointerId, {
        x: e.clientX, y: e.clientY,
        startX: e.clientX, startY: e.clientY,
        button: e.button,
      });
      this.moved = false;
      this.lastPinchDist = 0;

      // gizmo captura o toque
      if (e.button === 0 && this.pointers.size === 1 && !this.engine.playing) {
        const rect = c.getBoundingClientRect();
        if (this.gizmo.tryBeginDrag(e.clientX - rect.left, e.clientY - rect.top, rect.width, rect.height)) return;
      }
    });

    c.addEventListener('pointermove', (e) => {
      const p = this.pointers.get(e.pointerId);
      if (!p) return;
      const dx = e.clientX - p.x;
      const dy = e.clientY - p.y;
      if (Math.abs(e.clientX - p.startX) + Math.abs(e.clientY - p.startY) > 6) this.moved = true;
      p.x = e.clientX;
      p.y = e.clientY;

      // arrasto do gizmo
      if (this.gizmo.dragging) {
        const rect = c.getBoundingClientRect();
        this.gizmo.updateDrag(e.clientX - rect.left, e.clientY - rect.top, rect.width, rect.height);
        return;
      }

      if (this.pointers.size === 1) {
        // 1 dedo/botão: orbita (esq.) ou pan (dir./meio)
        const isPan = p.button === 2 || p.button === 1;
        if (isPan) this.pan(dx, dy);
        else this.orbit(dx, dy);
      } else if (this.pointers.size === 2) {
        // pinça: zoom + pan
        const pts = [...this.pointers.values()];
        const dist = Math.hypot(pts[0].x - pts[1].x, pts[0].y - pts[1].y);
        if (this.lastPinchDist > 0) {
          const factor = this.lastPinchDist / Math.max(dist, 1);
          this.orbitDist = MathUtils.clamp(this.orbitDist * factor, 0.8, 300);
        }
        this.lastPinchDist = dist;
        this.pan(dx * 0.5, dy * 0.5);
        this.updateCamera();
      }
    });

    const endPointer = (e: PointerEvent) => {
      const p = this.pointers.get(e.pointerId);
      this.pointers.delete(e.pointerId);
      this.lastPinchDist = 0;
      if (this.gizmo.dragging) {
        this.gizmo.endDrag();
        return;
      }
      // clique curto sem movimento → seleção
      if (p && !this.moved && p.button === 0 && !this.engine.playing) {
        const rect = c.getBoundingClientRect();
        this.pick(e.clientX - rect.left, e.clientY - rect.top, rect.width, rect.height);
      }
    };
    c.addEventListener('pointerup', endPointer);
    c.addEventListener('pointercancel', endPointer);

    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      const factor = Math.pow(1.0015, e.deltaY);
      this.orbitDist = MathUtils.clamp(this.orbitDist * factor, 0.8, 300);
      this.updateCamera();
    }, { passive: false });

    c.addEventListener('contextmenu', (e) => e.preventDefault());

    // teclado (desktop)
    window.addEventListener('keydown', (e) => {
      if ((e.target as HTMLElement)?.tagName === 'INPUT' || (e.target as HTMLElement)?.tagName === 'TEXTAREA') return;
      this.keys.add(e.key.toLowerCase());
      if (e.key.toLowerCase() === 'f') this.focusSelected();
    });
    window.addEventListener('keyup', (e) => this.keys.delete(e.key.toLowerCase()));
  }

  private orbit(dx: number, dy: number): void {
    this.orbitYaw -= dx * 0.008;
    this.orbitPitch = MathUtils.clamp(this.orbitPitch + dy * 0.008, -1.45, 1.45);
    this.updateCamera();
  }

  private pan(dx: number, dy: number): void {
    const speed = this.orbitDist * 0.0016;
    const view = this.camera.getViewDir();
    const right = new Vec3().cross(view, new Vec3(0, 1, 0)).norm();
    const up = new Vec3(0, 1, 0);
    this.orbitTarget.addScaled(right, -dx * speed);
    this.orbitTarget.addScaled(up, dy * speed);
    this.updateCamera();
  }

  private applyFlyKeys(dt: number): void {
    if (this.engine.playing || this.keys.size === 0) return;
    const speed = (this.keys.has('shift') ? 12 : 5) * dt;
    const view = this.camera.getViewDir();
    const right = new Vec3().cross(view, new Vec3(0, 1, 0)).norm();
    let moved = false;
    if (this.keys.has('w')) { this.orbitTarget.addScaled(view, speed); moved = true; }
    if (this.keys.has('s')) { this.orbitTarget.addScaled(view, -speed); moved = true; }
    if (this.keys.has('a')) { this.orbitTarget.addScaled(right, -speed); moved = true; }
    if (this.keys.has('d')) { this.orbitTarget.addScaled(right, speed); moved = true; }
    if (this.keys.has('q')) { this.orbitTarget.y -= speed; moved = true; }
    if (this.keys.has('e')) { this.orbitTarget.y += speed; moved = true; }
    if (moved) this.updateCamera();
  }

  // ---------------- picking ----------------

  pick(px: number, py: number, w: number, h: number): GOniObject | null {
    const ray = this.camera.screenRay(px, py, w, h);
    let best: { obj: GOniObject; t: number } | null = null;

    for (const obj of this.engine.scene.all()) {
      if (!obj.visible) continue;
      const mesh = obj.getComponent<MeshComponent>('mesh');
      if (!mesh) continue;
      const entry = this.engine.renderer.meshRegistry.get(mesh.meshId);
      if (!entry) continue;

      // teste rápido: esfera envolvente
      const scale = obj.transform.scale;
      const maxS = Math.max(Math.abs(scale.x), Math.abs(scale.y), Math.abs(scale.z));
      const center = obj.worldMatrix.transformPoint(new Vec3(
        entry.gpu.bounding.cx, entry.gpu.bounding.cy, entry.gpu.bounding.cz
      ));
      const radius = entry.gpu.bounding.radius * maxS;
      if (!raySphereHit(ray.origin, ray.dir, center, radius)) continue;

      // refinamento por triângulo
      const t = rayMeshTriangles(ray.origin, ray.dir, obj, entry.data, this.engine);
      if (t !== null) {
        if (!best || t < best.t) best = { obj, t };
      } else {
        // fallback: esfera serve (malhas muito grandes)
        const tSphere = raySphereT(ray.origin, ray.dir, center, radius);
        if (tSphere !== null && (!best || tSphere < best.t)) best = { obj, t: tSphere };
      }
    }

    this.select(best?.obj ?? null);
    return best?.obj ?? null;
  }

  select(obj: GOniObject | null): void {
    this.selected = obj;
    this.onSelectionChange?.(obj);
  }

  // ---------------- loop ----------------

  private async loop(): Promise<void> {
    const step = () => {
      this.raf = requestAnimationFrame(step);
      const now = performance.now();
      const dt = Math.min((now - this.lastTime) / 1000, 0.1);
      this.lastTime = now;
      if (!document.contains(this.canvas)) {
        cancelAnimationFrame(this.raf);
        return;
      }

      // fps
      this.fpsFrames++;
      this.fpsTime += dt;
      if (this.fpsTime >= 0.5) {
        this.fps = Math.round(this.fpsFrames / this.fpsTime);
        this.fpsFrames = 0;
        this.fpsTime = 0;
        if (this.hudElement) {
          const s = this.engine.renderer.stats;
          this.hudElement.textContent = `${this.fps} fps · ${s.drawCalls} draws · ${s.culled} culled`;
        }
      }

      this.applyFlyKeys(dt);

      // update da engine (física/scripts em play)
      this.engine.update(dt);

      // canvas size
      const rect = this.canvas.getBoundingClientRect();
      const w = Math.max(1, Math.floor(rect.width));
      const h = Math.max(1, Math.floor(rect.height));
      if (this.canvas.width !== Math.floor(w) || this.canvas.height !== Math.floor(h)) {
        this.canvas.style.width = `${w}px`;
        this.canvas.style.height = `${h}px`;
      }
      this.camera.setAspect(w, h);

      const cam = this.engine.playing && this.engine.activeCamera ? this.engine.activeCamera : this.camera;
      if (cam === this.engine.activeCamera) this.engine.activeCamera?.setAspect(w, h);

      // overlays de edição
      this.lines.clear();
      this.overlay.clear();
      if (!this.engine.playing) {
        this.drawEditorLines();
        this.gizmo.draw();
      }

      this.engine.renderFrame(cam, this.lines, this.overlay);
    };
    this.raf = requestAnimationFrame(step);
  }

  private drawEditorLines(): void {
    // bounding box da seleção
    if (this.selected) {
      const mesh = this.selected.getComponent<MeshComponent>('mesh');
      if (mesh) {
        const entry = this.engine.renderer.meshRegistry.get(mesh.meshId);
        if (entry) {
          const { min, max } = entry.data.bounds();
          this.lines.box(min, max, new Color(1, 0.68, 0.1), this.selected.worldMatrix);
        }
      }
      // colisor da seleção
      if (this.showColliders) this.drawCollider(this.selected, new Color(0.3, 0.9, 0.6));
    }
    // todos os colisores (modo debug)
    if (this.showColliders) {
      for (const obj of this.engine.scene.all()) {
        if (obj === this.selected) continue;
        const hasBody = !!obj.getComponent('rigidbody');
        this.drawCollider(obj, hasBody ? new Color(0.35, 0.65, 1) : new Color(0.45, 0.5, 0.6));
      }
    }
  }

  private drawCollider(obj: GOniObject, color: Color): void {
    const col = obj.getComponent('collider');
    if (!col) return;
    const collider = (col as unknown as { collider: import('@core/physics/RigidBody').Collider }).collider;
    const m = obj.worldMatrix;
    if (collider.shape === 'box') {
      const s = collider.size;
      this.lines.box([-s.x, -s.y, -s.z], [s.x, s.y, s.z], color, m);
    } else if (collider.shape === 'sphere') {
      this.lines.circle(new Vec3(0, 0, 0), collider.radius, 'x', color, 24, m);
      this.lines.circle(new Vec3(0, 0, 0), collider.radius, 'y', color, 24, m);
      this.lines.circle(new Vec3(0, 0, 0), collider.radius, 'z', color, 24, m);
    } else {
      // cápsula: 2 círculos + linhas
      const r = collider.radius;
      const hh = collider.height / 2;
      this.lines.circle(new Vec3(0, hh, 0), r, 'x', color, 20, m);
      this.lines.circle(new Vec3(0, -hh, 0), r, 'x', color, 20, m);
      for (const [x, z] of [[r, 0], [-r, 0], [0, r], [0, -r]]) {
        this.lines.line(x, -hh, z, x, hh, z, color);
      }
    }
  }

  setGizmoMode(mode: GizmoMode): void {
    this.gizmo.mode = mode;
  }

  dispose(): void {
    cancelAnimationFrame(this.raf);
    this.canvas.remove();
  }
}

// ---------------- ray helpers ----------------

function raySphereHit(origin: Vec3, dir: Vec3, center: Vec3, radius: number): boolean {
  const oc = origin.clone().sub(center);
  const b = oc.dot(dir);
  const c = oc.lenSq() - radius * radius;
  if (c > 0 && b > 0) return false;
  return b * b - c >= 0;
}

function raySphereT(origin: Vec3, dir: Vec3, center: Vec3, radius: number): number | null {
  const oc = origin.clone().sub(center);
  const b = oc.dot(dir);
  const c = oc.lenSq() - radius * radius;
  const disc = b * b - c;
  if (disc < 0) return null;
  const t = -b - Math.sqrt(disc);
  return t >= 0 ? t : 0;
}

/** Interseção raio-triângulo no espaço do objeto (Möller–Trumbore). */
function rayMeshTriangles(origin: Vec3, dir: Vec3, obj: GOniObject, data: import('@core/render/Mesh').MeshData, engine: Engine): number | null {
  const inv = Mat4.invert(obj.worldMatrix);
  const lo = inv.transformPoint(origin);
  const ld = inv.transformDir(dir);
  const p = data.positions;
  const idx = data.indices;
  const triCount = idx.length ? idx.length / 3 : p.length / 9;
  let bestT: number | null = null;

  for (let i = 0; i < triCount; i++) {
    const a = idx.length ? idx[i * 3] : i * 3;
    const b = idx.length ? idx[i * 3 + 1] : i * 3 + 1;
    const c = idx.length ? idx[i * 3 + 2] : i * 3 + 2;

    const ax = p[a * 3], ay = p[a * 3 + 1], az = p[a * 3 + 2];
    const e1x = p[b * 3] - ax, e1y = p[b * 3 + 1] - ay, e1z = p[b * 3 + 2] - az;
    const e2x = p[c * 3] - ax, e2y = p[c * 3 + 1] - ay, e2z = p[c * 3 + 2] - az;

    // h = ld × e2
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
    if (t > 1e-5 && (bestT === null || t < bestT)) bestT = t;
  }
  void engine;
  if (bestT === null) return null;
  // converte para distância de mundo aproximada
  return bestT;
}
