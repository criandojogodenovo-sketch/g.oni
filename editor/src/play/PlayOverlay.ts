/**
 * G.oni Llumni — Overlay de Play Mode
 * Joystick virtual (mobile), botão de pulo, teclado (desktop),
 * estatísticas em tempo real.
 */

import { el } from '@editor/ui/dom';
import { Engine } from '@core/goni/Engine';

export class PlayOverlay {
  root: HTMLElement;
  private knob: HTMLElement;
  private stats: HTMLElement;
  private engine: Engine;
  private raf = 0;

  constructor(parent: HTMLElement, engine: Engine) {
    this.engine = engine;
    this.root = el('div', { class: 'play-overlay' });

    // joystick
    const joystick = el('div', { class: 'joystick' });
    this.knob = el('div', { class: 'knob' });
    joystick.append(this.knob);

    let joyId: number | null = null;
    const joyCenter = { x: 0, y: 0 };

    joystick.addEventListener('pointerdown', (e) => {
      joystick.setPointerCapture(e.pointerId);
      joyId = e.pointerId;
      const r = joystick.getBoundingClientRect();
      joyCenter.x = r.left + r.width / 2;
      joyCenter.y = r.top + r.height / 2;
      this.updateKnob(e.clientX, e.clientY, joyCenter);
    });
    joystick.addEventListener('pointermove', (e) => {
      if (joyId === e.pointerId) this.updateKnob(e.clientX, e.clientY, joyCenter);
    });
    const joyEnd = (e: PointerEvent) => {
      if (joyId !== e.pointerId) return;
      joyId = null;
      this.knob.style.transform = 'translate(-50%, -50%)';
      engine.input.setAxis('move_x', 0);
      engine.input.setAxis('move_y', 0);
    };
    joystick.addEventListener('pointerup', joyEnd);
    joystick.addEventListener('pointercancel', joyEnd);

    // botão de pulo/ação
    const jumpBtn = el('button', { class: 'btn-jump' }, 'PULAR');
    jumpBtn.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      engine.input.setPressed('jump', true);
    });
    const jumpEnd = () => engine.input.setPressed('jump', false);
    jumpBtn.addEventListener('pointerup', jumpEnd);
    jumpBtn.addEventListener('pointercancel', jumpEnd);

    // stats
    this.stats = el('div', { class: 'play-stats' }, '…');

    // teclado (desktop)
    window.addEventListener('keydown', (e) => {
      const k = e.key.toLowerCase();
      if (k === 'arrowup' || k === 'w') engine.input.setPressed('up', true);
      if (k === 'arrowdown' || k === 's') engine.input.setPressed('down', true);
      if (k === 'arrowleft' || k === 'a') engine.input.setPressed('left', true);
      if (k === 'arrowright' || k === 'd') engine.input.setPressed('right', true);
      if (k === ' ') engine.input.setPressed('jump', true);
    });
    window.addEventListener('keyup', (e) => {
      const k = e.key.toLowerCase();
      if (k === 'arrowup' || k === 'w') engine.input.setPressed('up', false);
      if (k === 'arrowdown' || k === 's') engine.input.setPressed('down', false);
      if (k === 'arrowleft' || k === 'a') engine.input.setPressed('left', false);
      if (k === 'arrowright' || k === 'd') engine.input.setPressed('right', false);
      if (k === ' ') engine.input.setPressed('jump', false);
    });

    this.root.append(this.stats, joystick, jumpBtn);
    parent.append(this.root);
    this.root.style.display = 'none';

    void this.statsLoop();
  }

  show(v: boolean): void {
    this.root.style.display = v ? '' : 'none';
    if (!v) {
      this.engine.input.reset();
    }
  }

  private updateKnob(x: number, y: number, center: { x: number; y: number }): void {
    const dx = x - center.x;
    const dy = y - center.y;
    const len = Math.hypot(dx, dy);
    const max = 42;
    const cl = Math.min(len, max);
    const nx = len > 0 ? dx / len : 0;
    const ny = len > 0 ? dy / len : 0;
    this.knob.style.transform = `translate(calc(-50% + ${nx * cl}px), calc(-50% + ${ny * cl}px))`;
    this.engine.input.setAxis('move_x', (nx * cl) / max);
    this.engine.input.setAxis('move_y', -(ny * cl) / max);
  }

  private async statsLoop(): Promise<void> {
    const step = () => {
      this.raf = requestAnimationFrame(step);
      if (this.root.style.display === 'none') return;
      const e = this.engine;
      this.stats.textContent = `${e.time.elapsed.toFixed(1)}s · ${e.physics.stats.bodies} corpos · ${e.physics.stats.contacts} contatos · ${e.scriptInstances.length} scripts`;
    };
    this.raf = requestAnimationFrame(step);
  }

  dispose(): void {
    cancelAnimationFrame(this.raf);
    this.root.remove();
  }
}
