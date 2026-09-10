/**
 * G.oni Llumni — Helpers de DOM + ícones SVG inline
 * (sem dependências externas — mobile-first, leve)
 */

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Record<string, string | number | boolean | ((e: Event) => void)> = {},
  ...children: (Node | string | null | undefined)[]
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k.startsWith('on') && typeof v === 'function') {
      node.addEventListener(k.slice(2), v as EventListener);
    } else if (typeof v === 'boolean') {
      if (v) node.setAttribute(k, '');
    } else {
      node.setAttribute(k, String(v));
    }
  }
  for (const c of children) {
    if (c === null || c === undefined) continue;
    node.append(c instanceof Node ? c : document.createTextNode(c));
  }
  return node;
}

export function qs<T extends Element>(sel: string, parent: ParentNode = document): T {
  return parent.querySelector(sel) as T;
}

export function clear(node: HTMLElement): void {
  while (node.firstChild) node.removeChild(node.firstChild);
}

/** Toast flutuante. */
export function toast(msg: string, kind: 'info' | 'err' = 'info'): void {
  const zone = document.querySelector('.toast-zone') as HTMLElement | null;
  if (!zone) { console.log('[toast]', msg); return; }
  const t = el('div', { class: kind === 'err' ? 'toast err' : 'toast' }, msg);
  zone.append(t);
  setTimeout(() => t.remove(), 2600);
}

/** Modal de confirmação (promise). */
export function confirmModal(title: string, message: string, okLabel = 'Confirmar'): Promise<boolean> {
  return new Promise((resolve) => {
    const backdrop = el('div', { class: 'modal-backdrop' });
    const modal = el('div', { class: 'modal' },
      el('h2', {}, title),
      el('p', { style: 'margin:0;color:var(--text-2);line-height:1.5' }, message),
      el('div', { class: 'row' },
        el('button', { class: 'ghost', onclick: () => { backdrop.remove(); resolve(false); } }, 'Cancelar'),
        el('button', { class: 'primary', onclick: () => { backdrop.remove(); resolve(true); } }, okLabel)
      )
    );
    backdrop.append(modal);
    document.body.append(backdrop);
  });
}

/** Modal de prompt (input). */
export function promptModal(title: string, placeholder = '', initial = ''): Promise<string | null> {
  return new Promise((resolve) => {
    const input = el('input', { type: 'text', placeholder: placeholder, value: initial });
    const backdrop = el('div', { class: 'modal-backdrop' });
    const modal = el('div', { class: 'modal' },
      el('h2', {}, title),
      input,
      el('div', { class: 'row' },
        el('button', { class: 'ghost', onclick: () => { backdrop.remove(); resolve(null); } }, 'Cancelar'),
        el('button', { class: 'primary', onclick: () => { backdrop.remove(); resolve(input.value.trim() || null); } }, 'OK')
      )
    );
    backdrop.append(modal);
    document.body.append(backdrop);
    input.addEventListener('keydown', (e) => {
      if ((e as KeyboardEvent).key === 'Enter') {
        backdrop.remove();
        resolve(input.value.trim() || null);
      }
    });
    setTimeout(() => input.focus(), 60);
  });
}

// ---------------- Ícones ----------------

const svg = (paths: string, viewBox = '0 0 24 24'): SVGSVGElement => {
  const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  s.setAttribute('viewBox', viewBox);
  s.setAttribute('fill', 'none');
  s.setAttribute('stroke', 'currentColor');
  s.setAttribute('stroke-width', '1.8');
  s.setAttribute('stroke-linecap', 'round');
  s.setAttribute('stroke-linejoin', 'round');
  s.innerHTML = paths;
  return s;
};

export const Icons = {
  back: () => svg('<path d="M15 18l-6-6 6-6"/>'),
  play: () => svg('<path d="M6 4l14 8-14 8z" fill="currentColor" stroke="none"/>'),
  stop: () => svg('<rect x="6" y="6" width="12" height="12" rx="2" fill="currentColor" stroke="none"/>'),
  save: () => svg('<path d="M19 21H5a2 2 0 01-2-2V5a2 2 0 012-2h11l5 5v11a2 2 0 01-2 2z"/><path d="M17 21v-8H7v8M7 3v5h8"/>'),
  plus: () => svg('<path d="M12 5v14M5 12h14"/>'),
  cursor: () => svg('<path d="M3 3l7.5 18 2.6-7.4L21 11z"/>'),
  move: () => svg('<path d="M12 2v20M2 12h20M12 2l-3 3M12 2l3 3M12 22l-3-3M12 22l3-3M2 12l3-3M2 12l3 3M22 12l-3-3M22 12l-3 3"/>'),
  rotate: () => svg('<path d="M21 12a9 9 0 11-3-6.7"/><path d="M21 3v6h-6"/>'),
  scale: () => svg('<path d="M3 21L21 3M3 21v-6M3 21h6M21 3v6M21 3h-6"/>'),
  hierarchy: () => svg('<rect x="9" y="2" width="6" height="6" rx="1"/><rect x="2" y="16" width="6" height="6" rx="1"/><rect x="16" y="16" width="6" height="6" rx="1"/><path d="M12 8v4M5 16v-2h14v2"/>'),
  inspector: () => svg('<circle cx="12" cy="12" r="3"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/>'),
  script: () => svg('<path d="M8 4l-5 8 5 8M16 4l5 8-5 8"/>'),
  visual: () => svg('<circle cx="5" cy="6" r="2.5"/><circle cx="19" cy="6" r="2.5"/><circle cx="12" cy="18" r="2.5"/><path d="M7.5 6h9M8 17l8-9M16 17L8 8"/>'),
  cube: () => svg('<path d="M12 2l9 5v10l-9 5-9-5V7z"/><path d="M12 22V12M3 7l9 5 9-5"/>'),
  brush: () => svg('<path d="M18 2l4 4-13 13H5v-4z"/><path d="M13.5 6.5l4 4"/>'),
  clock: () => svg('<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 3"/>'),
  assets: () => svg('<path d="M3 7a2 2 0 012-2h4l2 2h8a2 2 0 012 2v9a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>'),
  eye: () => svg('<path d="M1 12s4-7 11-7 11 7 11 7-4 7-11 7S1 12 1 12z"/><circle cx="12" cy="12" r="3"/>'),
  eyeOff: () => svg('<path d="M3 3l18 18M10.6 10.6a3 3 0 004.2 4.2M9.4 5.3A10.9 10.9 0 0112 5c7 0 11 7 11 7a18.5 18.5 0 01-2.2 2.9M6.1 6.6A18.7 18.7 0 001 12s4 7 11 7c1 0 2-.1 2.9-.4"/>'),
  trash: () => svg('<path d="M3 6h18M8 6V4a1 1 0 011-1h6a1 1 0 011 1v2M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6"/>'),
  duplicate: () => svg('<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a2 2 0 01-2-2V4a2 2 0 012-2h9a2 2 0 012 2v1"/>'),
  settings: () => svg('<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 00.3 1.9l.1.1a2 2 0 11-2.8 2.8l-.1-.1a1.7 1.7 0 00-1.9-.3 1.7 1.7 0 00-1 1.5V21a2 2 0 11-4 0v-.1a1.7 1.7 0 00-1-1.6 1.7 1.7 0 00-1.9.3l-.1.1a2 2 0 11-2.8-2.8l.1-.1a1.7 1.7 0 00.3-1.9 1.7 1.7 0 00-1.5-1H3a2 2 0 110-4h.1a1.7 1.7 0 001.6-1 1.7 1.7 0 00-.3-1.9l-.1-.1a2 2 0 112.8-2.8l.1.1a1.7 1.7 0 001.9.3h.1a1.7 1.7 0 001-1.5V3a2 2 0 114 0v.1a1.7 1.7 0 001 1.6 1.7 1.7 0 001.9-.3l.1-.1a2 2 0 112.8 2.8l-.1.1a1.7 1.7 0 00-.3 1.9v.1a1.7 1.7 0 001.5 1H21a2 2 0 110 4h-.1a1.7 1.7 0 00-1.5 1z"/>'),
  camera: () => svg('<path d="M23 7l-7 5 7 5V7z"/><rect x="1" y="5" width="15" height="14" rx="2"/>'),
  code: () => svg('<path d="M16 18l6-6-6-6M8 6l-6 6 6 6"/>'),
  world: () => svg('<circle cx="12" cy="12" r="10"/><path d="M2 12h20M12 2a15 15 0 010 20 15 15 0 010-20z"/>'),
  layers: () => svg('<path d="M12 2l10 5-10 5L2 7z"/><path d="M2 12l10 5 10-5M2 17l10 5 10-5"/>'),
  import: () => svg('<path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4M7 10l5 5 5-5M12 15V3"/>'),
  export: () => svg('<path d="M21 15v4a2 2 0 01-2 2H5a2 2 0 01-2-2v-4M17 8l-5-5-5 5M12 3v12"/>'),
  undo: () => svg('<path d="M3 7v6h6"/><path d="M21 17a9 9 0 00-15-6.7L3 13"/>'),
  file: () => svg('<path d="M14 2H6a2 2 0 00-2 2v16a2 2 0 002 2h12a2 2 0 002-2V8z"/><path d="M14 2v6h6"/>'),
  folder: () => svg('<path d="M3 7a2 2 0 012-2h4l2 2h8a2 2 0 012 2v9a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>'),
};

/** Botão de ícone com tooltip. */
export function iconBtn(icon: keyof typeof Icons, title: string, onclick: () => void, active = false): HTMLButtonElement {
  const b = el('button', { class: `icon-btn${active ? ' active' : ''}`, title, onclick });
  b.append(Icons[icon]());
  return b;
}

/** Linha de número arrastável (scrub) — otimizada para toque. */
export function numberField(
  value: number,
  onChange: (v: number) => void,
  opts: { step?: number; min?: number; max?: number; decimals?: number } = {}
): HTMLInputElement {
  const input = el('input', { type: 'number' }) as HTMLInputElement;
  const step = opts.step ?? 0.1;
  const decimals = opts.decimals ?? 2;
  input.value = value.toFixed(decimals);
  input.addEventListener('input', () => {
    const v = parseFloat(input.value);
    if (!isNaN(v)) onChange(v);
  });
  return input;
}
