/**
 * G.oni Funciona — Gerenciador de funções globais e métodos.
 *
 * Permite:
 *  - registro de funções nativas da engine (JS)
 *  - registro de funções definidas em G.oni Script
 *  - chamada cruzada entre scripts (call_function("dano_total", ...))
 *  - hot-reload: substituição de implementações em tempo de edição
 */

export interface FuncMeta {
  args?: string[];
  desc?: string;
  module?: string;
}

type NativeFn = (args: unknown[], ctx?: unknown) => unknown;

interface Entry {
  name: string;
  kind: 'native' | 'script';
  meta: FuncMeta;
  fn?: NativeFn;
  scriptCallable?: (args: unknown[]) => unknown;
}

export class Funciona {
  private fns = new Map<string, Entry>();

  /** Registra função nativa (engine/editor/plugins). */
  registerNative(name: string, fn: NativeFn, meta: FuncMeta = {}): void {
    this.fns.set(name, { name, kind: 'native', meta, fn });
  }

  /** Registra função de script (chamável pela VM). */
  registerScript(name: string, callable: (args: unknown[]) => unknown, meta: FuncMeta = {}): void {
    this.fns.set(name, { name, kind: 'script', meta, scriptCallable: callable });
  }

  /**
   * Hot-reload: substitui implementação mantendo identidade.
   * Se callable for null, remove a função de script.
   */
  hotReload(name: string, callable: ((args: unknown[]) => unknown) | null): void {
    if (callable === null) {
      this.fns.delete(name);
      return;
    }
    const existing = this.fns.get(name);
    if (existing) {
      existing.scriptCallable = callable;
      existing.kind = 'script';
    } else {
      this.registerScript(name, callable);
    }
  }

  has(name: string): boolean { return this.fns.has(name); }
  kindOf(name: string): 'native' | 'script' | null {
    return this.fns.get(name)?.kind ?? null;
  }

  /** Chamada genérica. Lança erro se a função não existir. */
  call(name: string, args: unknown[] = [], ctx?: unknown): unknown {
    const e = this.fns.get(name);
    if (!e) throw new Error(`G.oni Funciona: função "${name}" não registrada`);
    if (e.kind === 'native') return e.fn!(args, ctx);
    return e.scriptCallable!(args);
  }

  /** Chamada segura: retorna fallback em caso de erro. */
  tryCall(name: string, args: unknown[] = [], fallback: unknown = null, ctx?: unknown): unknown {
    try {
      return this.call(name, args, ctx);
    } catch {
      return fallback;
    }
  }

  list(kind?: 'native' | 'script'): { name: string; kind: string; meta: FuncMeta }[] {
    return [...this.fns.values()]
      .filter((e) => !kind || e.kind === kind)
      .map((e) => ({ name: e.name, kind: e.kind, meta: e.meta }));
  }

  serialize(): Record<string, unknown> {
    return {
      natives: this.list('native').map((f) => ({ name: f.name, ...f.meta })),
      scripts: this.list('script').map((f) => ({ name: f.name, ...f.meta })),
    };
  }
}
