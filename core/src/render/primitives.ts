/**
 * G.oni Llumni — Geradores de primitivas
 * Malhas paramétricas com UVs consistentes para texturização/pintura.
 */

import { MeshData } from './Mesh';

/** Cubo unitário centrado (tamanho final = size por eixo). */
export function createCube(size = 1): MeshData {
  const h = size / 2;
  const p: number[] = [];
  const n: number[] = [];
  const u: number[] = [];
  const i: number[] = [];

  const faces: [number[], number[], number[]][] = [
    // [normal, eixo U da face, eixo V da face] → 4 cantos
    [[0, 0, 1], [1, 0, 0], [0, 1, 0]],
    [[0, 0, -1], [-1, 0, 0], [0, 1, 0]],
    [[1, 0, 0], [0, 0, -1], [0, 1, 0]],
    [[-1, 0, 0], [0, 0, 1], [0, 1, 0]],
    [[0, 1, 0], [1, 0, 0], [0, 0, -1]],
    [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
  ];

  for (const [normal, axU, axV] of faces) {
    const base = p.length / 3;
    const corners = [
      [-1, -1], [1, -1], [1, 1], [-1, 1],
    ];
    for (const [cu, cv] of corners) {
      p.push(
        normal[0] * h + axU[0] * h * cu + axV[0] * h * cv,
        normal[1] * h + axU[1] * h * cu + axV[1] * h * cv,
        normal[2] * h + axU[2] * h * cu + axV[2] * h * cv
      );
      n.push(normal[0], normal[1], normal[2]);
      u.push((cu + 1) / 2, (cv + 1) / 2);
    }
    i.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  return new MeshData(p, n, u, i);
}

/** Esfera UV (latitude/longitude), radius, segments de anel. */
export function createSphere(radius = 0.5, widthSeg = 24, heightSeg = 16): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  for (let y = 0; y <= heightSeg; y++) {
    const v = y / heightSeg;
    const phi = v * Math.PI;
    for (let x = 0; x <= widthSeg; x++) {
      const uu = x / widthSeg;
      const theta = uu * Math.PI * 2;
      const nx = Math.cos(theta) * Math.sin(phi);
      const ny = Math.cos(phi);
      const nz = Math.sin(theta) * Math.sin(phi);
      p.push(nx * radius, ny * radius, nz * radius);
      n.push(nx, ny, nz);
      u.push(1 - uu, 1 - v);
    }
  }
  for (let y = 0; y < heightSeg; y++) {
    for (let x = 0; x < widthSeg; x++) {
      const a = y * (widthSeg + 1) + x;
      const b = a + widthSeg + 1;
      i.push(a, b, a + 1, b, b + 1, a + 1);
    }
  }
  return new MeshData(p, n, u, i);
}

/** Plano no XZ, subdividido. */
export function createPlane(width = 1, depth = 1, segs = 1): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  for (let z = 0; z <= segs; z++) {
    for (let x = 0; x <= segs; x++) {
      const fx = x / segs, fz = z / segs;
      p.push((fx - 0.5) * width, 0, (fz - 0.5) * depth);
      n.push(0, 1, 0);
      u.push(fx, fz);
    }
  }
  for (let z = 0; z < segs; z++) {
    for (let x = 0; x < segs; x++) {
      const a = z * (segs + 1) + x;
      const b = a + segs + 1;
      i.push(a, b, a + 1, b, b + 1, a + 1);
    }
  }
  return new MeshData(p, n, u, i);
}

/** Cilindro no eixo Y. */
export function createCylinder(radius = 0.5, height = 1, radialSeg = 24): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  const h = height / 2;

  // lateral
  for (let y = 0; y <= 1; y++) {
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      p.push(c * radius, (y === 0 ? -h : h), s * radius);
      n.push(c, 0, s);
      u.push(x / radialSeg, y);
    }
  }
  for (let x = 0; x < radialSeg; x++) {
    const a = x, b = x + radialSeg + 1;
    i.push(a, b, a + 1, b, b + 1, a + 1);
  }
  // tampas
  for (const dir of [1, -1]) {
    const base = p.length / 3;
    const ny = dir;
    p.push(0, ny * h, 0);
    n.push(0, ny, 0);
    u.push(0.5, 0.5);
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      p.push(c * radius, ny * h, s * radius);
      n.push(0, ny, 0);
      u.push(c * 0.5 + 0.5, s * 0.5 + 0.5);
    }
    for (let x = 0; x < radialSeg; x++) {
      const a = base + 1 + x;
      if (dir > 0) i.push(base, a, a + 1);
      else i.push(base, a + 1, a);
    }
  }
  return new MeshData(p, n, u, i);
}

/** Cone no eixo Y (ponta para cima). */
export function createCone(radius = 0.5, height = 1, radialSeg = 24): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  const h = height / 2;
  // lateral (vértices duplicados por anel para normais duras)
  for (let ring = 0; ring < 2; ring++) {
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      const y = ring === 0 ? -h : h;
      const rr = ring === 0 ? radius : 0;
      p.push(c * rr, y, s * rr);
      const slope = radius / height;
      const nl = 1 / Math.hypot(1, slope);
      n.push(c * nl, slope * nl, s * nl);
      u.push(x / radialSeg, ring);
    }
  }
  for (let x = 0; x < radialSeg; x++) {
    const a = x, b = x + radialSeg + 1;
    i.push(a, b, a + 1);
  }
  // base
  const base = p.length / 3;
  p.push(0, -h, 0);
  n.push(0, -1, 0);
  u.push(0.5, 0.5);
  for (let x = 0; x <= radialSeg; x++) {
    const t = (x / radialSeg) * Math.PI * 2;
    p.push(Math.cos(t) * radius, -h, Math.sin(t) * radius);
    n.push(0, -1, 0);
    u.push(0.5 + Math.cos(t) * 0.5, 0.5 + Math.sin(t) * 0.5);
  }
  for (let x = 0; x < radialSeg; x++) i.push(base, base + 1 + x + 1, base + 1 + x);
  return new MeshData(p, n, u, i);
}

/** Cápsula (cilindro + hemisférios) no eixo Y — ideal para character controller. */
export function createCapsule(radius = 0.3, height = 1, radialSeg = 16, rings = 8): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  const halfH = Math.max(0.01, height / 2 - radius);
  // topo
  for (let r = 0; r <= rings; r++) {
    const phi = (r / rings) * (Math.PI / 2);
    const rr = radius * Math.cos(phi);
    const yy = radius * Math.sin(phi);
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      p.push(c * rr, halfH + yy, s * rr);
      n.push(c * Math.cos(phi), Math.sin(phi), s * Math.cos(phi));
      u.push(x / radialSeg, r / rings * 0.25);
    }
  }
  const topCount = (rings + 1) * (radialSeg + 1);
  // base (espelho)
  for (let r = 0; r <= rings; r++) {
    const phi = (r / rings) * (Math.PI / 2);
    const rr = radius * Math.cos(phi);
    const yy = -radius * Math.sin(phi);
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      p.push(c * rr, -halfH + yy, s * rr);
      n.push(c * Math.cos(phi), -Math.sin(phi), s * Math.cos(phi));
      u.push(x / radialSeg, 0.75 + r / rings * 0.25);
    }
  }
  const total = p.length / 3;
  // lateral central
  const cylBase = total;
  for (let y = 0; y <= 1; y++) {
    for (let x = 0; x <= radialSeg; x++) {
      const t = (x / radialSeg) * Math.PI * 2;
      const c = Math.cos(t), s = Math.sin(t);
      p.push(c * radius, y === 0 ? -halfH : halfH, s * radius);
      n.push(c, 0, s);
      u.push(x / radialSeg, 0.25 + y * 0.5);
    }
  }
  // índices topo
  for (let r = 0; r < rings; r++) {
    for (let x = 0; x < radialSeg; x++) {
      const a = r * (radialSeg + 1) + x;
      i.push(a, a + radialSeg + 1, a + 1, a + radialSeg + 1, a + radialSeg + 2, a + 1);
    }
  }
  // índices base
  const baseOffset = topCount;
  for (let r = 0; r < rings; r++) {
    for (let x = 0; x < radialSeg; x++) {
      const a = baseOffset + r * (radialSeg + 1) + x;
      i.push(a, a + 1, a + radialSeg + 1, a + radialSeg + 1, a + 1, a + radialSeg + 2);
    }
  }
  // lateral
  for (let x = 0; x < radialSeg; x++) {
    const a = cylBase + x;
    const b = cylBase + radialSeg + 1 + x;
    i.push(a, b, a + 1, b, b + 1, a + 1);
  }
  // junta topo→lateral
  const topRing = rings * (radialSeg + 1);
  for (let x = 0; x < radialSeg; x++) {
    i.push(topRing + x, cylBase + radialSeg + 1 + x, topRing + x + 1);
  }
  const bottomRing = baseOffset + rings * (radialSeg + 1);
  for (let x = 0; x < radialSeg; x++) {
    i.push(bottomRing + x, bottomRing + x + 1, cylBase + x);
  }
  return new MeshData(p, n, u, i);
}

/** Torus no plano XZ. */
export function createTorus(radius = 0.4, tube = 0.15, radialSeg = 32, tubeSeg = 12): MeshData {
  const p: number[] = [], n: number[] = [], u: number[] = [], i: number[] = [];
  for (let r = 0; r <= radialSeg; r++) {
    const t = (r / radialSeg) * Math.PI * 2;
    const ct = Math.cos(t), st = Math.sin(t);
    for (let s = 0; s <= tubeSeg; s++) {
      const ph = (s / tubeSeg) * Math.PI * 2;
      const cp = Math.cos(ph), sp = Math.sin(ph);
      p.push((radius + tube * cp) * ct, tube * sp, (radius + tube * cp) * st);
      n.push(cp * ct, sp, cp * st);
      u.push(r / radialSeg, s / tubeSeg);
    }
  }
  for (let r = 0; r < radialSeg; r++) {
    for (let s = 0; s < tubeSeg; s++) {
      const a = r * (tubeSeg + 1) + s;
      const b = a + tubeSeg + 1;
      i.push(a, b, a + 1, b, b + 1, a + 1);
    }
  }
  return new MeshData(p, n, u, i);
}

export const PRIMITIVES: Record<string, () => MeshData> = {
  cube: () => createCube(1),
  sphere: () => createSphere(0.5, 24, 16),
  plane: () => createPlane(2, 2, 1),
  cylinder: () => createCylinder(0.5, 1, 24),
  cone: () => createCone(0.5, 1, 24),
  capsule: () => createCapsule(0.3, 1),
  torus: () => createTorus(0.4, 0.15),
};
