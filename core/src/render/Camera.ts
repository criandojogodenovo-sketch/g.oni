/**
 * G.oni Llumni — Câmeras
 * Perspectiva/ortográfica, com ray-casting de tela para o mundo
 * (usado pelo picking do editor e pelo raycast de scripts).
 */

import { Mat4, Vec3, MathUtils } from '../math';

export class Camera {
  perspective = true;
  fovY = MathUtils.degToRad(60);
  orthoSize = 10;
  aspect = 1;
  near = 0.1;
  far = 500;

  eye = new Vec3(5, 5, 5);
  target = new Vec3(0, 0.5, 0);
  up = new Vec3(0, 1, 0);

  private _proj = new Mat4();
  private _view = new Mat4();
  private _projDirty = true;
  private _viewDirty = true;

  setFovDegrees(deg: number): this {
    this.fovY = MathUtils.degToRad(deg);
    this._projDirty = true;
    return this;
  }

  setAspect(w: number, h: number): this {
    this.aspect = w / Math.max(1, h);
    this._projDirty = true;
    return this;
  }

  updateMatrices(): void {
    if (this._projDirty) {
      this._proj = this.perspective
        ? Mat4.perspective(this.fovY, this.aspect, this.near, this.far)
        : Mat4.ortho(-this.orthoSize * this.aspect, this.orthoSize * this.aspect, -this.orthoSize, this.orthoSize, this.near, this.far);
      this._projDirty = false;
    }
    if (this._viewDirty) {
      this._view = Mat4.lookAt(this.eye, this.target, this.up);
      this._viewDirty = false;
    }
  }

  get proj(): Mat4 { this.updateMatrices(); return this._proj; }
  get view(): Mat4 { this.updateMatrices(); return this._view; }

  getViewDir(): Vec3 {
    return this.target.clone().sub(this.eye).norm();
  }

  markViewDirty(): void { this._viewDirty = true; }

  /**
   * Raio do mundo a partir de coordenadas de tela (px) dentro de w x h.
   * Retorna origem (eye) e direção normalizada.
   */
  screenRay(px: number, py: number, w: number, h: number): { origin: Vec3; dir: Vec3 } {
    const ndcX = (px / w) * 2 - 1;
    const ndcY = 1 - (py / h) * 2;

    const invProj = Mat4.invert(this.proj);
    const invView = Mat4.invert(this.view);

    // pontos em espaço de recorte (z=-1 perto, z=1 longe)
    const near = invView.transformPoint(invProj.transformPoint(new Vec3(ndcX, ndcY, -1)));
    const far = invView.transformPoint(invProj.transformPoint(new Vec3(ndcX, ndcY, 1)));
    const dir = far.clone().sub(near).norm();
    return { origin: near.clone(), dir };
  }

  /** Projeta um ponto do mundo em coordenadas de tela (px) — para gizmos. */
  worldToScreen(p: Vec3, w: number, h: number): { x: number; y: number; z: number } {
    const vp = Mat4.multiply(this.proj, this.view);
    const c = vp.transformPoint(p);
    return {
      x: (c.x * 0.5 + 0.5) * w,
      y: (1 - (c.y * 0.5 + 0.5)) * h,
      z: c.z,
    };
  }
}
