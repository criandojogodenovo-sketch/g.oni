/**
 * G.oni Llumni — Runtime Player (standalone)
 * Carrega um arquivo .g.oni e roda o jogo: física, scripts, animação.
 * Entrada: arquivo local, drag&drop ou ?src=<url>.
 */

import { Engine } from '@core/goni/Engine';
import { Camera } from '@core/render/Camera';
import { GOniFormat } from '@core/goni/Format';
import { Vec3 } from '@core/math';

async function boot(): Promise<void> {
  const canvas = document.getElementById('gl') as HTMLCanvasElement;
  const hud = document.getElementById('hud') as HTMLElement;
  const drop = document.getElementById('drop') as HTMLElement;
  const pick = document.getElementById('pick') as HTMLButtonElement;
  const joy = document.getElementById('joy') as HTMLElement;
  const knob = document.getElementById('knob') as HTMLElement;
  const jumpBtn = document.getElementById('jump') as HTMLButtonElement;

  const engine = new Engine({
    onLog: (msg) => { hud.textContent = msg.length > 80 ? msg.slice(0, 80) : msg; },
    onScriptError: (name, err) => console.error(`[${name}]`, err),
  });

  try {
    engine.init(canvas);
  } catch (err) {
    drop.innerHTML = `<div><h1>WebGL2 indisponível</h1><p>${(err as Error).message}</p></div>`;
    return;
  }

  const gameCamera = new Camera();
  engine.activeCamera = gameCamera;

  // ---- carregamento ----
  async function loadFromBuffer(buf: ArrayBuffer): Promise<void> {
    const project = await GOniFormat.importBuffer(buf);
    engine.loadProject(project);
    engine.play();
    drop.style.display = 'none';
    hud.textContent = `${project.name} · rodando`;
  }

  function pickFile(): void {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.oni,.g.oni,.json,.gz';
    input.addEventListener('change', async () => {
      const f = input.files?.[0];
      if (f) await loadFromBuffer(await f.arrayBuffer());
    });
    input.click();
  }
  pick.addEventListener('click', pickFile);

  // drag & drop
  window.addEventListener('dragover', (e) => e.preventDefault());
  window.addEventListener('drop', async (e) => {
    e.preventDefault();
    const f = e.dataTransfer?.files?.[0];
    if (f) await loadFromBuffer(await f.arrayBuffer());
  });

  // ?src=URL
  const src = new URLSearchParams(location.search).get('src');
  if (src) {
    try {
      const resp = await fetch(src);
      await loadFromBuffer(await resp.arrayBuffer());
    } catch (err) {
      hud.textContent = `falha ao carregar ${src}`;
      console.error(err);
    }
  }

  // ---- entrada: joystick + pulo + teclado ----
  let joyId: number | null = null;
  const center = { x: 0, y: 0 };
  joy.addEventListener('pointerdown', (e) => {
    joy.setPointerCapture(e.pointerId);
    joyId = e.pointerId;
    const r = joy.getBoundingClientRect();
    center.x = r.left + r.width / 2;
    center.y = r.top + r.height / 2;
  });
  joy.addEventListener('pointermove', (e) => {
    if (joyId !== e.pointerId) return;
    const dx = e.clientX - center.x, dy = e.clientY - center.y;
    const len = Math.hypot(dx, dy) || 1;
    const cl = Math.min(len, 42);
    const nx = dx / len, ny = dy / len;
    knob.style.transform = `translate(calc(-50% + ${nx * cl}px), calc(-50% + ${ny * cl}px))`;
    engine.input.setAxis('move_x', (nx * cl) / 42);
    engine.input.setAxis('move_y', -(ny * cl) / 42);
  });
  const joyEnd = (e: PointerEvent) => {
    if (joyId !== e.pointerId) return;
    joyId = null;
    knob.style.transform = 'translate(-50%, -50%)';
    engine.input.setAxis('move_x', 0);
    engine.input.setAxis('move_y', 0);
  };
  joy.addEventListener('pointerup', joyEnd);
  joy.addEventListener('pointercancel', joyEnd);

  jumpBtn.addEventListener('pointerdown', (e) => { e.preventDefault(); engine.input.setPressed('jump', true); });
  jumpBtn.addEventListener('pointerup', () => engine.input.setPressed('jump', false));
  jumpBtn.addEventListener('pointercancel', () => engine.input.setPressed('jump', false));

  window.addEventListener('keydown', (e) => {
    const k = e.key.toLowerCase();
    if (k === 'w' || k === 'arrowup') engine.input.setPressed('up', true);
    if (k === 's' || k === 'arrowdown') engine.input.setPressed('down', true);
    if (k === 'a' || k === 'arrowleft') engine.input.setPressed('left', true);
    if (k === 'd' || k === 'arrowright') engine.input.setPressed('right', true);
    if (k === ' ') engine.input.setPressed('jump', true);
  });
  window.addEventListener('keyup', (e) => {
    const k = e.key.toLowerCase();
    if (k === 'w' || k === 'arrowup') engine.input.setPressed('up', false);
    if (k === 's' || k === 'arrowdown') engine.input.setPressed('down', false);
    if (k === 'a' || k === 'arrowleft') engine.input.setPressed('left', false);
    if (k === 'd' || k === 'arrowright') engine.input.setPressed('right', false);
    if (k === ' ') engine.input.setPressed('jump', false);
  });

  // toque na tela → on_input
  canvas.addEventListener('pointerdown', (e) => {
    const r = canvas.getBoundingClientRect();
    engine.input_event('down', e.clientX - r.left, e.clientY - r.top);
  });

  // ---- loop ----
  let last = performance.now();
  function frame(): void {
    requestAnimationFrame(frame);
    const now = performance.now();
    const dt = Math.min((now - last) / 1000, 0.1);
    last = now;

    const w = Math.max(1, canvas.clientWidth);
    const h = Math.max(1, canvas.clientHeight);
    gameCamera.setAspect(w, h);
    engine.update(dt);

    const cam = engine.playing && engine.activeCamera ? engine.activeCamera : gameCamera;
    engine.renderFrame(cam);

    // stats periódicos
    if (Math.floor(now / 1000) % 2 === 0) {
      hud.textContent = `${engine.time.elapsed.toFixed(0)}s · ${engine.physics.stats.contacts} contatos`;
    }
  }
  requestAnimationFrame(frame);
  void Vec3;
}

void boot();
