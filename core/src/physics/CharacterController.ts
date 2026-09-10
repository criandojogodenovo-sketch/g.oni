/**
 * G.oni Llumni — Character Controller
 *
 * Movimento estilo FPS/TPS: cápsula contra o mundo, com:
 *  - move & slide (desliza em superfícies, não "gruda")
 *  - gravidade + detecção de chão (grounded)
 *  - degraus baixos (step height) são subidos automaticamente
 *
 * Uso típico em G.oni Script:
 *   func on_process(delta):
 *       var cc = self.get_controller()
 *       cc.move(Input.axis("move_x"), 0, Input.axis("move_y"), delta)
 */

import { Vec3 } from '../math';
import { PhysicsWorld, PhysBody } from './PhysicsWorld';

export class CharacterController {
  body: PhysBody;
  world: PhysicsWorld;

  /** velocidade de movimento (m/s) */
  speed = 5;
  /** força do pulo (impulso vertical m/s) */
  jumpSpeed = 5.5;
  stepHeight = 0.35;
  slopeLimit = 50; // graus

  grounded = false;
  /** normal do chão sob o personagem */
  groundNormal = new Vec3(0, 1, 0);
  velocity = new Vec3();

  private gravity = -9.81;

  constructor(body: PhysBody, world: PhysicsWorld) {
    this.body = body;
    this.world = world;
    this.gravity = world.gravity.y;
    // controller cuida da gravidade: desliga a do rigidbody
    body.rigid.useGravity = false;
    body.rigid.freezeRotation = true;
    body.rigid.canSleep = false;
  }

  /**
   * Move o personagem. vx/vz em unidades de mundo por segundo,
   * o controller aplica gravidade e deslize.
   */
  move(vx: number, vz: number, dt: number, jump = false): void {
    const b = this.body;

    // gravidade
    this.velocity.y += this.gravity * dt;

    // pulo (só com chão)
    if (jump && this.grounded) {
      this.velocity.y = this.jumpSpeed;
      this.grounded = false;
    }

    const desired = new Vec3(vx, 0, vz);
    if (desired.lenSq() > 1) desired.norm();
    desired.mul(this.speed);

    // movimento horizontal suave
    b.velocity.x = desired.x;
    b.velocity.z = desired.z;
    b.velocity.y = this.velocity.y;

    // integra
    b.position.addScaled(b.velocity, dt);

    // resolve colisões (3 passadas de push-out)
    this.grounded = false;
    for (let iter = 0; iter < 3; iter++) {
      let collided = false;
      for (const other of this.world.bodies) {
        if (other === b) continue;
        if (other.collider.isTrigger) continue;
        if (other.rigid.type === 'dynamic') continue; // controller ignora dinâmicos
        const push = this.pushOut(b, other);
        if (push) collided = true;
      }
      if (!collided) break;
    }

    // ground check: raio curto para baixo
    const feet = b.position.clone();
    feet.y -= (b.capsuleHeight / 2 + b.radius) * 0.95;
    const hit = this.world.raycast(feet.clone().add(new Vec3(0, 0.1, 0)), new Vec3(0, -1, 0), 0.25);
    if (hit) {
      this.grounded = true;
      this.groundNormal.copy(hit.normal);
      if (this.velocity.y < 0) this.velocity.y = 0;
      b.velocity.y = Math.max(b.velocity.y, 0);
    }
  }

  private pushOut(b: PhysBody, other: PhysBody): boolean {
    // teste cápsula(aprox esfera) vs shape do outro
    const capRadius = b.radius + b.capsuleHeight * 0.5;
    let normal: Vec3 | null = null;
    let penetration = 0;

    if (other.collider.shape === 'sphere') {
      const d = b.position.clone().sub(other.position);
      const dist = d.len();
      const rr = capRadius + other.radius;
      if (dist < rr && dist > 1e-6) {
        normal = d.mul(1 / dist);
        penetration = rr - dist;
      }
    } else {
      // box (OBB): ponto mais próximo
      const inv = transposeRot(other.rot);
      const rel = b.position.clone().sub(other.position);
      const local = mulMat3(inv, rel);
      const h = other.collider.shape === 'box' ? other.collider.size : new Vec3(other.radius);
      const clamped = new Vec3(
        Math.max(-h.x, Math.min(h.x, local.x)),
        Math.max(-h.y, Math.min(h.y, local.y)),
        Math.max(-h.z, Math.min(h.z, local.z))
      );
      const delta = local.clone().sub(clamped);
      const dist = delta.len();
      if (dist < capRadius) {
        const nLocal = dist > 1e-6 ? delta.mul(1 / dist) : new Vec3(0, 1, 0);
        normal = mulMat3(other.rot, nLocal);
        penetration = capRadius - dist;
      }
    }

    if (!normal) return false;

    // degrau baixo: sobe em vez de bloquear
    if (normal.y > 0.5 && penetration <= this.stepHeight) {
      this.body.position.y += penetration * 1.05;
      return true;
    }
    // rampa íngreme demais: trava
    this.body.position.addScaled(normal, penetration * 1.05);
    // cancela velocidade na direção da colisão
    const vn = this.body.velocity.dot(normal);
    if (vn < 0) {
      this.body.velocity.sub(normal.clone().mul(vn));
      if (normal.y > 0.55) {
        this.grounded = true;
        this.groundNormal.copy(normal);
        if (this.velocity.y < 0) this.velocity.y = 0;
      }
    }
    return true;
  }

  /** teleporte seguro */
  teleport(x: number, y: number, z: number): void {
    this.body.position.set(x, y, z);
    this.velocity.set(0, 0, 0);
    this.body.velocity.set(0, 0, 0);
  }
}

function transposeRot(m: Float32Array): Float32Array {
  const t = new Float32Array(9);
  for (let c = 0; c < 3; c++) for (let r = 0; r < 3; r++) t[r * 3 + c] = m[c * 3 + r];
  return t;
}

function mulMat3(m: Float32Array, v: Vec3): Vec3 {
  return new Vec3(
    m[0] * v.x + m[3] * v.y + m[6] * v.z,
    m[1] * v.x + m[4] * v.y + m[7] * v.z,
    m[2] * v.x + m[5] * v.y + m[8] * v.z
  );
}
