/**
 * G.oni Llumni — Juntas físicas (v1: baseadas em forças/molas, estáveis com damping)
 *
 * Tipos: fixed (solda), spring (mola), hinge (livre em torno do eixo),
 * slider (constrange ao eixo).
 *
 * Nota de implementação: hinge/slider são aproximações por molas direcionais
 * nesta versão — documentadas em docs/ARCHITECTURE.md. A API é estável;
 * a implementação pode evoluir para resolutores de restrição sem mudar chamadas.
 */

import { Vec3 } from '../math';
import { PhysicsWorld, PhysBody } from './PhysicsWorld';

export type JointType = 'fixed' | 'spring' | 'hinge' | 'slider';

export class Joint {
  type: JointType = 'spring';
  bodyA: PhysBody;
  bodyB: PhysBody;

  /** ancoragens locais (em relação ao centro de cada corpo) */
  anchorA = new Vec3();
  anchorB = new Vec3();

  /** eixo no espaço local de A (hinge/slider) */
  axis = new Vec3(0, 1, 0);

  // mola
  restLength = 0;
  stiffness = 60;
  damping = 6;

  // slider/hinge: rigidez da correção perpendicular
  correctionStrength = 120;

  broken = false;
  breakForce = 0; // 0 = inquebrável

  constructor(a: PhysBody, b: PhysBody) {
    this.bodyA = a;
    this.bodyB = b;
    this.restLength = b.position.distanceTo(a.position);
  }

  serialize(): Record<string, unknown> {
    return {
      type: this.type,
      objA: this.bodyA.obj.id,
      objB: this.bodyB.obj.id,
      anchorA: this.anchorA.toArray(),
      anchorB: this.anchorB.toArray(),
      axis: this.axis.toArray(),
      restLength: this.restLength,
      stiffness: this.stiffness,
      damping: this.damping,
    };
  }
}

export class JointSystem {
  joints: Joint[] = [];

  add(j: Joint): Joint {
    this.joints.push(j);
    return j;
  }

  create(a: PhysBody, b: PhysBody, type: JointType): Joint {
    return this.add(new Joint(a, b));
  }

  removeJointsOf(objId: string): void {
    this.joints = this.joints.filter((j) => j.bodyA.obj.id !== objId && j.bodyB.obj.id !== objId);
  }

  /** Aplica forças das juntas (chamado antes de PhysicsWorld.update). */
  applyForces(dt: number): void {
    for (const j of this.joints) {
      if (j.broken) continue;
      const { bodyA: a, bodyB: b } = j;
      if (a.invMass === 0 && b.invMass === 0) continue;

      const worldA = a.position.clone().add(anchorWorld(a, j.anchorA));
      const worldB = b.position.clone().add(anchorWorld(b, j.anchorB));
      const delta = worldB.clone().sub(worldA);
      const dist = Math.max(delta.len(), 1e-6);
      const n = delta.mul(1 / dist);

      switch (j.type) {
        case 'spring': {
          // F = -k(dist-rest) - c * vRel
          const ext = dist - j.restLength;
          const vRel = relativeVelocityAlong(a, b, worldA, worldB, n);
          const f = j.stiffness * ext + j.damping * vRel;
          const force = n.clone().mul(f);
          a.force.add(force);
          b.force.sub(force);
          checkBreak(j, Math.abs(f));
          break;
        }
        case 'fixed': {
          // mola forte + correção de velocidade
          const vRel = relativeVelocityAlong(a, b, worldA, worldB, n);
          const f = j.stiffness * (dist - j.restLength) * 8 + j.damping * vRel * 10;
          const force = n.clone().mul(f);
          a.force.add(force);
          b.force.sub(force);
          // também puxa perpendicular para alinhar âncoras
          correctPerpendicular(j, a, b, worldA, worldB, 120);
          checkBreak(j, Math.abs(f));
          break;
        }
        case 'hinge': {
          // mantém âncoras coincidentes, rotação livre em torno do eixo
          correctPerpendicular(j, a, b, worldA, worldB, j.correctionStrength);
          const vRel = relativeVelocityAlong(a, b, worldA, worldB, n);
          const f = j.stiffness * (dist - 0) + j.damping * vRel;
          const force = n.clone().mul(f);
          a.force.add(force);
          b.force.sub(force);
          break;
        }
        case 'slider': {
          // só corrige desvio perpendicular ao eixo
          correctPerpendicular(j, a, b, worldA, worldB, j.correctionStrength);
          break;
        }
      }
      void dt;
    }
  }
}

function anchorWorld(b: PhysBody, local: Vec3): Vec3 {
  // rotação aplicada à âncora local
  const m = b.rot;
  return new Vec3(
    m[0] * local.x + m[3] * local.y + m[6] * local.z,
    m[1] * local.x + m[4] * local.y + m[7] * local.z,
    m[2] * local.x + m[5] * local.y + m[8] * local.z
  );
}

function relativeVelocityAlong(a: PhysBody, b: PhysBody, pa: Vec3, pb: Vec3, n: Vec3): number {
  const ra = pa.clone().sub(a.position);
  const rb = pb.clone().sub(b.position);
  const va = a.velocity.clone().add(a.angularVelocity.crossVec(ra));
  const vb = b.velocity.clone().add(b.angularVelocity.crossVec(rb));
  return vb.clone().sub(va).dot(n);
}

function correctPerpendicular(j: Joint, a: PhysBody, b: PhysBody, worldA: Vec3, worldB: Vec3, strength: number): void {
  // componente do desvio perpendicular ao eixo da junta
  const delta = worldB.clone().sub(worldA);
  const axisWorld = anchorWorld(a, j.axis).norm();
  const along = axisWorld.clone().mul(delta.dot(axisWorld));
  const perp = delta.clone().sub(along);
  const force = perp.mul(strength * (a.invMass + b.invMass > 0 ? 1 : 0));
  a.force.add(force);
  b.force.sub(force);
  void j;
}

function checkBreak(j: Joint, force: number): void {
  if (j.breakForce > 0 && force > j.breakForce) j.broken = true;
}
