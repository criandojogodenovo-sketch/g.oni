/**
 * G.oni Llumni — Animação (clips, trilhas, keyframes, interpolação)
 * Usada pelo editor de animação e pelo runtime.
 */

import { MathUtils } from '../math';

export type InterpMode = 'linear' | 'ease_in' | 'ease_out' | 'ease_in_out' | 'bezier' | 'constant';

export interface Keyframe {
  t: number; // segundos
  value: number;
  interp: InterpMode;
}

export interface AnimationTrack {
  /** propriedade: "position.x", "rotation.y", "scale.z", ... */
  property: string;
  keys: Keyframe[];
}

export class AnimationClip {
  name = 'clipe';
  duration = 1;
  tracks: AnimationTrack[] = [];

  /** Amostra um valor interpolado no tempo t (com loop opcional). */
  sample(property: string, t: number, loop: boolean): number | null {
    const track = this.tracks.find((tr) => tr.property === property);
    if (!track || track.keys.length === 0) return null;
    let time = t;
    if (loop && this.duration > 0) time = ((time % this.duration) + this.duration) % this.duration;
    const keys = track.keys;
    if (time <= keys[0].t) return keys[0].value;
    if (time >= keys[keys.length - 1].t) return keys[keys.length - 1].value;
    for (let i = 0; i < keys.length - 1; i++) {
      const a = keys[i], b = keys[i + 1];
      if (time >= a.t && time <= b.t) {
        const span = b.t - a.t;
        let u = span > 1e-6 ? (time - a.t) / span : 0;
        switch (b.interp ?? 'linear') {
          case 'constant': return a.value;
          case 'ease_in': u = MathUtils.easeIn(u); break;
          case 'ease_out': u = MathUtils.easeOut(u); break;
          case 'ease_in_out':
          case 'bezier': u = MathUtils.bezier(u); break;
          default: break; // linear
        }
        return MathUtils.lerp(a.value, b.value, u);
      }
    }
    return keys[keys.length - 1].value;
  }

  addKey(property: string, t: number, value: number, interp: InterpMode = 'ease_in_out'): void {
    let track = this.tracks.find((tr) => tr.property === property);
    if (!track) {
      track = { property, keys: [] };
      this.tracks.push(track);
    }
    const existing = track.keys.find((k) => Math.abs(k.t - t) < 1e-3);
    if (existing) {
      existing.value = value;
      existing.interp = interp;
    } else {
      track.keys.push({ t, value, interp });
      track.keys.sort((a, b) => a.t - b.t);
    }
    this.duration = Math.max(this.duration, t);
  }

  removeKey(property: string, t: number): void {
    const track = this.tracks.find((tr) => tr.property === property);
    if (!track) return;
    const i = track.keys.findIndex((k) => Math.abs(k.t - t) < 1e-3);
    if (i >= 0) track.keys.splice(i, 1);
    if (track.keys.length === 0) this.tracks = this.tracks.filter((tr) => tr !== track);
  }

  serialize(): Record<string, unknown> {
    return { name: this.name, duration: this.duration, tracks: this.tracks };
  }

  static deserialize(d: Record<string, unknown>): AnimationClip {
    const c = new AnimationClip();
    c.name = String(d.name ?? 'clipe');
    c.duration = Number(d.duration ?? 1);
    c.tracks = (d.tracks as AnimationTrack[]) ?? [];
    return c;
  }
}
