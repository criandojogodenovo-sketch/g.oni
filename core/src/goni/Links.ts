/**
 * G.oni Links — Sistema de conexões entre nós, scripts e objetos.
 *
 * Representa as arestas do grafo do projeto:
 *  - referências entre entidades (objeto → objeto, script → objeto)
 *  - conexões de eventos (sinal → script, sinal → função)
 *
 * Recursos: detecção de ciclos, propagação de eventos,
 * serialização no formato .g.oni.
 */

export type LinkNodeKind = 'object' | 'script' | 'signal' | 'function' | 'prefab' | 'variable';
export type LinkEdgeKind = 'reference' | 'signal' | 'event' | 'data';

export interface LinkNode {
  id: string;
  kind: LinkNodeKind;
  refId: string; // id do objeto / nome do script / nome do sinal...
  label?: string;
}

export interface LinkEdge {
  from: string;
  to: string;
  kind: LinkEdgeKind;
  label?: string;
  /** id para permitir remoção individual */
  id: string;
}

export class CycleError extends Error {
  constructor(public readonly path: string[]) {
    super(`G.oni Links: ciclo detectado: ${path.join(' → ')}`);
  }
}

let edgeCounter = 0;

export class LinkGraph {
  nodes: LinkNode[] = [];
  edges: LinkEdge[] = [];

  addNode(kind: LinkNodeKind, refId: string, label?: string): LinkNode {
    const existing = this.nodes.find((n) => n.kind === kind && n.refId === refId);
    if (existing) return existing;
    const node: LinkNode = { id: `ln_${kind}_${refId}`, kind, refId, label };
    this.nodes.push(node);
    return node;
  }

  removeNode(nodeId: string): void {
    this.nodes = this.nodes.filter((n) => n.id !== nodeId);
    this.edges = this.edges.filter((e) => e.from !== nodeId && e.to !== nodeId);
  }

  /**
   * Adiciona uma aresta com validação de ciclo.
   * Uma aresta de "reference"/"data" não pode fechar ciclo (evita dependência circular).
   * Arestas de "signal"/"event" podem (reactions em cadeia são resolvidas em runtime com limite).
   */
  addEdge(from: string, to: string, kind: LinkEdgeKind, label?: string, allowCycle = false): LinkEdge {
    if (!allowCycle && (kind === 'reference' || kind === 'data')) {
      if (this.wouldCreateCycle(from, to)) {
        throw new CycleError(this.findCyclePath(from, to));
      }
    }
    const edge: LinkEdge = { id: `le_${++edgeCounter}`, from, to, kind, label };
    this.edges.push(edge);
    return edge;
  }

  removeEdge(edgeId: string): void {
    this.edges = this.edges.filter((e) => e.id !== edgeId);
  }

  edgesFrom(nodeId: string): LinkEdge[] { return this.edges.filter((e) => e.from === nodeId); }
  edgesTo(nodeId: string): LinkEdge[] { return this.edges.filter((e) => e.to === nodeId); }

  /** DFS: verificar se adicionar from→to criaria ciclo. */
  wouldCreateCycle(from: string, to: string): boolean {
    if (from === to) return true;
    const visited = new Set<string>();
    const stack = [to];
    while (stack.length) {
      const cur = stack.pop()!;
      if (cur === from) return true;
      if (visited.has(cur)) continue;
      visited.add(cur);
      for (const e of this.edgesFrom(cur)) stack.push(e.to);
    }
    return false;
  }

  findCyclePath(from: string, to: string): string[] {
    const prev = new Map<string, string>();
    const queue = [to];
    const visited = new Set<string>();
    while (queue.length) {
      const cur = queue.shift()!;
      if (cur === from) {
        const path = [from];
        let p = to;
        while (p !== from) { path.unshift(p); p = prev.get(p)!; }
        return path;
      }
      if (visited.has(cur)) continue;
      visited.add(cur);
      for (const e of this.edgesFrom(cur)) {
        if (!visited.has(e.to)) { prev.set(e.to, cur); queue.push(e.to); }
      }
    }
    return [from, to];
  }

  /**
   * Propagação de eventos: a partir de um nó, dispara callbacks
   * seguindo arestas de signal/event (BFS com limite de profundidade
   * para evitar loops infinitos).
   */
  propagate(fromNode: string, payload: unknown, onVisit: (edge: LinkEdge, payload: unknown) => void, maxDepth = 16): number {
    const visited = new Set<string>();
    const queue: { node: string; depth: number; payload: unknown }[] = [{ node: fromNode, depth: 0, payload }];
    let visits = 0;
    while (queue.length) {
      const { node, depth, payload: pl } = queue.shift()!;
      if (depth >= maxDepth) continue;
      for (const e of this.edgesFrom(node)) {
        if (e.kind !== 'signal' && e.kind !== 'event') continue;
        const key = `${e.from}→${e.to}`;
        if (visited.has(key)) continue;
        visited.add(key);
        onVisit(e, pl);
        visits++;
        queue.push({ node: e.to, depth: depth + 1, payload: pl });
      }
    }
    return visits;
  }

  serialize(): Record<string, unknown> {
    return { nodes: this.nodes, edges: this.edges };
  }

  static deserialize(d: Record<string, unknown>): LinkGraph {
    const g = new LinkGraph();
    g.nodes = (d.nodes as LinkNode[]) ?? [];
    g.edges = (d.edges as LinkEdge[]) ?? [];
    for (const e of g.edges) {
      const n = parseInt(String(e.id ?? 'le_0').split('_')[1] ?? '0', 10);
      if (!isNaN(n) && n > edgeCounter) edgeCounter = n;
    }
    return g;
  }
}
