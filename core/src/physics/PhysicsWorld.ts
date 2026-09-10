/**
 * G.oni Llumni — Mundo de Física
 *
 * Implementação própria, mobile-first:
 *  - Integração semi-implícita de Euler com passo fixo (acumulador + substeps)
 *  - Broadphase O(n²) com AABBs + early-out (culling de estáticos dormentes)
 *  - Narrowphase: esfera-esfera, esfera-OBB, OBB-OBB (SAT 15 eixos),
 *    cápsula-cápsula (segmento-segmento), cápsula≈esfera (aprox. documentada)
 *  - Solver de impulsos sequenciais (6 iterações) com restituição,
 *    atrito (Coulomb clamp) e correção posicional de Baumgarte
 *  - Inércia rotacional de caixa (diagonal local → mundo por rotação)
 *  - Sleep com despertar por contato
 *  - Eventos: on_collision_enter / on_collision_exit (nativos G.oni Signal)
 */

import { Vec3, Quat, Mat4 } from '../math';
import { Collider, RigidBody } from './RigidBody';
import { GOniObject, ColliderComponent, RigidBodyComponent } from '../goni/Objetos';
import { Scene } from '../goni/Scene';

export interface PhysBody {
  obj: GOniObject;
  collider: Collider;
  rigid: RigidBody;

  /** estado de mundo */
  position: Vec3;       // centro do colisor no mundo
  velocity: Vec3;
  angularVelocity: Vec3;
  force: Vec3;
  torque: Vec3;

  /** matriz de rotação 3x3 (colunas = eixos da caixa) */
  rot: Float32Array;    // 9
  invMass: number;
  invInertia: Float32Array; // 9 (mundo)

  halfExtents: Vec3;    // para box (mundo, sem rotação aplicada)
  radius: number;
  capsuleHeight: number;

  sleeping: boolean;
  sleepTimer: number;

  aabbMin: Vec3;
  aabbMax: Vec3;

  /** par de contato ativo (para eventos) */
  contactPairs: Set<string>;
}

export interface Contact {
  a: PhysBody;
  b: PhysBody;
  normal: Vec3; // de A para B
  point: Vec3;
  penetration: number;
}

export interface RaycastHit {
  obj: GOniObject;
  point: Vec3;
  normal: Vec3;
  distance: number;
}

function mat3MulVec(m: Float32Array, v: Vec3): Vec3 {
  return new Vec3(
    m[0] * v.x + m[3] * v.y + m[6] * v.z,
    m[1] * v.x + m[4] * v.y + m[7] * v.z,
    m[2] * v.x + m[5] * v.y + m[8] * v.z
  );
}

function mat3FromQuat(q: Quat, out: Float32Array): void {
  const { x, y, z, w } = q;
  out[0] = 1 - 2 * (y * y + z * z); out[1] = 2 * (x * y + w * z); out[2] = 2 * (x * z - w * y);
  out[3] = 2 * (x * y - w * z); out[4] = 1 - 2 * (x * x + z * z); out[5] = 2 * (y * z + w * x);
  out[6] = 2 * (x * z + w * y); out[7] = 2 * (y * z - w * x); out[8] = 1 - 2 * (x * x + y * y);
}

export class PhysicsWorld {
  bodies: PhysBody[] = [];
  gravity = new Vec3(0, -9.81, 0);

  /** passo fixo e substeps */
  fixedStep = 1 / 60;
  maxSubsteps = 4;
  private accumulator = 0;

  /** stats */
  stats = { bodies: 0, contacts: 0, activePairs: 0, steps: 0 };

  /** Chave canônica de par de contato (ordem independente). */
  static pairKey(a: PhysBody, b: PhysBody): string {
    return a.obj.id < b.obj.id ? `${a.obj.id}|${b.obj.id}` : `${b.obj.id}|${a.obj.id}`;
  }

  /** callbacks de evento (conectados pela Engine aos sinais dos objetos) */
  onCollisionEnter: ((a: PhysBody, b: PhysBody, contact: Contact) => void) | null = null;
  onCollisionExit: ((a: PhysBody, b: PhysBody) => void) | null = null;

  private activePairs = new Map<string, { a: PhysBody; b: PhysBody; contact: Contact }>();

  /** Sincroniza corpos com os objetos da cena (chamar após montar a cena). */
  syncFromScene(scene: Scene): void {
    this.bodies = [];
    for (const obj of scene.all()) {
      const rb = obj.getComponent('rigidbody') as RigidBodyComponent | undefined;
      const col = obj.getComponent('collider') as import('../goni/Objetos').ColliderComponent | undefined;
      if (!rb || !col) continue;
      if (!col.enabled || !rb.enabled) continue;
      this.addBody(obj, col.collider, rb.body);
    }
  }

  addBody(obj: GOniObject, collider: Collider, rigid: RigidBody): PhysBody {
    scene_updateWorld(obj);
    const world = obj.worldMatrix;
    const pos = world.transformPoint(collider.offset.clone());

    const body: PhysBody = {
      obj,
      collider,
      rigid,
      position: pos,
      velocity: rigid.linearVelocity.clone(),
      angularVelocity: rigid.angularVelocity.clone(),
      force: new Vec3(),
      torque: new Vec3(),
      rot: new Float32Array(9),
      invMass: rigid.type === 'dynamic' && rigid.mass > 0 ? 1 / rigid.mass : 0,
      invInertia: new Float32Array(9),
      halfExtents: collider.shape === 'box' ? collider.size.clone() : new Vec3(collider.radius),
      radius: collider.radius,
      capsuleHeight: collider.height,
      sleeping: false,
      sleepTimer: 0,
      aabbMin: new Vec3(),
      aabbMax: new Vec3(),
      contactPairs: new Set(),
    };
    this.updateDerived(body);
    this.bodies.push(body);
    return body;
  }

  removeBody(obj: GOniObject): void {
    this.bodies = this.bodies.filter((b) => b.obj !== obj);
    for (const [k, p] of this.activePairs) {
      if (p.a.obj === obj || p.b.obj === obj) this.activePairs.delete(k);
    }
  }

  getBody(obj: GOniObject): PhysBody | undefined {
    return this.bodies.find((b) => b.obj === obj);
  }

  private updateDerived(body: PhysBody): void {
    const euler = body.obj.transform.rotation;
    const q = Quat.fromEuler(euler.x, euler.y, euler.z);
    mat3FromQuat(q, body.rot);

    // inércia local (diagonal) → mundo
    const m = body.rigid.type === 'dynamic' ? body.rigid.mass : 1;
    const inv = new Float32Array(9);
    if (body.rigid.type === 'dynamic' && !body.rigid.freezeRotation && m > 0) {
      let ixx: number, iyy: number, izz: number;
      if (body.collider.shape === 'box') {
        const h = body.collider.size;
        const sy = 2 * h.y, sz = 2 * h.z, sx = 2 * h.x;
        ixx = (m / 12) * (sy * sy + sz * sz);
        iyy = (m / 12) * (sx * sx + sz * sz);
        izz = (m / 12) * (sx * sx + sy * sy);
      } else if (body.collider.shape === 'capsule') {
        const r = body.collider.radius, h = body.collider.height;
        ixx = (m / 12) * (3 * r * r + h * h) + (m / 6) * r * r;
        iyy = ixx;
        izz = (m / 5) * r * r;
      } else {
        const r = body.collider.radius;
        ixx = iyy = izz = (2 / 5) * m * r * r;
      }
      const ixxInv = ixx > 1e-9 ? 1 / ixx : 0;
      const iyyInv = iyy > 1e-9 ? 1 / iyy : 0;
      const izzInv = izz > 1e-9 ? 1 / izz : 0;
      // invI_world = R * diag_inv * R^T
      const r = body.rot;
      for (let col = 0; col < 3; col++) {
        for (let row = 0; row < 3; row++) {
          let s = 0;
          for (let k = 0; k < 3; k++) {
            const dInv = k === 0 ? ixxInv : k === 1 ? iyyInv : izzInv;
            s += r[col * 3 + row] * dInv * r[k * 3 + col];
          }
          inv[col * 3 + row] = s;
        }
      }
    }
    body.invInertia = inv;

    // AABB aproximado (esfera envolvente do colisor)
    let r: number;
    if (body.collider.shape === 'box') {
      const h = body.collider.size;
      r = Math.sqrt(h.x * h.x + h.y * h.y + h.z * h.z);
    } else if (body.collider.shape === 'capsule') {
      r = body.collider.height / 2 + body.collider.radius;
    } else {
      r = body.collider.radius;
    }
    body.aabbMin.set(body.position.x - r, body.position.y - r, body.position.z - r);
    body.aabbMax.set(body.position.x + r, body.position.y + r, body.position.z + r);
  }

  /** Avança a física com dt variável externamente, internamente passo fixo. */
  update(dt: number): void {
    this.accumulator += Math.min(dt, 0.25);
    let steps = 0;
    while (this.accumulator >= this.fixedStep && steps < this.maxSubsteps) {
      this.step(this.fixedStep);
      this.accumulator -= this.fixedStep;
      steps++;
    }
    if (steps === this.maxSubsteps) this.accumulator = 0;
    this.stats.steps = steps;
    this.stats.bodies = this.bodies.length;
  }

  /** Um passo de física. */
  step(dt: number): void {
    // 0. corpos cinemáticos seguem o transform definido por scripts
    for (const b of this.bodies) {
      if (b.rigid.type === 'kinematic') {
        b.position = b.obj.worldMatrix.transformPoint(b.collider.offset.clone());
        this.updateDerived(b);
      }
    }

    // 1. integração
    for (const b of this.bodies) {
      if (b.rigid.type !== 'dynamic' || b.rigid.sleeping) continue;
      b.sleeping = false;

      if (b.rigid.useGravity) b.velocity.addScaled(this.gravity, b.rigid.gravityScale * dt);
      b.velocity.addScaled(b.force, b.invMass * dt);
      b.velocity.mul(1 / (1 + b.rigid.linearDamping * dt));
      b.position.addScaled(b.velocity, dt);

      if (!b.rigid.freezeRotation) {
        // torque → delta angular via inércia inversa
        const dOmega = mat3MulVec(b.invInertia, b.torque);
        b.angularVelocity.addScaled(dOmega, dt);
        b.angularVelocity.mul(1 / (1 + b.rigid.angularDamping * dt));
        // integra rotação
        const w = b.angularVelocity;
        if (w.lenSq() > 1e-10) {
          const angle = w.len() * dt;
          const axis = w.clone().norm();
          const dq = new Quat().setFromAxisAngle(axis, angle);
          const q = Quat.fromEuler(b.obj.transform.rotation.x, b.obj.transform.rotation.y, b.obj.transform.rotation.z);
          q.multiply(dq).norm();
          applyQuatToTransform(b.obj, q);
        }
      }
      b.force.set(0, 0, 0);
      b.torque.set(0, 0, 0);
      this.updateDerived(b);

      // teste de sleep
      if (b.rigid.canSleep) {
        if (b.velocity.lenSq() < 0.003 && b.angularVelocity.lenSq() < 0.01) {
          b.sleepTimer += dt;
          if (b.sleepTimer > 0.6) {
            b.rigid.sleeping = true;
            b.velocity.set(0, 0, 0);
            b.angularVelocity.set(0, 0, 0);
          }
        } else {
          b.sleepTimer = 0;
        }
      }
    }

    // 2. broadphase + narrowphase
    const contacts: Contact[] = [];
    const n = this.bodies.length;
    for (let i = 0; i < n; i++) {
      const a = this.bodies[i];
      if (a.rigid.type === 'static') continue;
      for (let j = 0; j < n; j++) {
        if (i === j) continue;
        const b = this.bodies[j];
        if (a.rigid.type !== 'dynamic' && b.rigid.type !== 'dynamic') continue;
        // AABB overlap
        if (a.aabbMax.x < b.aabbMin.x || a.aabbMin.x > b.aabbMax.x) continue;
        if (a.aabbMax.y < b.aabbMin.y || a.aabbMin.y > b.aabbMax.y) continue;
        if (a.aabbMax.z < b.aabbMin.z || a.aabbMin.z > b.aabbMax.z) continue;
        const c = this.narrowphase(a, b);
        if (c) contacts.push(c);
      }
    }
    this.stats.contacts = contacts.length;

    // 3. eventos enter/exit
    this.fireEvents(contacts);

    // 4. resolver (apenas não-triggers)
    const real = contacts.filter((c) => !c.a.collider.isTrigger && !c.b.collider.isTrigger);
    for (let iter = 0; iter < 6; iter++) {
      for (const c of real) this.resolveContact(c, iter === 0);
    }
    // 5. correção posicional
    for (const c of real) this.positionalCorrection(c);

    // 6. escreve resultado nos objetos
    for (const b of this.bodies) {
      if (b.rigid.type !== 'dynamic') continue;
      b.obj.transform.position.copy(b.position.clone().sub(
        mat3MulVec(b.rot, b.collider.offset)
      ));
      b.rigid.linearVelocity.copy(b.velocity);
      b.rigid.angularVelocity.copy(b.angularVelocity);
      b.obj.markDirty();
    }
  }

  private fireEvents(contacts: Contact[]): void {
    const seen = new Set<string>();
    for (const c of contacts) {
      const key = PhysicsWorld.pairKey(c.a, c.b);
      seen.add(key);
      if (!this.activePairs.has(key)) {
        this.activePairs.set(key, { a: c.a, b: c.b, contact: c });
        this.onCollisionEnter?.(c.a, c.b, c);
      } else {
        this.activePairs.set(key, { a: c.a, b: c.b, contact: c });
      }
    }
    for (const [key, p] of [...this.activePairs]) {
      if (!seen.has(key)) {
        this.activePairs.delete(key);
        this.onCollisionExit?.(p.a, p.b);
      }
    }
    this.stats.activePairs = this.activePairs.size;
  }

  // ---------------- Narrowphase ----------------

  private narrowphase(a: PhysBody, b: PhysBody): Contact | null {
    const sa = a.collider.shape, sb = b.collider.shape;
    if (sa === 'sphere' && sb === 'sphere') return this.sphereSphere(a, b);
    if (sa === 'sphere' && sb === 'box') return this.sphereBox(a, b);
    if (sa === 'box' && sb === 'sphere') {
      const c = this.sphereBox(b, a);
      if (c) { c.normal.neg(); swapContact(c); }
      return c;
    }
    if (sa === 'box' && sb === 'box') return this.boxBox(a, b);
    // cápsulas
    if (sa === 'capsule' && sb === 'capsule') return this.capsuleCapsule(a, b);
    if (sa === 'capsule' && sb === 'sphere') {
      const c = this.sphereCapsule(b, a);
      if (c) { c.normal.neg(); swapContact(c); }
      return c;
    }
    if (sa === 'sphere' && sb === 'capsule') return this.sphereCapsule(a, b);
    // capsule vs box: aproxima cápsula por esfera central (documentado)
    if (sa === 'capsule' && sb === 'box') return this.sphereLikeBox(a, b);
    if (sa === 'box' && sb === 'capsule') {
      const c = this.sphereLikeBox(b, a);
      if (c) { c.normal.neg(); swapContact(c); }
      return c;
    }
    return null;
  }

  private sphereSphere(a: PhysBody, b: PhysBody): Contact | null {
    const d = b.position.clone().sub(a.position);
    const dist = d.len();
    const rr = a.radius + b.radius;
    if (dist >= rr) return null;
    const normal = dist > 1e-6 ? d.mul(1 / dist) : new Vec3(0, 1, 0);
    return {
      a, b, normal,
      point: a.position.clone().addScaled(normal, a.radius - (rr - dist) / 2),
      penetration: rr - dist,
    };
  }

  private sphereBox(s: PhysBody, bx: PhysBody): Contact | null {
    // esfera em espaço local da caixa
    const rel = s.position.clone().sub(bx.position);
    const inv = transpose3(bx.rot);
    const local = mat3MulVec(inv, rel);
    const h = bx.collider.shape === 'box' ? bx.collider.size : new Vec3(bx.radius);
    const clamped = new Vec3(
      Math.max(-h.x, Math.min(h.x, local.x)),
      Math.max(-h.y, Math.min(h.y, local.y)),
      Math.max(-h.z, Math.min(h.z, local.z))
    );
    const delta = local.clone().sub(clamped);
    const distSq = delta.lenSq();
    const r = s.radius;
    let normalLocal: Vec3;
    let penetration: number;
    if (distSq > 1e-10) {
      const dist = Math.sqrt(distSq);
      if (dist >= r) return null;
      normalLocal = delta.mul(1 / dist);
      penetration = r - dist;
    } else {
      // centro dentro da caixa: empurra pela face mais próxima
      const dx = h.x - Math.abs(local.x);
      const dy = h.y - Math.abs(local.y);
      const dz = h.z - Math.abs(local.z);
      if (dx < dy && dx < dz) {
        normalLocal = new Vec3(Math.sign(local.x) || 1, 0, 0);
        penetration = r + dx;
      } else if (dy < dz) {
        normalLocal = new Vec3(0, Math.sign(local.y) || 1, 0);
        penetration = r + dy;
      } else {
        normalLocal = new Vec3(0, 0, Math.sign(local.z) || 1);
        penetration = r + dz;
      }
    }
    const normalWorld = mat3MulVec(bx.rot, normalLocal);
    const point = bx.position.clone().add(mat3MulVec(bx.rot, clamped));
    // normal aponta da caixa para fora → invertemos para a convenção A(esfera)→B(caixa)
    const contact: Contact = {
      a: s, b: bx,
      normal: normalWorld.neg(),
      point,
      penetration,
    };
    return contact;
  }

  /** cápsula tratada como esfera central contra caixa (aproximação v1). */
  private sphereLikeBox(cap: PhysBody, bx: PhysBody): Contact | null {
    const fake: PhysBody = Object.assign(Object.create(Object.getPrototypeOf(cap)), cap);
    fake.radius = cap.radius + cap.capsuleHeight / 2;
    // retorna a=cápsula, b=caixa, normal cápsula→caixa
    return this.sphereBox(fake, bx);
  }

  private sphereCapsule(s: PhysBody, cap: PhysBody): Contact | null {
    // ponto mais próximo do eixo da cápsula ao centro da esfera
    const axis = mat3MulVec(cap.rot, new Vec3(0, 1, 0));
    const rel = s.position.clone().sub(cap.position);
    const t = Math.max(-cap.capsuleHeight / 2, Math.min(cap.capsuleHeight / 2, rel.dot(axis)));
    const closest = cap.position.clone().addScaled(axis, t);
    const d = s.position.clone().sub(closest);
    const dist = d.len();
    const rr = s.radius + cap.radius;
    if (dist >= rr) return null;
    const normal = dist > 1e-6 ? d.mul(1 / dist) : new Vec3(0, 1, 0);
    return {
      a: s, b: cap, normal,
      point: closest.clone().addScaled(normal, cap.radius),
      penetration: rr - dist,
    };
  }

  private capsuleCapsule(a: PhysBody, b: PhysBody): Contact | null {
    const axisA = mat3MulVec(a.rot, new Vec3(0, 1, 0));
    const axisB = mat3MulVec(b.rot, new Vec3(0, 1, 0));
    const hA = a.capsuleHeight / 2, hB = b.capsuleHeight / 2;
    const pa1 = a.position.clone().addScaled(axisA, -hA);
    const pa2 = a.position.clone().addScaled(axisA, hA);
    const pb1 = b.position.clone().addScaled(axisB, -hB);
    const pb2 = b.position.clone().addScaled(axisB, hB);
    const { t: s, u } = closestSegmentSegment(pa1, pa2, pb1, pb2);
    const ca = pa1.clone().addScaled(pa2.clone().sub(pa1), s);
    const cb = pb1.clone().addScaled(pb2.clone().sub(pb1), u);
    const d = cb.clone().sub(ca);
    const dist = d.len();
    const rr = a.radius + b.radius;
    if (dist >= rr) return null;
    const normal = dist > 1e-6 ? d.mul(1 / dist) : new Vec3(0, 1, 0);
    return {
      a, b, normal,
      point: ca.clone().addScaled(normal, a.radius - (rr - dist) / 2),
      penetration: rr - dist,
    };
  }

  private boxBox(a: PhysBody, b: PhysBody): Contact | null {
    const rotA = a.rot, rotB = b.rot;
    const hA = a.collider.size, hB = b.collider.size;
    const T = b.position.clone().sub(a.position);

    const axesA = [
      new Vec3(rotA[0], rotA[1], rotA[2]),
      new Vec3(rotA[3], rotA[4], rotA[5]),
      new Vec3(rotA[6], rotA[7], rotA[8]),
    ];
    const axesB = [
      new Vec3(rotB[0], rotB[1], rotB[2]),
      new Vec3(rotB[3], rotB[4], rotB[5]),
      new Vec3(rotB[6], rotB[7], rotB[8]),
    ];

    const allAxes: Vec3[] = [...axesA, ...axesB];
    for (const u of axesA) for (const v of axesB) {
      const c = new Vec3().cross(u, v);
      if (c.lenSq() > 1e-6) allAxes.push(c.norm());
    }

    let minPen = Infinity;
    let minAxis: Vec3 | null = null;

    for (const axis of allAxes) {
      const ra = extentAlong(hA, axesA, axis);
      const rb = extentAlong(hB, axesB, axis);
      const dist = Math.abs(T.dot(axis));
      const overlap = ra + rb - dist;
      if (overlap <= 0) return null;
      if (overlap < minPen) {
        minPen = overlap;
        minAxis = axis;
      }
    }
    if (!minAxis) return null;

    // orientar o eixo de A para B
    let axis = minAxis;
    if (T.dot(axis) < 0) axis = axis.clone().neg();

    const ra = extentAlong(hA, axesA, axis);
    const rb = extentAlong(hB, axesB, axis);
    const sep = Math.abs(T.dot(axis));
    // ponto médio da região de sobreposição ao longo do eixo
    const distFromA = (sep - rb + ra) / 2;
    const point = a.position.clone().addScaled(axis, distFromA);

    return { a, b, normal: axis.clone(), point, penetration: minPen };
  }

  // ---------------- Solver ----------------

  private resolveContact(c: Contact, applyRestitution: boolean): void {
    const { a, b, normal, point } = c;
    const invMa = a.invMass, invMb = b.invMass;
    const invMassSum = invMa + invMb;
    if (invMassSum === 0) return;

    const ra = point.clone().sub(a.position);
    const rb = point.clone().sub(b.position);

    // velocidade relativa no ponto
    const velA = a.velocity.clone().add(a.angularVelocity.crossVec(ra));
    const velB = b.velocity.clone().add(b.angularVelocity.crossVec(rb));
    const rv = velB.clone().sub(velA);
    const vn = rv.dot(normal);
    if (vn > 0) return; // separando

    // massa efetiva normal
    const angA = mat3MulVec(a.invInertia, ra.crossVec(normal));
    const angB = mat3MulVec(b.invInertia, rb.crossVec(normal));
    const kn = invMassSum + angA.crossVec(ra).dot(normal) + angB.crossVec(rb).dot(normal);

    const e = applyRestitution && Math.abs(vn) > 0.4 ? Math.min(a.rigid.restitution, b.rigid.restitution) : 0;
    let j = -(1 + e) * vn / Math.max(kn, 1e-9);
    j = Math.max(j, 0);

    const impulse = normal.clone().mul(j);
    applyImpulse(a, impulse.neg(), ra);
    applyImpulse(b, impulse, rb);

    // atrito
    const velA2 = a.velocity.clone().add(a.angularVelocity.crossVec(ra));
    const velB2 = b.velocity.clone().add(b.angularVelocity.crossVec(rb));
    const rv2 = velB2.clone().sub(velA2);
    const tangent = rv2.clone().sub(normal.clone().mul(rv2.dot(normal)));
    if (tangent.lenSq() > 1e-10) {
      tangent.norm();
      const angAt = mat3MulVec(a.invInertia, ra.crossVec(tangent));
      const angBt = mat3MulVec(b.invInertia, rb.crossVec(tangent));
      const kt = invMassSum + angAt.crossVec(ra).dot(tangent) + angBt.crossVec(rb).dot(tangent);
      let jt = -rv2.dot(tangent) / Math.max(kt, 1e-9);
      const mu = Math.sqrt(a.rigid.friction * b.rigid.friction);
      jt = Math.max(-mu * j, Math.min(mu * j, jt));
      const tImp = tangent.mul(jt);
      applyImpulse(a, tImp.neg(), ra);
      applyImpulse(b, tImp, rb);
    }

    // despertar
    if (a.rigid.type === 'dynamic' && !a.rigid.sleeping && b.rigid.type === 'dynamic' && b.rigid.sleeping) {
      wake(b);
    }
    if (b.rigid.type === 'dynamic' && !b.rigid.sleeping && a.rigid.type === 'dynamic' && a.rigid.sleeping) {
      wake(a);
    }
  }

  private positionalCorrection(c: Contact): void {
    const { a, b, normal, penetration } = c;
    const invMa = a.invMass, invMb = b.invMass;
    const invSum = invMa + invMb;
    if (invSum === 0) return;
    const slop = 0.008;
    const beta = 0.35;
    const corr = Math.max(penetration - slop, 0) * beta / invSum;
    a.position.addScaled(normal, -corr * invMa);
    b.position.addScaled(normal, corr * invMb);
    if (invMa > 0) { a.obj.markDirty(); updateAABB(a); }
    if (invMb > 0) { b.obj.markDirty(); updateAABB(b); }
  }

  // ---------------- Raycast ----------------

  raycast(origin: Vec3, dir: Vec3, maxDist = Infinity, skipTriggers = false): RaycastHit | null {
    let best: RaycastHit | null = null;
    for (const b of this.bodies) {
      if (skipTriggers && b.collider.isTrigger) continue;
      const hit = this.raycastBody(b, origin, dir, maxDist);
      if (hit && (!best || hit.distance < best.distance)) best = hit;
    }
    return best;
  }

  private raycastBody(b: PhysBody, origin: Vec3, dir: Vec3, maxDist: number): RaycastHit | null {
    if (b.collider.shape === 'sphere' || b.collider.shape === 'capsule') {
      // esfera; cápsula: testa 3 esferas ao longo do eixo
      if (b.collider.shape === 'capsule') {
        let best: RaycastHit | null = null;
        const axis = mat3MulVec(b.rot, new Vec3(0, 1, 0));
        for (const t of [-b.capsuleHeight / 2, 0, b.capsuleHeight / 2]) {
          const center = b.position.clone().addScaled(axis, t);
          const h = raySphere(origin, dir, center, b.radius, maxDist);
          if (h && (!best || h.distance < best.distance)) best = { ...h, obj: b.obj };
        }
        return best;
      }
      const h = raySphere(origin, dir, b.position, b.radius, maxDist);
      return h ? { ...h, obj: b.obj } : null;
    }
    // OBB: slab test em espaço local
    const inv = transpose3(b.rot);
    const rel = origin.clone().sub(b.position);
    const lo = mat3MulVec(inv, rel);
    const ld = mat3MulVec(inv, dir);
    const h = b.collider.size;
    let tmin = 0, tmax = maxDist;
    let normalLocal = new Vec3(0, 1, 0);
    const axes = ['x', 'y', 'z'] as const;
    for (let i = 0; i < 3; i++) {
      const o = axes[i] === 'x' ? lo.x : axes[i] === 'y' ? lo.y : lo.z;
      const d = axes[i] === 'x' ? ld.x : axes[i] === 'y' ? ld.y : ld.z;
      const hh = axes[i] === 'x' ? h.x : axes[i] === 'y' ? h.y : h.z;
      if (Math.abs(d) < 1e-9) {
        if (Math.abs(o) > hh) return null;
        continue;
      }
      let t1 = (-hh - o) / d;
      let t2 = (hh - o) / d;
      let sign = -Math.sign(d);
      if (t1 > t2) { const tmp = t1; t1 = t2; t2 = tmp; sign = -sign; }
      if (t1 > tmin) {
        tmin = t1;
        normalLocal = new Vec3(
          i === 0 ? sign : 0,
          i === 1 ? sign : 0,
          i === 2 ? sign : 0
        );
      }
      tmax = Math.min(tmax, t2);
      if (tmin > tmax) return null;
    }
    if (tmin <= 0 || tmin > maxDist) return null;
    const normal = mat3MulVec(b.rot, normalLocal);
    const point = origin.clone().addScaled(dir, tmin);
    return { obj: b.obj, point, normal, distance: tmin };
  }
}

// ---------------- Helpers ----------------

function scene_updateWorld(obj: GOniObject): void {
  // garante matriz de mundo atualizada (caminho de pais)
  const chain: GOniObject[] = [];
  let p: GOniObject | null = obj;
  while (p) { chain.unshift(p); p = p.parent; }
  let parentMat: Mat4 | null = null;
  for (const o of chain) {
    const local = o.transform.toMatrix();
    if (parentMat) o.worldMatrix.copy(parentMat).multiply(local);
    else o.worldMatrix.copy(local);
    o.worldDirty = false;
    parentMat = o.worldMatrix;
  }
}

function applyQuatToTransform(obj: GOniObject, q: Quat): void {
  // Quat → Euler YXZ
  const { x: qx, y: qy, z: qz, w: qw } = q;
  // yaw (Y)
  const sinY = 2 * (qw * qy - qz * qx);
  const cosY = 1 - 2 * (qy * qy + qz * qz);
  const yaw = Math.atan2(sinY, cosY);
  // pitch (X)
  const sinP = 2 * (qw * qx + qy * qz);
  const pitch = Math.abs(sinP) >= 1 ? Math.sign(sinP) * Math.PI / 2 : Math.asin(sinP);
  // roll (Z)
  const sinR = 2 * (qw * qz + qx * qy);
  const cosR = 1 - 2 * (qz * qz + qx * qx);
  const roll = Math.atan2(sinR, cosR);
  obj.transform.rotation.set(pitch, yaw, roll);
}

function applyImpulse(b: PhysBody, impulse: Vec3, r: Vec3): void {
  if (b.rigid.type !== 'dynamic' || b.rigid.sleeping) return;
  wake(b);
  b.velocity.addScaled(impulse, b.invMass);
  if (!b.rigid.freezeRotation) {
    const torqueImpulse = r.crossVec(impulse);
    b.angularVelocity.add(mat3MulVec(b.invInertia, torqueImpulse));
  }
}

function wake(b: PhysBody): void {
  if (b.rigid.sleeping) {
    b.rigid.sleeping = false;
    b.sleepTimer = 0;
  }
}

function updateAABB(b: PhysBody): void {
  let r: number;
  if (b.collider.shape === 'box') {
    const h = b.collider.size;
    r = Math.sqrt(h.x * h.x + h.y * h.y + h.z * h.z);
  } else if (b.collider.shape === 'capsule') {
    r = b.capsuleHeight / 2 + b.radius;
  } else {
    r = b.radius;
  }
  b.aabbMin.set(b.position.x - r, b.position.y - r, b.position.z - r);
  b.aabbMax.set(b.position.x + r, b.position.y + r, b.position.z + r);
}

function transpose3(m: Float32Array): Float32Array {
  const t = new Float32Array(9);
  for (let c = 0; c < 3; c++) for (let r = 0; r < 3; r++) t[r * 3 + c] = m[c * 3 + r];
  return t;
}

function swapContact(c: Contact): void {
  const tmp = c.a;
  c.a = c.b;
  c.b = tmp;
}

function extentAlong(h: Vec3, axes: Vec3[], axis: Vec3): number {
  return Math.abs(h.x * axes[0].dot(axis)) + Math.abs(h.y * axes[1].dot(axis)) + Math.abs(h.z * axes[2].dot(axis));
}

function raySphere(origin: Vec3, dir: Vec3, center: Vec3, radius: number, maxDist: number): { point: Vec3; normal: Vec3; distance: number } | null {
  const oc = origin.clone().sub(center);
  const b = oc.dot(dir);
  const c = oc.lenSq() - radius * radius;
  if (c > 0 && b > 0) return null;
  const disc = b * b - c;
  if (disc < 0) return null;
  let t = -b - Math.sqrt(disc);
  if (t < 0) t = 0;
  if (t > maxDist) return null;
  const point = origin.clone().addScaled(dir, t);
  const normal = point.clone().sub(center).norm();
  return { point, normal, distance: t };
}

/** Ponto mais próximo entre dois segmentos (para cápsula-cápsula). */
function closestSegmentSegment(p1: Vec3, q1: Vec3, p2: Vec3, q2: Vec3): { t: number; u: number } {
  const d1 = q1.clone().sub(p1);
  const d2 = q2.clone().sub(p2);
  const r = p1.clone().sub(p2);
  const a = d1.lenSq();
  const e = d2.lenSq();
  const f = d2.dot(r);
  let s = 0, u = 0;
  if (a <= 1e-10 && e <= 1e-10) return { t: 0, u: 0 };
  if (a <= 1e-10) {
    u = Math.max(0, Math.min(1, f / e));
  } else {
    const c = d1.dot(r);
    if (e <= 1e-10) {
      s = Math.max(0, Math.min(1, -c / a));
    } else {
      const b = d1.dot(d2);
      const denom = a * e - b * b;
      if (denom > 1e-10) {
        s = Math.max(0, Math.min(1, (b * f - c * e) / denom));
      } else {
        s = 0;
      }
      const t2 = (b * s + f) / e;
      if (t2 < 0) {
        u = 0;
        s = Math.max(0, Math.min(1, -c / a));
      } else if (t2 > 1) {
        u = 1;
        s = Math.max(0, Math.min(1, (b - c) / a));
      } else {
        u = t2;
      }
    }
  }
  return { t: s, u };
}
