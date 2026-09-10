/**
 * G.oni Visual — Editor de lógica por nós (node-based)
 *
 * Nós: eventos (on_ready/on_process), ações (translate, set_position, rotate,
 * spawn, destroy, print), condição (branch, compare), matemática (+ − × ÷),
 * variáveis (get/set), entrada (axis, pressed).
 *
 * Conexões com validação de tipo (exec + dados).
 * Exporta para código G.oni Script (requisito do spec §3.2).
 */

import { el, toast, promptModal, confirmModal } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { GOniObject, ScriptComponent } from '@core/goni/Objetos';

type PortType = 'exec' | 'number' | 'string' | 'bool' | 'any';

interface VSPort {
  id: string;
  name: string;
  type: PortType;
}

export interface VSNodeDef {
  type: string;
  label: string;
  category: 'evento' | 'ação' | 'fluxo' | 'matemática' | 'variável' | 'entrada';
  inputs: VSPort[];
  outputs: VSPort[];
  /** campos editáveis (literais) */
  fields: string[];
  /** gera código G.oni Script */
  code: (n: VSNode, inputExpr: (id: string, fallback: string) => string) => string | null;
}

export interface VSNode {
  id: string;
  type: string;
  x: number;
  y: number;
  /** valores literais definidos em campos */
  fields: Record<string, string>;
}

export interface VSEdge {
  id: string;
  from: string; // "nodeId:portId"
  to: string;
}

export interface VSGraph {
  name: string;
  nodes: VSNode[];
  edges: VSEdge[];
}

// ---------------- definições de nós ----------------

const def = (
  type: string, label: string, category: VSNodeDef['category'],
  inputs: [string, PortType][], outputs: [string, PortType][],
  code: VSNodeDef['code'],
  fields: string[] = []
): VSNodeDef => ({
  type, label, category,
  inputs: inputs.map(([name, t]) => ({ id: name, name, type: t })),
  outputs: outputs.map(([name, t]) => ({ id: name, name, type: t })),
  code,
  fields,
});

const NODE_DEFS: VSNodeDef[] = [
  def('on_ready', 'Ao Iniciar', 'evento', [], [['exec', 'exec']],
    () => null, []),
  def('on_process', 'A Cada Quadro', 'evento', [], [['exec', 'exec'], ['delta', 'number']],
    () => null),
  def('print', 'Imprimir', 'ação', [['exec', 'exec'], ['valor', 'any']], [['exec', 'exec']],
    (n, inExpr) => `    print(${inExpr('valor', '"…"')})`),
  def('translate', 'Transladar', 'ação', [['exec', 'exec'], ['x', 'number'], ['y', 'number'], ['z', 'number'], ['mult', 'number']], [['exec', 'exec']],
    (n, inExpr) => `    self.translate(${inExpr('x', '0')}, ${inExpr('y', '0')}, ${inExpr('z', '0')})`,
    ['x', 'y', 'z']),
  def('rotate_y', 'Girar Y', 'ação', [['exec', 'exec'], ['graus', 'number']], [['exec', 'exec']],
    (n, inExpr) => `    self.rotate_y(${inExpr('graus', '45')} * ${'delta'})`,
    ['graus']),
  def('set_position', 'Definir Posição', 'ação', [['exec', 'exec'], ['x', 'number'], ['y', 'number'], ['z', 'number']], [['exec', 'exec']],
    (n, inExpr) => `    self.set_position(${inExpr('x', '0')}, ${inExpr('y', '0')}, ${inExpr('z', '0')})`,
    ['x', 'y', 'z']),
  def('spawn', 'Criar (prefab)', 'ação', [['exec', 'exec'], ['prefab', 'string']], [['exec', 'exec']],
    (n, inExpr) => `    spawn(${inExpr('prefab', '"cubo"')})`,
    ['prefab']),
  def('destroy_self', 'Destruir', 'ação', [['exec', 'exec']], [['exec', 'exec']],
    () => `    destroy(self)`),
  def('wait', 'Esperar', 'ação', [['exec', 'exec'], ['segundos', 'number']], [['exec', 'exec']],
    (n, inExpr) => `    await wait(${inExpr('segundos', '1')})`,
    ['segundos']),
  def('emit_signal', 'Emitir Sinal', 'ação', [['exec', 'exec'], ['nome', 'string']], [['exec', 'exec']],
    (n, inExpr) => `    emit(${inExpr('nome', '"meu_sinal"')})`,
    ['nome']),
  def('branch', 'Se/Condição', 'fluxo', [['exec', 'exec'], ['cond', 'bool']], [['true', 'exec'], ['false', 'exec']],
    (n, inExpr) => `    if ${inExpr('cond', 'true')}:`),
  def('compare', 'Comparar', 'fluxo', [['a', 'number'], ['b', 'number']], [['result', 'bool']],
    (n, inExpr) => `(${inExpr('a', '0')} ${n.fields.op ?? '=='} ${inExpr('b', '0')})`,
    ['a', 'b', 'op']),
  def('add', 'A + B', 'matemática', [['a', 'number'], ['b', 'number']], [['result', 'number']],
    (n, inExpr) => `(${inExpr('a', '0')} + ${inExpr('b', '0')})`),
  def('sub', 'A − B', 'matemática', [['a', 'number'], ['b', 'number']], [['result', 'number']],
    (n, inExpr) => `(${inExpr('a', '0')} - ${inExpr('b', '0')})`),
  def('mul', 'A × B', 'matemática', [['a', 'number'], ['b', 'number']], [['result', 'number']],
    (n, inExpr) => `(${inExpr('a', '0')} * ${inExpr('b', '0')})`),
  def('div', 'A ÷ B', 'matemática', [['a', 'number'], ['b', 'number']], [['result', 'number']],
    (n, inExpr) => `(${inExpr('a', '0')} / ${inExpr('b', '1')})`),
  def('number', 'Número', 'matemática', [], [['value', 'number']],
    (n) => `(${n.fields.value ?? '0'})`, ['value']),
  def('var_get', 'Ler Variável', 'variável', [], [['value', 'any']],
    (n) => n.fields.name ?? 'minha_var', ['name']),
  def('var_set', 'Definir Variável', 'variável', [['exec', 'exec'], ['valor', 'any']], [['exec', 'exec']],
    (n, inExpr) => `    ${n.fields.name ?? 'minha_var'} = ${inExpr('valor', '0')}`, ['name']),
  def('input_axis', 'Eixo (joystick)', 'entrada', [], [['value', 'number']],
    (n) => `input_axis("${n.fields.axis ?? 'move_x'}")`, ['axis']),
  def('input_pressed', 'Botão?', 'entrada', [], [['value', 'bool']],
    (n) => `input_pressed("${n.fields.btn ?? 'jump'}")`, ['btn']),
  def('touch_x', 'Toque X', 'entrada', [], [['value', 'number']],
    () => `touch_x()`),
  def('touch_y', 'Toque Y', 'entrada', [], [['value', 'number']],
    () => `touch_y()`),
];

export class VisualScriptingPanel {
  graph: VSGraph = { name: 'lógica', nodes: [], edges: [] };
  private canvas: HTMLCanvasElement | null = null;
  private view = { x: 40, y: 40, zoom: 1 };
  private dragNode: { id: string; dx: number; dy: number } | null = null;
  private dragView: { x: number; y: number; px: number; py: number } | null = null;
  private linking: { from: string; x: number; y: number } | null = null;
  private selectedNode: string | null = null;

  constructor(private app: EditorApp) {}

  render(body: HTMLElement): void {
    body.innerHTML = '';

    // toolbar
    const tools = el('div', { style: 'display:flex;gap:8px;flex-wrap:wrap;margin-bottom:10px' },
      el('button', {
        class: 'small primary',
        onclick: () => void this.compileToScript(),
      }, '⇄ Exportar p/ G.oni Script'),
      el('button', {
        class: 'small',
        onclick: () => void this.clearGraph(),
      }, 'Limpar'),
      el('button', {
        class: 'small',
        onclick: () => void this.attachAsScript(),
      }, 'Anexar ao objeto'),
    );
    body.append(tools);
    body.append(el('p', {
      class: 'hint',
      style: 'margin:0 0 10px',
    }, 'Arraste nós da paleta para o canvas. Conecte saídas (direita) a entradas (esquerda). Arraste o fundo para navegar; pinça para zoom (mobile).'));

    // layout: paleta + canvas
    const wrap = el('div', { class: 'vs-wrap' });
    const canvas = el('canvas', { class: 'vs-canvas' }) as HTMLCanvasElement;
    this.canvas = canvas;
    const palette = el('div', { class: 'vs-palette' });

    const cats = ['evento', 'fluxo', 'matemática', 'variável', 'entrada', 'ação'] as const;
    for (const cat of cats) {
      const defs = NODE_DEFS.filter((d) => d.category === cat);
      if (!defs.length) continue;
      const catEl = el('div', {});
      catEl.append(el('h4', {}, cat));
      for (const d of defs) {
        catEl.append(el('button', {
          class: 'vs-node-pal',
          onclick: () => this.addNode(d.type),
        }, d.label));
      }
      palette.append(catEl);
    }
    // botão de fechar paleta
    palette.append(el('button', {
      class: 'small ghost',
      style: 'margin-top:6px',
      onclick: () => { palette.style.display = palette.style.display === 'none' ? '' : 'none'; },
    }, '▣ alternar paleta'));

    wrap.append(palette, canvas);
    body.append(wrap);

    this.bindCanvasEvents();
    requestAnimationFrame(() => this.draw());
  }

  private nodeDef(type: string): VSNodeDef | undefined {
    return NODE_DEFS.find((d) => d.type === type);
  }

  private addNode(type: string): void {
    const id = `n${Date.now().toString(36)}${Math.random().toString(36).slice(2, 5)}`;
    this.graph.nodes.push({
      id, type,
      x: this.view.x + 60 + Math.random() * 40,
      y: this.view.y + 60 + Math.random() * 40,
      fields: {},
    });
    this.draw();
    this.app.markDirty();
  }

  private nodeAt(x: number, y: number): VSNode | null {
    for (const n of [...this.graph.nodes].reverse()) {
      const d = this.nodeDef(n.type);
      if (!d) continue;
      const w = this.nodeWidth(d);
      const h = this.nodeHeight(d);
      if (x >= n.x && x <= n.x + w && y >= n.y && y <= n.y + h) return n;
    }
    return null;
  }

  private nodeWidth(d: VSNodeDef): number { return 150; }
  private nodeHeight(d: VSNodeDef): number {
    return 34 + Math.max(d.inputs.length, d.outputs.length) * 20 + d.fields.length * 26;
  }

  private portPos(n: VSNode, port: VSPort, isInput: boolean): { x: number; y: number } {
    const d = this.nodeDef(n.type)!;
    const w = this.nodeWidth(d);
    const list = isInput ? d.inputs : d.outputs;
    const idx = list.findIndex((p) => p.id === port.id);
    return {
      x: n.x + (isInput ? 8 : w - 8),
      y: n.y + 30 + idx * 20 + 8,
    };
  }

  // ---------------- eventos do canvas ----------------

  private bindCanvasEvents(): void {
    const c = this.canvas!;
    const getPos = (e: PointerEvent) => {
      const r = c.getBoundingClientRect();
      return {
        x: (e.clientX - r.left) / this.view.zoom - this.view.x,
        y: (e.clientY - r.top) / this.view.zoom - this.view.y,
        sx: e.clientX - r.left,
        sy: e.clientY - r.top,
      };
    };

    let last: { x: number; y: number } | null = null;
    let pointers = 0;
    let pinchStart = 0;
    let zoomStart = 1;

    c.addEventListener('pointerdown', (e) => {
      c.setPointerCapture(e.pointerId);
      pointers++;
      const p = getPos(e);

      // porta de saída → iniciar conexão
      const port = this.hitOutputPort(p.x, p.y);
      if (port) {
        this.linking = { from: `${port.node.id}:${port.port.id}`, x: p.sx, y: p.sy };
        return;
      }
      // entrada → concluir conexão
      const inPort = this.hitInputPort(p.x, p.y);
      if (inPort && this.linking) {
        this.connect(this.linking.from, `${inPort.node.id}:${inPort.port.id}`);
        this.linking = null;
        return;
      }

      const n = this.nodeAt(p.x, p.y);
      if (n) {
        // toque em campo de edição?
        const d = this.nodeDef(n.type)!;
        let fy = n.y + 34 + Math.max(d.inputs.length, d.outputs.length) * 20 + 6;
        for (const f of d.fields) {
          if (p.y >= fy && p.y <= fy + 24) {
            void this.editField(n, f);
            return;
          }
          fy += 26;
        }
        this.dragNode = { id: n.id, dx: p.x - n.x, dy: p.y - n.y };
        this.selectedNode = n.id;
      } else {
        this.dragView = { x: this.view.x, y: this.view.y, px: p.sx, py: p.sy };
      }
      last = { x: p.sx, y: p.sy };
    });

    c.addEventListener('pointermove', (e) => {
      const p = getPos(e);
      if (this.linking) {
        this.linking.x = p.sx;
        this.linking.y = p.sy;
        this.draw();
        return;
      }
      if (this.dragNode) {
        const n = this.graph.nodes.find((x) => x.id === this.dragNode!.id);
        if (n) {
          n.x = p.x - this.dragNode.dx;
          n.y = p.y - this.dragNode.dy;
          this.draw();
        }
        return;
      }
      if (this.dragView && last) {
        this.view.x = this.dragView.x + (p.sx - this.dragView.px) / this.view.zoom;
        this.view.y = this.dragView.y + (p.sy - this.dragView.py) / this.view.zoom;
        this.draw();
      }
      if (pointers === 2) {
        // pinça
        const touches = [...(c as unknown as { _pts?: Map<number, { x: number; y: number }> })._pts?.values() ?? []];
        void touches;
      }
    });

    const endPointer = (e: PointerEvent) => {
      pointers = Math.max(0, pointers - 1);
      if (this.linking) {
        const p = getPos(e);
        const inPort = this.hitInputPort(p.x, p.y);
        if (inPort) this.connect(this.linking.from, `${inPort.node.id}:${inPort.port.id}`);
        this.linking = null;
        this.draw();
      }
      this.dragNode = null;
      this.dragView = null;
      last = null;
      pinchStart = 0;
      void pinchStart;
      void zoomStart;
    };
    c.addEventListener('pointerup', endPointer);
    c.addEventListener('pointercancel', endPointer);

    // zoom por roda
    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      const factor = Math.pow(1.0015, -e.deltaY);
      this.view.zoom = Math.min(2, Math.max(0.4, this.view.zoom * factor));
      this.draw();
    }, { passive: false });

    // pinch zoom
    c.addEventListener('touchmove', (e) => {
      if (e.touches.length === 2) {
        e.preventDefault();
        const dist = Math.hypot(e.touches[0].clientX - e.touches[1].clientX, e.touches[0].clientY - e.touches[1].clientY);
        if (pinchStart > 0) {
          this.view.zoom = Math.min(2, Math.max(0.4, zoomStart * dist / pinchStart));
          this.draw();
        } else {
          pinchStart = dist;
          zoomStart = this.view.zoom;
        }
      }
    }, { passive: false });
  }

  private hitOutputPort(x: number, y: number): { node: VSNode; port: VSPort } | null {
    return this.hitPort(x, y, false);
  }
  private hitInputPort(x: number, y: number): { node: VSNode; port: VSPort } | null {
    return this.hitPort(x, y, true);
  }

  private hitPort(x: number, y: number, isInput: boolean): { node: VSNode; port: VSPort } | null {
    for (const n of this.graph.nodes) {
      const d = this.nodeDef(n.type);
      if (!d) continue;
      const list = isInput ? d.inputs : d.outputs;
      for (const port of list) {
        const pp = this.portPos(n, port, isInput);
        if (Math.hypot(x - pp.x, y - pp.y) < 12) return { node: n, port };
      }
    }
    return null;
  }

  /** Conecta com validação de tipo. */
  private connect(from: string, to: string): void {
    const [fromNode, fromPortId] = from.split(':');
    const [toNode, toPortId] = to.split(':');
    if (fromNode === toNode) return;
    const fd = this.nodeDef(this.graph.nodes.find((n) => n.id === fromNode)?.type ?? '');
    const td = this.nodeDef(this.graph.nodes.find((n) => n.id === toNode)?.type ?? '');
    if (!fd || !td) return;
    const outP = fd.outputs.find((p) => p.id === fromPortId);
    const inP = td.inputs.find((p) => p.id === toPortId);
    if (!outP || !inP) return;

    const compatible = (a: PortType, b: PortType) => a === b || a === 'any' || b === 'any' || (a === 'exec' && b === 'exec');
    if (!compatible(outP.type, inP.type)) {
      toast(`Tipo incompatível: ${outP.type} → ${inP.type}`, 'err');
      return;
    }
    // substitui conexão existente na mesma entrada
    this.graph.edges = this.graph.edges.filter((e) => e.to !== to);
    this.graph.edges.push({ id: `e${Date.now().toString(36)}`, from, to });
    this.draw();
    this.app.markDirty();
  }

  private async editField(n: VSNode, field: string): Promise<void> {
    const v = await promptModal(`Valor de "${field}"`, '', n.fields[field] ?? '');
    if (v === null) return;
    n.fields[field] = v;
    this.draw();
    this.app.markDirty();
  }

  private async clearGraph(): Promise<void> {
    const ok = await confirmModal('Limpar grafo', 'Remover todos os nós e conexões?');
    if (!ok) return;
    this.graph = { name: this.graph.name, nodes: [], edges: [] };
    this.draw();
  }

  // ---------------- compilação p/ G.oni Script ----------------

  private inputOf(nodeId: string, portId: string): { node: VSNode; port: string } | null {
    const edge = this.graph.edges.find((e) => e.to === `${nodeId}:${portId}`);
    if (!edge) return null;
    const [fn, fp] = edge.from.split(':');
    const n = this.graph.nodes.find((x) => x.id === fn);
    if (!n) return null;
    return { node: n, port: fp };
  }

  private exprOf(nodeId: string, portId: string, fallback: string, depth = 0): string {
    if (depth > 8) return fallback;
    const src = this.inputOf(nodeId, portId);
    if (!src) {
      // valor literal do campo, se houver
      const n = this.graph.nodes.find((x) => x.id === nodeId);
      const d = n ? this.nodeDef(n.type) : undefined;
      if (n && d) {
        const inP = d.inputs.find((p) => p.id === portId);
        if (inP && n.fields[portId] !== undefined) {
          const v = n.fields[portId];
          return inP.type === 'string' ? `"${v}"` : v;
        }
      }
      return fallback;
    }
    const d = this.nodeDef(src.node.type);
    if (!d || !d.code) return fallback;
    const result = d.code(src.node, (pid, fb) => this.exprOf(src.node.id, pid, fb, depth + 1));
    return result ?? fallback;
  }

  /** Gera o código G.oni Script a partir do grafo. */
  compile(): string {
    const lines: string[] = ['# Gerado pelo G.oni Visual', ''];
    // declara variáveis usadas
    const varNodes = this.graph.nodes.filter((n) => n.type === 'var_get' || n.type === 'var_set');
    const declared = new Set<string>();
    for (const v of varNodes) {
      const name = v.fields.name ?? 'minha_var';
      if (!declared.has(name)) {
        lines.push(`var ${name} = 0`);
        declared.add(name);
      }
    }
    if (declared.size) lines.push('');

    for (const ev of ['on_ready', 'on_process'] as const) {
      const evNode = this.graph.nodes.find((n) => n.type === ev);
      if (!evNode) continue;
      const sig = ev === 'on_process' ? '(delta)' : '()';
      lines.push(`func ${ev}${sig}:`);
      const emitted = this.emitChain(evNode.id, 0, new Set());
      lines.push(...(emitted || ['    pass']));
      lines.push('');
    }
    if (lines.length <= 3) {
      lines.push('func on_ready():', '    print("grafo vazio")');
    }
    return lines.join('\n');
  }

  private emitChain(fromNodeId: string, depth: number, visited: Set<string>): string[] {
    if (depth > 24) return [];
    const out: string[] = [];
    // segue arestas exec saindo do nó
    const nextEdges = this.graph.edges
      .filter((e) => e.from.startsWith(`${fromNodeId}:`))
      .map((e) => {
        const [nodeId, portId] = e.from.split(':');
        const d = this.nodeDef(this.graph.nodes.find((n) => n.id === nodeId)?.type ?? '');
        return { e, portId, d };
      })
      .filter(({ d, portId }) => d?.outputs.find((p) => p.id === portId)?.type === 'exec');

    for (const { e } of nextEdges) {
      const [toNodeId] = e.to.split(':');
      if (visited.has(toNodeId)) continue;
      visited.add(toNodeId);
      const node = this.graph.nodes.find((n) => n.id === toNodeId);
      if (!node) continue;
      const d = this.nodeDef(node.type);
      if (!d) continue;

      if (node.type === 'branch') {
        const cond = this.exprOf(node.id, 'cond', 'true');
        out.push(`    if ${cond}:`);
        const trueBranch = this.emitBranchOut(node, 'true', depth + 1, new Set(visited));
        out.push(...(trueBranch.length ? trueBranch.map((l) => '    ' + l) : ['        pass']));
        const falseEdges = this.graph.edges.filter((e2) => e2.from === `${node.id}:false`);
        if (falseEdges.length) {
          out.push('    else:');
          const falseBranch = this.emitBranchOut(node, 'false', depth + 1, new Set(visited));
          out.push(...(falseBranch.length ? falseBranch.map((l) => '    ' + l) : ['        pass']));
        }
      } else {
        const code = d.code(node, (pid, fb) => this.exprOf(node.id, pid, fb, 0));
        if (code) out.push(code);
        out.push(...this.emitChain(node.id, depth + 1, visited).map((l) => l));
      }
    }
    return out;
  }

  private emitBranchOut(node: VSNode, portId: string, depth: number, visited: Set<string>): string[] {
    const edges = this.graph.edges.filter((e) => e.from === `${node.id}:${portId}`);
    const out: string[] = [];
    for (const e of edges) {
      const [toNodeId] = e.to.split(':');
      const target = this.graph.nodes.find((n) => n.id === toNodeId);
      if (!target) continue;
      const d = this.nodeDef(target.type);
      if (!d) continue;
      const code = d.code(target, (pid, fb) => this.exprOf(target.id, pid, fb, 0));
      if (code) out.push(code.replace(/^    /, ''));
      out.push(...this.emitChain(target.id, depth, visited).map((l) => l.replace(/^    /, '')));
    }
    return out.map((l) => '    ' + l).map((l) => l.replace(/^    /, ''));
  }

  private async compileToScript(): Promise<void> {
    const src = this.compile();
    // salva como script novo
    const name = await promptModal('Exportar como script', '', `${this.graph.name.replace(/[^a-z0-9_]/gi, '_')}_visual`);
    if (!name) return;
    this.app.engine.scriptSources.set(name, src);
    this.app.engine.astCacheTouch();
    this.app.markDirty();
    toast(`Script "${name}" gerado a partir do grafo`);
    // abre no editor
    this.app.openSheet('scripts');
  }

  private async attachAsScript(): Promise<void> {
    const sel = this.app.viewport.selected;
    if (!sel) {
      toast('Selecione um objeto primeiro', 'err');
      return;
    }
    const name = `visual_${Date.now().toString(36)}`;
    this.app.engine.scriptSources.set(name, this.compile());
    this.app.engine.astCacheTouch();
    let comp = sel.getComponent<ScriptComponent>('script');
    if (!comp) comp = sel.addComponent(new ScriptComponent());
    comp.scriptName = name;
    this.app.markDirty();
    toast(`Grafo anexado a ${sel.name} como script`);
  }

  serialize(): VSGraph {
    return this.graph;
  }

  loadGraph(g: VSGraph): void {
    this.graph = g;
  }

  // ---------------- desenho ----------------

  private draw(): void {
    const c = this.canvas;
    if (!c) return;
    const rect = c.getBoundingClientRect();
    if (rect.width === 0) {
      requestAnimationFrame(() => this.draw());
      return;
    }
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    c.width = Math.floor(rect.width * dpr);
    c.height = Math.floor(rect.height * dpr);
    const ctx = c.getContext('2d')!;
    ctx.scale(dpr, dpr);
    ctx.clearRect(0, 0, rect.width, rect.height);

    // fundo
    ctx.fillStyle = '#0d1017';
    ctx.fillRect(0, 0, rect.width, rect.height);
    // grid
    ctx.strokeStyle = 'rgba(255,255,255,0.04)';
    ctx.lineWidth = 1;
    const cell = 28 * this.view.zoom;
    const ox = (this.view.x * this.view.zoom) % cell;
    const oy = (this.view.y * this.view.zoom) % cell;
    for (let x = ox; x < rect.width; x += cell) {
      ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, rect.height); ctx.stroke();
    }
    for (let y = oy; y < rect.height; y += cell) {
      ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(rect.width, y); ctx.stroke();
    }

    ctx.save();
    ctx.scale(this.view.zoom, this.view.zoom);
    ctx.translate(this.view.x, this.view.y);

    // arestas
    ctx.lineWidth = 2;
    for (const e of this.graph.edges) {
      const [fn, fp] = e.from.split(':');
      const [tn, tp] = e.to.split(':');
      const fromN = this.graph.nodes.find((n) => n.id === fn);
      const toN = this.graph.nodes.find((n) => n.id === tn);
      if (!fromN || !toN) continue;
      const fd = this.nodeDef(fromN.type);
      const td = this.nodeDef(toN.type);
      const outP = fd?.outputs.find((p) => p.id === fp);
      const inP = td?.inputs.find((p) => p.id === tp);
      if (!outP || !inP) continue;
      const a = this.portPos(fromN, outP, false);
      const b = this.portPos(toN, inP, true);
      const isExec = outP.type === 'exec';
      ctx.strokeStyle = isExec ? '#e8ecf4' : '#ffb020';
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.bezierCurveTo(a.x + 40, a.y, b.x - 40, b.y, b.x, b.y);
      ctx.stroke();
    }

    // linha de conexão em andamento
    if (this.linking) {
      const [fn, fp] = this.linking.from.split(':');
      const fromN = this.graph.nodes.find((n) => n.id === fn);
      const fd = fromN ? this.nodeDef(fromN.type) : undefined;
      const outP = fd?.outputs.find((p) => p.id === fp);
      if (fromN && outP) {
        const a = this.portPos(fromN, outP, false);
        ctx.strokeStyle = '#ffd07a';
        ctx.setLineDash([5, 4]);
        ctx.beginPath();
        ctx.moveTo(a.x, a.y);
        ctx.lineTo(this.linking.x / this.view.zoom - this.view.x, this.linking.y / this.view.zoom - this.view.y);
        ctx.stroke();
        ctx.setLineDash([]);
      }
    }

    // nós
    for (const n of this.graph.nodes) {
      const d = this.nodeDef(n.type);
      if (!d) continue;
      const w = this.nodeWidth(d);
      const h = this.nodeHeight(d);
      const sel = n.id === this.selectedNode;

      ctx.fillStyle = d.category === 'evento' ? '#22301e' : '#1a2030';
      ctx.strokeStyle = sel ? '#ffb020' : '#2a3348';
      ctx.lineWidth = sel ? 2 : 1;
      roundRect(ctx, n.x, n.y, w, h, 8);
      ctx.fill();
      ctx.stroke();

      // título
      ctx.fillStyle = d.category === 'evento' ? '#7fdb8f' : '#e8ecf4';
      ctx.font = '600 11px system-ui';
      ctx.fillText(d.label, n.x + 10, n.y + 17);

      // portas
      const drawPorts = (ports: VSPort[], isInput: boolean) => {
        ports.forEach((p, i) => {
          const pp = this.portPos(n, p, isInput);
          ctx.beginPath();
          ctx.arc(pp.x, pp.y, 4, 0, Math.PI * 2);
          ctx.fillStyle = p.type === 'exec' ? '#e8ecf4' : '#ffb020';
          ctx.fill();
          ctx.fillStyle = '#9aa6bd';
          ctx.font = '10px system-ui';
          ctx.textAlign = isInput ? 'left' : 'right';
          ctx.fillText(p.name, pp.x + (isInput ? 10 : -10), pp.y + 3);
          ctx.textAlign = 'left';
        });
      };
      drawPorts(d.inputs, true);
      drawPorts(d.outputs, false);

      // campos
      let fy = n.y + 34 + Math.max(d.inputs.length, d.outputs.length) * 20 + 4;
      for (const f of d.fields) {
        ctx.fillStyle = '#0d1017';
        ctx.strokeStyle = '#2a3348';
        roundRect(ctx, n.x + 8, fy, w - 16, 20, 4);
        ctx.fill();
        ctx.stroke();
        ctx.fillStyle = '#9aa6bd';
        ctx.font = '10px ui-monospace, monospace';
        ctx.fillText(`${f}=${n.fields[f] ?? ''}`, n.x + 14, fy + 13);
        fy += 26;
      }
    }
    ctx.restore();
  }
}

function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number): void {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r);
  ctx.arcTo(x, y + h, x, y, r);
  ctx.arcTo(x, y, x + w, y, r);
  ctx.closePath();
}
