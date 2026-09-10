/**
 * G.oni Llumni — Editor de Animação
 *
 * Timeline com keyframes das trilhas de transform (position/rotation/scale xyz),
 * interpolação (linear, ease, bezier), scrubbing e preview.
 * Os clipes ficam no AnimationComponent e tocam em play mode.
 */

import { el, toast, promptModal } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { AnimationComponent, MeshComponent } from '@core/goni/Objetos';
import { AnimationClip, InterpMode } from '@core/goni/Animation';
import { MathUtils } from '@core/math';

const TRACKS = [
  'position.x', 'position.y', 'position.z',
  'rotation.x', 'rotation.y', 'rotation.z',
  'scale.x', 'scale.y', 'scale.z',
] as const;

export class AnimationPanel {
  clip: AnimationClip | null = null;
  time = 0;
  playing = false;
  loop = true;
  private timelineCanvas: HTMLCanvasElement | null = null;
  private raf = 0;
  private body: HTMLElement | null = null;

  constructor(private app: EditorApp) {}

  render(body: HTMLElement): void {
    body.innerHTML = '';
    this.body = body;
    const sel = this.app.viewport.selected;

    if (!sel) {
      body.append(el('p', { class: 'hint' }, 'Selecione um objeto para animar.'));
      return;
    }

    // pega ou cria AnimationComponent
    let comp = sel.getComponent<AnimationComponent>('animation');
    if (!comp) {
      comp = sel.addComponent(new AnimationComponent());
    }

    // usa o clipe em edição ou cria
    if (!this.clip) {
      this.clip = comp.clips[0] ?? new AnimationClip();
      this.clip.name = 'clipe1';
      if (!comp.clips.includes(this.clip)) comp.clips.push(this.clip);
    }

    // ---------- controles ----------
    const controls = el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px' },
      el('button', {
        class: 'small primary',
        onclick: () => this.addKeyframe(comp!),
      }, '+ Keyframe'),
      el('button', {
        class: 'small',
        onclick: () => this.togglePlay(),
      }, '▶ Preview'),
      el('button', {
        class: 'small',
        onclick: () => { this.time = 0; this.applyPreview(comp!); this.drawTimeline(); },
      }, '⏮ Início'),
      el('button', {
        class: 'small',
        onclick: () => { this.loop = !this.loop; this.render(body); },
      }, this.loop ? '🔁 Loop: on' : '➡ Loop: off'),
      el('button', {
        class: 'small',
        onclick: () => void this.saveClip(comp!),
      }, '💾 Salvar'),
      el('button', {
        class: 'small danger',
        onclick: () => {
          this.clip = null;
          this.render(body);
        },
      }, 'Novo clipe'),
    );
    body.append(controls);

    // ---------- propriedades do clipe ----------
    const info = el('div', { class: 'insp-group' },
      el('h3', {}, `Clipe: ${this.clip.name} · ${this.clip.duration.toFixed(1)}s · ${this.clip.tracks.length} trilhas`),
    );
    const nameBtn = el('button', { class: 'small' }, 'Renomear');
    nameBtn.addEventListener('click', async () => {
      const name = await promptModal('Nome do clipe', '', this.clip!.name);
      if (name && this.clip) {
        this.clip.name = name;
        this.render(body);
      }
    });
    info.append(nameBtn);
    body.append(info);

    // ---------- scrubber ----------
    const scrub = el('input', {
      type: 'range', min: 0, max: Math.max(this.clip.duration, 0.1), step: 0.01, value: this.time,
    }) as HTMLInputElement;
    const scrubLabel = el('span', {
      style: 'font-family:var(--font-mono);font-size:12px;color:var(--text-2);min-width:64px;text-align:right',
    }, `${this.time.toFixed(2)}s`);
    scrub.addEventListener('input', () => {
      this.time = parseFloat(scrub.value);
      scrubLabel.textContent = `${this.time.toFixed(2)}s`;
      this.applyPreview(comp!);
      this.drawTimeline();
    });
    body.append(el('div', { style: 'display:flex;align-items:center;gap:10px;margin-bottom:10px' }, scrub, scrubLabel));

    // ---------- timeline por trilha ----------
    const timeline = el('div', { class: 'timeline' });
    const labels = el('div', {});
    const drawnTracks: string[] = [];
    for (const track of TRACKS) {
      const hasKeys = this.clip.tracks.some((t) => t.property === track);
      if (!hasKeys) continue;
      drawnTracks.push(track);
    }
    if (drawnTracks.length === 0) {
      timeline.append(el('p', { class: 'hint', style: 'margin:4px' }, 'Sem keyframes. Mova o objeto e toque em "+ Keyframe" para registrar trilhas.'));
    }

    const canvas = el('canvas') as HTMLCanvasElement;
    canvas.style.width = '100%';
    canvas.style.height = `${Math.max(60, drawnTracks.length * 56)}px`;
    this.timelineCanvas = canvas;
    timeline.append(canvas);
    body.append(timeline);

    // ---------- lista de trilhas + exclusão ----------
    const tracksGroup = el('div', { class: 'insp-group' }, el('h3', {}, 'Trilhas'));
    for (const track of this.clip.tracks) {
      tracksGroup.append(el('div', {
        style: 'display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid var(--border)',
      },
        el('span', { style: 'font-family:var(--font-mono);font-size:12px' }, track.property),
        el('span', { class: 'hint', style: 'font-size:11px' }, `${track.keys.length} keys`),
        el('button', {
          class: 'small danger',
          onclick: () => {
            this.clip!.tracks = this.clip!.tracks.filter((t) => t !== track);
            this.render(body);
          },
        }, '✕'),
      ));
    }
    // modo de interpolação para novas keys
    const interpSel = el('select') as HTMLSelectElement;
    for (const m of ['ease_in_out', 'linear', 'ease_in', 'ease_out', 'bezier', 'constant'] as InterpMode[]) {
      interpSel.append(el('option', { value: m }, m));
    }
    interpSel.value = this.interpMode;
    interpSel.addEventListener('change', () => { this.interpMode = interpSel.value as InterpMode; });
    tracksGroup.append(el('div', { class: 'insp-row' }, el('label', {}, 'Interpolação'), interpSel));
    body.append(tracksGroup);

    body.append(el('p', { class: 'hint' }, 'Dica: no play mode, use play_animation(self, "nome") para disparar o clipe via script.'));

    this.drawTimeline();
    this.startPreviewLoop(comp);
  }

  private interpMode: InterpMode = 'ease_in_out';

  private addKeyframe(comp: AnimationComponent): void {
    const sel = this.app.viewport.selected;
    if (!sel || !this.clip) return;
    const t = this.app.viewport.selected;
    void comp;
    for (const track of TRACKS) {
      const [prop, axis] = track.split('.');
      let value: number;
      if (prop === 'position') value = t!.transform.position[axis as 'x' | 'y' | 'z'];
      else if (prop === 'rotation') value = t!.transform.rotation[axis as 'x' | 'y' | 'z'];
      else value = t!.transform.scale[axis as 'x' | 'y' | 'z'];
      // ignora trilhas sem keys existentes quando o valor é "neutro"
      const hasTrack = this.clip!.tracks.some((tr) => tr.property === track);
      const neutral = (prop === 'scale' ? 1 : 0);
      if (!hasTrack && Math.abs(value - neutral) < 1e-4) continue;
      this.clip!.addKey(track, this.time, value, this.interpMode);
    }
    this.app.markDirty();
    toast(`Keyframe em ${this.time.toFixed(2)}s`);
    if (this.body) this.render(this.body);
  }

  private togglePlay(): void {
    this.playing = !this.playing;
    if (this.playing) this.time = 0;
  }

  private startPreviewLoop(comp: AnimationComponent): void {
    cancelAnimationFrame(this.raf);
    let last = performance.now();
    const step = () => {
      this.raf = requestAnimationFrame(step);
      if (!document.contains(this.timelineCanvas ?? document.body)) {
        cancelAnimationFrame(this.raf);
        return;
      }
      const now = performance.now();
      const dt = (now - last) / 1000;
      last = now;
      if (this.playing && this.clip) {
        this.time += dt;
        if (this.time > Math.max(this.clip.duration, 0.1)) {
          if (this.loop) this.time = 0;
          else { this.time = this.clip.duration; this.playing = false; }
        }
        this.applyPreview(comp);
        this.drawTimeline();
      }
    };
    this.raf = requestAnimationFrame(step);
  }

  private applyPreview(comp: AnimationComponent): void {
    const sel = this.app.viewport.selected;
    if (!sel || !this.clip) return;
    void comp;
    for (const track of this.clip.tracks) {
      const v = this.clip.sample(track.property, this.time, this.loop);
      if (v === null) continue;
      const [prop, axis] = track.property.split('.');
      const a = axis as 'x' | 'y' | 'z';
      if (prop === 'position') sel.transform.position[a] = v;
      else if (prop === 'rotation') sel.transform.rotation[a] = v;
      else if (prop === 'scale') sel.transform.scale[a] = v;
    }
    sel.markDirty();
  }

  private async saveClip(comp: AnimationComponent): Promise<void> {
    if (!this.clip) return;
    if (!comp.clips.includes(this.clip)) comp.clips.push(this.clip);
    this.clip.duration = Math.max(this.clip.duration, 0.1);
    this.app.markDirty();
    toast(`Clipe "${this.clip.name}" salvo (${this.clip.tracks.length} trilhas)`);
    void this.app;
  }

  private drawTimeline(): void {
    const c = this.timelineCanvas;
    if (!c || !this.clip) return;
    const rect = c.getBoundingClientRect();
    if (rect.width === 0) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    c.width = Math.floor(rect.width * dpr);
    c.height = Math.floor(Math.max(60, this.clip.tracks.length * 56) * dpr);
    const ctx = c.getContext('2d')!;
    ctx.scale(dpr, dpr);
    const W = rect.width;
    const H = c.height / dpr;
    ctx.clearRect(0, 0, W, H);

    const dur = Math.max(this.clip.duration, 0.1);
    const xOf = (t: number) => (t / dur) * (W - 70);

    // fundo + réguas
    ctx.fillStyle = '#0d1017';
    ctx.fillRect(0, 0, W, H);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    for (let t = 0; t <= dur; t += dur / 8) {
      const x = xOf(t);
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke();
      ctx.fillStyle = '#5d6a85';
      ctx.font = '9px ui-monospace';
      ctx.fillText(`${t.toFixed(1)}s`, x + 2, 10);
    }

    // trilhas
    this.clip.tracks.forEach((track, i) => {
      const y = 24 + i * 56;
      // label
      ctx.fillStyle = '#9aa6bd';
      ctx.font = '10px ui-monospace';
      ctx.fillText(track.property, 4, y - 4);

      // linha
      ctx.strokeStyle = '#2a3348';
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W - 70, y); ctx.stroke();

      // range de valores
      const values = track.keys.map((k) => k.value);
      const min = Math.min(...values), max = Math.max(...values);
      const span = max - min || 1;

      // segmentos
      for (let k = 0; k < track.keys.length - 1; k++) {
        const a = track.keys[k], b = track.keys[k + 1];
        const ya = y - ((a.value - min) / span) * 34 - 2;
        const yb = y - ((b.value - min) / span) * 34 - 2;
        ctx.strokeStyle = '#ffb020';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(xOf(a.t), ya);
        ctx.lineTo(xOf(b.t), yb);
        ctx.stroke();
      }

      // keys
      for (const k of track.keys) {
        const ky = y - ((k.value - min) / span) * 34 - 2;
        ctx.fillStyle = '#ffd07a';
        ctx.beginPath();
        ctx.arc(xOf(k.t), ky, 4.5, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#1a1204';
        ctx.lineWidth = 1;
        ctx.stroke();
      }
    });

    // playhead
    const px = xOf(Math.min(this.time, dur));
    ctx.strokeStyle = '#4d9fff';
    ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(px, 14); ctx.lineTo(px, H); ctx.stroke();
    ctx.fillStyle = '#4d9fff';
    ctx.beginPath();
    ctx.moveTo(px - 5, 8); ctx.lineTo(px + 5, 8); ctx.lineTo(px, 16);
    ctx.closePath();
    ctx.fill();
  }
}
