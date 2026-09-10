/**
 * G.oni Llumni — Física: colisores e corpos rígidos
 *
 * Corpos: dinâmicos, estáticos, cinemáticos.
 * Colisores: box (OBB), sphere, capsule.
 *
 * Nota v1: a física opera no espaço de MUNDO e escreve o resultado no
 * transform LOCAL de objetos-raiz. Objetos com pai recebem o valor de mundo
 * no transform local (aproximação documentada — adequada para personagens,
 * plataformas e projéteis, que são raiz na prática).
 */

import { Vec3 } from '../math';

export type BodyType = 'dynamic' | 'static' | 'kinematic';
export type ColliderShape = 'box' | 'sphere' | 'capsule';

export class Collider {
  shape: ColliderShape = 'box';

  /** Meia-extensão (box). */
  size = new Vec3(0.5, 0.5, 0.5);
  /** Raio (sphere/capsule). */
  radius = 0.5;
  /** Altura da parte cilíndrica (capsule). */
  height = 1.0;

  /** Deslocamento local em relação ao objeto. */
  offset = new Vec3(0, 0, 0);

  /** Colisor sensor: gera eventos mas não resolve colisão. */
  isTrigger = false;

  serialize(): Record<string, unknown> {
    return {
      shape: this.shape,
      size: this.size.toArray(),
      radius: this.radius,
      height: this.height,
      offset: this.offset.toArray(),
      isTrigger: this.isTrigger,
    };
  }

  static deserialize(d: Record<string, unknown>): Collider {
    const c = new Collider();
    c.shape = (d.shape as ColliderShape) ?? 'box';
    if (d.size) c.size.fromArray(d.size as number[]);
    c.radius = Number(d.radius ?? 0.5);
    c.height = Number(d.height ?? 1);
    if (d.offset) c.offset.fromArray(d.offset as number[]);
    c.isTrigger = Boolean(d.isTrigger ?? false);
    return c;
  }
}

export class RigidBody {
  type: BodyType = 'dynamic';

  mass = 1.0;
  linearVelocity = new Vec3();
  angularVelocity = new Vec3();

  linearDamping = 0.02;
  angularDamping = 0.08;

  restitution = 0.25;
  friction = 0.6;

  useGravity = true;
  gravityScale = 1.0;

  /** congela rotação (personagens, itens 2.5D). */
  freezeRotation = false;

  canSleep = true;
  sleeping = false;

  serialize(): Record<string, unknown> {
    return {
      type: this.type,
      mass: this.mass,
      linearDamping: this.linearDamping,
      angularDamping: this.angularDamping,
      restitution: this.restitution,
      friction: this.friction,
      useGravity: this.useGravity,
      gravityScale: this.gravityScale,
      freezeRotation: this.freezeRotation,
      canSleep: this.canSleep,
    };
  }

  static deserialize(d: Record<string, unknown>): RigidBody {
    const b = new RigidBody();
    b.type = (d.type as BodyType) ?? 'dynamic';
    b.mass = Math.max(0, Number(d.mass ?? 1));
    b.linearDamping = Number(d.linearDamping ?? 0.02);
    b.angularDamping = Number(d.angularDamping ?? 0.08);
    b.restitution = Number(d.restitution ?? 0.25);
    b.friction = Number(d.friction ?? 0.6);
    b.useGravity = Boolean(d.useGravity ?? true);
    b.gravityScale = Number(d.gravityScale ?? 1);
    b.freezeRotation = Boolean(d.freezeRotation ?? false);
    b.canSleep = Boolean(d.canSleep ?? true);
    return b;
  }
}
