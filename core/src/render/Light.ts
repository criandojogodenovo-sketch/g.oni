/**
 * G.oni Llumni — Luzes
 * Direcional (com sombras), pontual e spot.
 */

import { Color, Vec3 } from '../math';

export type LightType = 'directional' | 'point' | 'spot';

export class Light {
  type: LightType = 'point';

  color = new Color(1, 1, 1, 1);
  intensity = 1.0;

  /** Alcance de atenuação (point/spot). */
  range = 10.0;

  /** Ângulo do cone (spot), em radianos. */
  angle = Math.PI / 6;

  /** Direção no mundo (directional/spot) — aponta DA luz PARA a cena. */
  direction = new Vec3(0, -1, 0);

  /** Sombras (apenas direcional nesta versão). */
  castShadows = true;
  shadowStrength = 0.75;

  /** posição no mundo — preenchida pelo Engine a partir do objeto. */
  worldPos = new Vec3(0, 5, 0);

  serialize(): Record<string, unknown> {
    return {
      type: this.type,
      color: this.color.toArray(),
      intensity: this.intensity,
      range: this.range,
      angle: this.angle,
      castShadows: this.castShadows,
      shadowStrength: this.shadowStrength,
    };
  }

  static deserialize(d: Record<string, unknown>): Light {
    const l = new Light();
    l.type = (d.type as LightType) ?? 'point';
    const c = (d.color as number[]) ?? [1, 1, 1, 1];
    l.color = new Color(c[0], c[1], c[2], c[3] ?? 1);
    l.intensity = Number(d.intensity ?? 1);
    l.range = Number(d.range ?? 10);
    l.angle = Number(d.angle ?? Math.PI / 6);
    l.castShadows = Boolean(d.castShadows ?? true);
    l.shadowStrength = Number(d.shadowStrength ?? 0.75);
    return l;
  }
}

/** Luzes ativas coletadas por frame (limites do shader). */
export class LightBatch {
  dir: Light | null = null;
  points: Light[] = [];
  spots: Light[] = [];

  static readonly MAX_POINT = 4;
  static readonly MAX_SPOT = 2;

  clear(): void {
    this.dir = null;
    this.points.length = 0;
    this.spots.length = 0;
  }

  add(l: Light): void {
    if (l.type === 'directional') {
      if (!this.dir || l.intensity >= this.dir.intensity) this.dir = l;
    } else if (l.type === 'point') {
      if (this.points.length < LightBatch.MAX_POINT) this.points.push(l);
    } else if (l.type === 'spot') {
      if (this.spots.length < LightBatch.MAX_SPOT) this.spots.push(l);
    }
  }
}
