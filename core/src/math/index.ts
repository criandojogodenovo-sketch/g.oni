/**
 * G.oni Llumni — Núcleo Matemático
 * Vetores, matrizes, quaternions e utilidades.
 *
 * Nota de arquitetura: este módulo é o candidato nº 1 para migração
 * futura a C++/WASM (arquitetura idêntica, APIs puras e sem DOM).
 * Convenções: matrizes column-major (padrão WebGL), ângulos em radianos,
 * rotações Euler na ordem Y-X-Z (yaw, pitch, roll — natural para câmeras).
 */

export class MathUtils {
  static readonly DEG2RAD = Math.PI / 180;
  static readonly RAD2DEG = 180 / Math.PI;
  static readonly EPSILON = 1e-6;

  static clamp(v: number, min: number, max: number): number {
    return v < min ? min : v > max ? max : v;
  }
  static lerp(a: number, b: number, t: number): number {
    return a + (b - a) * t;
  }
  static smoothstep(a: number, b: number, t: number): number {
    const x = MathUtils.clamp((t - a) / (b - a), 0, 1);
    return x * x * (3 - 2 * x);
  }
  static easeIn(t: number): number { return t * t; }
  static easeOut(t: number): number { return 1 - (1 - t) * (1 - t); }
  static easeInOut(t: number): number { return t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2; }
  static bezier(t: number): number { return MathUtils.easeInOut(t); }
  static randRange(min: number, max: number): number {
    return min + Math.random() * (max - min);
  }
  static degToRad(d: number): number { return d * MathUtils.DEG2RAD; }
  static radToDeg(r: number): number { return r * MathUtils.RAD2DEG; }
}

export class Vec2 {
  constructor(public x = 0, public y = 0) {}
  set(x: number, y: number): this { this.x = x; this.y = y; return this; }
  clone(): Vec2 { return new Vec2(this.x, this.y); }
  copy(v: Vec2): this { this.x = v.x; this.y = v.y; return this; }
  add(v: Vec2): this { this.x += v.x; this.y += v.y; return this; }
  sub(v: Vec2): this { this.x -= v.x; this.y -= v.y; return this; }
  mul(s: number): this { this.x *= s; this.y *= s; return this; }
  len(): number { return Math.hypot(this.x, this.y); }
  norm(): this { const l = this.len(); return l > 1e-8 ? this.mul(1 / l) : this; }
  dot(v: Vec2): number { return this.x * v.x + this.y * v.y; }
  static get ZERO() { return new Vec2(0, 0); }
}

export class Vec3 {
  constructor(public x = 0, public y = 0, public z = 0) {}

  set(x: number, y: number, z: number): this { this.x = x; this.y = y; this.z = z; return this; }
  copy(v: Vec3): this { this.x = v.x; this.y = v.y; this.z = v.z; return this; }
  clone(): Vec3 { return new Vec3(this.x, this.y, this.z); }

  add(v: Vec3): this { this.x += v.x; this.y += v.y; this.z += v.z; return this; }
  addScaled(v: Vec3, s: number): this { this.x += v.x * s; this.y += v.y * s; this.z += v.z * s; return this; }
  sub(v: Vec3): this { this.x -= v.x; this.y -= v.y; this.z -= v.z; return this; }
  mul(s: number): this { this.x *= s; this.y *= s; this.z *= s; return this; }
  mulVec(v: Vec3): this { this.x *= v.x; this.y *= v.y; this.z *= v.z; return this; }

  dot(v: Vec3): number { return this.x * v.x + this.y * v.y + this.z * v.z; }
  cross(a: Vec3, b: Vec3): this {
    const ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
    this.x = ay * bz - az * by;
    this.y = az * bx - ax * bz;
    this.z = ax * by - ay * bx;
    return this;
  }
  crossVec(v: Vec3): Vec3 { return new Vec3().cross(this, v); }

  len(): number { return Math.sqrt(this.x * this.x + this.y * this.y + this.z * this.z); }
  lenSq(): number { return this.x * this.x + this.y * this.y + this.z * this.z; }
  norm(): this { const l = this.len(); return l > 1e-8 ? this.mul(1 / l) : this; }
  neg(): this { this.x = -this.x; this.y = -this.y; this.z = -this.z; return this; }

  distanceTo(v: Vec3): number { return this.clone().sub(v).len(); }
  lerp(a: Vec3, b: Vec3, t: number): this {
    this.x = a.x + (b.x - a.x) * t;
    this.y = a.y + (b.y - a.y) * t;
    this.z = a.z + (b.z - a.z) * t;
    return this;
  }

  toArray(): [number, number, number] { return [this.x, this.y, this.z]; }
  fromArray(a: ArrayLike<number>): this { this.x = a[0]; this.y = a[1]; this.z = a[2]; return this; }

  equals(v: Vec3): boolean {
    return Math.abs(this.x - v.x) < 1e-6 && Math.abs(this.y - v.y) < 1e-6 && Math.abs(this.z - v.z) < 1e-6;
  }
  toString(): string { return `Vec3(${this.x.toFixed(3)}, ${this.y.toFixed(3)}, ${this.z.toFixed(3)})`; }

  static get ZERO() { return new Vec3(0, 0, 0); }
  static get ONE() { return new Vec3(1, 1, 1); }
  static get UP() { return new Vec3(0, 1, 0); }
  static get DOWN() { return new Vec3(0, -1, 0); }
  static get RIGHT() { return new Vec3(1, 0, 0); }
  static get LEFT() { return new Vec3(-1, 0, 0); }
  static get FORWARD() { return new Vec3(0, 0, -1); }
  static get BACK() { return new Vec3(0, 0, 1); }
}

export class Vec4 {
  constructor(public x = 0, public y = 0, public z = 0, public w = 1) {}
  set(x: number, y: number, z: number, w: number): this { this.x = x; this.y = y; this.z = z; this.w = w; return this; }
  clone(): Vec4 { return new Vec4(this.x, this.y, this.z, this.w); }
  toArray(): [number, number, number, number] { return [this.x, this.y, this.z, this.w]; }
}

/**
 * Quaternion (x, y, z, w). Internamente usado para composição de rotação,
 * mas a API pública da engine usa Euler (graus/radianos) por simplicidade.
 */
export class Quat {
  constructor(public x = 0, public y = 0, public z = 0, public w = 1) {}

  clone(): Quat { return new Quat(this.x, this.y, this.z, this.w); }
  set(x: number, y: number, z: number, w: number): this { this.x = x; this.y = y; this.z = z; this.w = w; return this; }
  identity(): this { return this.set(0, 0, 0, 1); }

  norm(): this {
    const l = Math.hypot(this.x, this.y, this.z, this.w);
    if (l > 1e-8) { this.x /= l; this.y /= l; this.z /= l; this.w /= l; }
    return this;
  }

  /** Rotação a partir de eixo unitário e ângulo (radianos). */
  setFromAxisAngle(axis: Vec3, angle: number): this {
    const h = angle / 2, s = Math.sin(h);
    this.x = axis.x * s; this.y = axis.y * s; this.z = axis.z * s; this.w = Math.cos(h);
    return this;
  }

  /** Euler na ordem YXZ: primeiro yaw (Y), depois pitch (X), depois roll (Z). */
  setFromEuler(x: number, y: number, z: number): this {
    const cx = Math.cos(x / 2), sx = Math.sin(x / 2);
    const cy = Math.cos(y / 2), sy = Math.sin(y / 2);
    const cz = Math.cos(z / 2), sz = Math.sin(z / 2);
    // R = Ry * Rx * Rz
    this.w = cy * cx * cz + sy * sx * sz;
    this.x = cy * sx * cz + sy * cx * sz;
    this.y = sy * cx * cz - cy * sx * sz;
    this.z = cy * cx * sz - sy * sx * cz;
    return this;
  }

  multiply(q: Quat): this {
    const { x: a1, y: a2, z: a3, w: a4 } = this;
    const { x: b1, y: b2, z: b3, w: b4 } = q;
    this.x = a4 * b1 + a1 * b4 + a2 * b3 - a3 * b2;
    this.y = a4 * b2 + a2 * b4 + a3 * b1 - a1 * b3;
    this.z = a4 * b3 + a3 * b4 + a1 * b2 - a2 * b1;
    this.w = a4 * b4 - a1 * b1 - a2 * b2 - a3 * b3;
    return this;
  }

  slerp(a: Quat, b: Quat, t: number): this {
    let dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    let bx = b.x, by = b.y, bz = b.z, bw = b.w;
    if (dot < 0) { bx = -bx; by = -by; bz = -bz; bw = -bw; dot = -dot; }
    let s0 = 1 - t, s1 = t;
    if (dot > 0.9995) {
      s0 = 1 - t; s1 = t;
    } else {
      const theta = Math.acos(MathUtils.clamp(dot, -1, 1));
      const sin = Math.sin(theta);
      s0 = Math.sin((1 - t) * theta) / sin;
      s1 = Math.sin(t * theta) / sin;
    }
    this.x = a.x * s0 + bx * s1;
    this.y = a.y * s0 + by * s1;
    this.z = a.z * s0 + bz * s1;
    this.w = a.w * s0 + bw * s1;
    return this.norm();
  }

  applyToVec3(v: Vec3): Vec3 {
    // v' = q * v * q^-1 (forma otimizada)
    const { x: qx, y: qy, z: qz, w: qw } = this;
    const vx = v.x, vy = v.y, vz = v.z;
    const ix = qw * vx + qy * vz - qz * vy;
    const iy = qw * vy + qz * vx - qx * vz;
    const iz = qw * vz + qx * vy - qy * vx;
    const iw = -qx * vx - qy * vy - qz * vz;
    return new Vec3(
      ix * qw + iw * -qx + iy * -qz - iz * -qy,
      iy * qw + iw * -qy + iz * -qx - ix * -qz,
      iz * qw + iw * -qz + ix * -qy - iy * -qx
    );
  }

  static fromEuler(x: number, y: number, z: number): Quat {
    return new Quat().setFromEuler(x, y, z);
  }
}

/** Matriz 4x4 column-major (layout exigido pelo WebGL). */
export class Mat4 {
  /** m[col * 4 + row] */
  m = new Float32Array(16);

  constructor() { this.identity(); }

  static identity(): Mat4 { const r = new Mat4(); return r; }

  identity(): this {
    const m = this.m;
    m.fill(0);
    m[0] = 1; m[5] = 1; m[10] = 1; m[15] = 1;
    return this;
  }

  clone(): Mat4 {
    const r = new Mat4();
    r.m.set(this.m);
    return r;
  }

  copy(o: Mat4): this { this.m.set(o.m); return this; }

  equals(o: Mat4): boolean {
    for (let i = 0; i < 16; i++) if (Math.abs(this.m[i] - o.m[i]) > 1e-6) return false;
    return true;
  }

  /** this = this * o (aplica o DEPOIS de this). */
  multiply(o: Mat4): this {
    const a = this.m, b = o.m;
    const out = new Float32Array(16);
    for (let c = 0; c < 4; c++) {
      for (let r = 0; r < 4; r++) {
        let s = 0;
        for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
        out[c * 4 + r] = s;
      }
    }
    a.set(out);
    return this;
  }

  static multiply(a: Mat4, b: Mat4): Mat4 { return a.clone().multiply(b); }

  /** Pos-multiplicação de uma translação. */
  translate(x: number, y: number, z: number): this {
    const m = this.m;
    m[12] += m[0] * x + m[4] * y + m[8] * z;
    m[13] += m[1] * x + m[5] * y + m[9] * z;
    m[14] += m[2] * x + m[6] * y + m[10] * z;
    m[15] += m[3] * x + m[7] * y + m[11] * z;
    return this;
  }

  scale(x: number, y: number, z: number): this {
    const m = this.m;
    for (let i = 0; i < 3; i++) { m[i] *= x; m[4 + i] *= y; m[8 + i] *= z; }
    return this;
  }

  /** Inversa geral (implementação clássica verificada, estilo gl-matrix). */
  invert(): this {
    const a = this.m;
    const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
    const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
    const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
    const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];

    const b00 = a00 * a11 - a01 * a10;
    const b01 = a00 * a12 - a02 * a10;
    const b02 = a00 * a13 - a03 * a10;
    const b03 = a01 * a12 - a02 * a11;
    const b04 = a01 * a13 - a03 * a11;
    const b05 = a02 * a13 - a03 * a12;
    const b06 = a20 * a31 - a21 * a30;
    const b07 = a20 * a32 - a22 * a30;
    const b08 = a20 * a33 - a23 * a30;
    const b09 = a21 * a32 - a22 * a31;
    const b10 = a21 * a33 - a23 * a31;
    const b11 = a22 * a33 - a23 * a32;

    let det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (!det) return this.identity();
    det = 1.0 / det;

    const out = this.m;
    out[0] = (a11 * b11 - a12 * b10 + a13 * b09) * det;
    out[1] = (a02 * b10 - a01 * b11 - a03 * b09) * det;
    out[2] = (a31 * b05 - a32 * b04 + a33 * b03) * det;
    out[3] = (a22 * b04 - a21 * b05 - a23 * b03) * det;
    out[4] = (a12 * b08 - a10 * b11 - a13 * b07) * det;
    out[5] = (a00 * b11 - a02 * b08 + a03 * b07) * det;
    out[6] = (a32 * b02 - a30 * b05 - a33 * b01) * det;
    out[7] = (a20 * b05 - a22 * b02 + a23 * b01) * det;
    out[8] = (a10 * b10 - a11 * b08 + a13 * b06) * det;
    out[9] = (a01 * b08 - a00 * b10 - a03 * b06) * det;
    out[10] = (a30 * b04 - a31 * b02 + a33 * b00) * det;
    out[11] = (a21 * b02 - a20 * b04 - a23 * b00) * det;
    out[12] = (a11 * b07 - a10 * b09 - a12 * b06) * det;
    out[13] = (a00 * b09 - a01 * b07 + a02 * b06) * det;
    out[14] = (a31 * b01 - a30 * b03 - a32 * b00) * det;
    out[15] = (a20 * b03 - a21 * b01 + a22 * b00) * det;
    return this;
  }

  static invert(o: Mat4): Mat4 { return o.clone().invert(); }

  transpose(): this {
    const m = this.m, t = new Float32Array(16);
    for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) t[r * 4 + c] = m[c * 4 + r];
    m.set(t);
    return this;
  }

  /** Matriz normal a partir da model (inversa transposta da parte 3x3, sem escala). */
  normalMatrix(): Mat4 {
    // Para escala uniforme, a própria rotação serve; para não-uniforme aproximamos.
    const inv = Mat4.invert(this);
    return inv.transpose();
  }

  static perspective(fovyRad: number, aspect: number, near: number, far: number): Mat4 {
    const r = new Mat4();
    const f = 1 / Math.tan(fovyRad / 2);
    const nf = 1 / (near - far);
    const m = r.m;
    m.fill(0);
    m[0] = f / aspect; m[5] = f;
    m[10] = (far + near) * nf; m[11] = -1;
    m[14] = 2 * far * near * nf;
    return r;
  }

  static ortho(l: number, rgt: number, b: number, t: number, near: number, far: number): Mat4 {
    const r = new Mat4();
    const m = r.m;
    m.fill(0);
    m[0] = 2 / (rgt - l); m[5] = 2 / (t - b);
    m[10] = -2 / (far - near); m[15] = 1;
    m[12] = -(rgt + l) / (rgt - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(far + near) / (far - near);
    return r;
  }

  static lookAt(eye: Vec3, target: Vec3, up: Vec3): Mat4 {
    const r = new Mat4();
    const z = eye.clone().sub(target).norm(); // eixo -Z da câmera
    let x = new Vec3().cross(up, z);
    if (x.len() < 1e-6) {
      // up paralelo a z: escolhe fallback
      x = new Vec3().cross(new Vec3(0, 0, 1), z);
      if (x.len() < 1e-6) x = new Vec3().cross(new Vec3(1, 0, 0), z);
    }
    x.norm();
    const y = new Vec3().cross(z, x);
    const m = r.m;
    m[0] = x.x; m[4] = x.y; m[8] = x.z;
    m[1] = y.x; m[5] = y.y; m[9] = y.z;
    m[2] = z.x; m[6] = z.y; m[10] = z.z;
    m[12] = -x.dot(eye); m[13] = -y.dot(eye); m[14] = -z.dot(eye);
    m[15] = 1;
    return r;
  }

  transformPoint(p: Vec3): Vec3 {
    const m = this.m;
    const x = p.x, y = p.y, z = p.z;
    const w = m[3] * x + m[7] * y + m[11] * z + m[15] || 1;
    return new Vec3(
      (m[0] * x + m[4] * y + m[8] * z + m[12]) / w,
      (m[1] * x + m[5] * y + m[9] * z + m[13]) / w,
      (m[2] * x + m[6] * y + m[10] * z + m[14]) / w
    );
  }

  transformDir(p: Vec3): Vec3 {
    const m = this.m;
    return new Vec3(
      m[0] * p.x + m[4] * p.y + m[8] * p.z,
      m[1] * p.x + m[5] * p.y + m[9] * p.z,
      m[2] * p.x + m[6] * p.y + m[10] * p.z
    );
  }

  /** Composição TRS: translação * rotação(Euler YXZ) * escala. */
  static compose(position: Vec3, rotationEuler: Vec3, scale: Vec3): Mat4 {
    const q = Quat.fromEuler(rotationEuler.x, rotationEuler.y, rotationEuler.z);
    return Mat4.composeQuat(position, q, scale);
  }

  static composeQuat(position: Vec3, q: Quat, scale: Vec3): Mat4 {
    const r = new Mat4();
    const m = r.m;
    const { x: qx, y: qy, z: qz, w: qw } = q;
    const sx = scale.x, sy = scale.y, sz = scale.z;

    m[0] = (1 - 2 * (qy * qy + qz * qz)) * sx;
    m[1] = 2 * (qx * qy + qw * qz) * sx;
    m[2] = 2 * (qx * qz - qw * qy) * sx;

    m[4] = 2 * (qx * qy - qw * qz) * sy;
    m[5] = (1 - 2 * (qx * qx + qz * qz)) * sy;
    m[6] = 2 * (qy * qz + qw * qx) * sy;

    m[8] = 2 * (qx * qz + qw * qy) * sz;
    m[9] = 2 * (qy * qz - qw * qx) * sz;
    m[10] = (1 - 2 * (qx * qx + qy * qy)) * sz;

    m[12] = position.x; m[13] = position.y; m[14] = position.z;
    m[15] = 1;
    return r;
  }
}

/** Transform padrão de um G.oni Objeto. */
export class Transform {
  position = new Vec3();
  rotation = new Vec3(); // Euler radianos, ordem YXZ
  scale = new Vec3(1, 1, 1);

  clone(): Transform {
    const t = new Transform();
    t.position = this.position.clone();
    t.rotation = this.rotation.clone();
    t.scale = this.scale.clone();
    return t;
  }

  copy(o: Transform): this {
    this.position.copy(o.position);
    this.rotation.copy(o.rotation);
    this.scale.copy(o.scale);
    return this;
  }

  toMatrix(): Mat4 { return Mat4.compose(this.position, this.rotation, this.scale); }
}

/** Helpers de cor (0..1) com conversão para RGBA. */
export class Color {
  constructor(public r = 1, public g = 1, public b = 1, public a = 1) {}
  static hex(hex: string): Color {
    const h = hex.replace('#', '');
    const n = parseInt(h.length === 3 ? h.split('').map((c) => c + c).join('') : h, 16);
    return new Color(((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255, 1);
  }
  toCSS(): string {
    const c = (v: number) => Math.round(MathUtils.clamp(v, 0, 1) * 255).toString(16).padStart(2, '0');
    return `#${c(this.r)}${c(this.g)}${c(this.b)}`;
  }
  toArray(): [number, number, number, number] { return [this.r, this.g, this.b, this.a]; }
  clone(): Color { return new Color(this.r, this.g, this.b, this.a); }
}
