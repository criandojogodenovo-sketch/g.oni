/**
 * G.oni Signal — Sistema de sinais/eventos inspirado no Godot.
 *
 * API:
 *   signal meusinal                (declaração em G.oni Script)
 *   emit("nome", args...)          (emite)
 *   connect("nome", callback)      (escuta)
 *   disconnect("nome", callback)   (para de escutar)
 *   await signal("nome")           (corrotina aguarda próximo disparo)
 *
 * Sinais nativos da engine:
 *   on_ready, on_process, on_input, on_destroy,
 *   on_collision_enter, on_collision_exit
 */

export type SignalCallback = (...args: unknown[]) => void;

interface Listener {
  cb: SignalCallback;
  once: boolean;
  id: number;
}

let listenerId = 0;

export class Signal {
  private listeners: Listener[] = [];
  private waiters: { resolve: (v: unknown[]) => void }[] = [];

  constructor(public readonly name: string) {}

  get connectionCount(): number { return this.listeners.length; }

  connect(cb: SignalCallback, opts: { once?: boolean } = {}): number {
    const id = ++listenerId;
    this.listeners.push({ cb, once: !!opts.once, id });
    return id;
  }

  disconnect(cbOrId: SignalCallback | number): boolean {
    const before = this.listeners.length;
    if (typeof cbOrId === 'number') {
      this.listeners = this.listeners.filter((l) => l.id !== cbOrId);
    } else {
      this.listeners = this.listeners.filter((l) => l.cb !== cbOrId);
    }
    return this.listeners.length < before;
  }

  disconnectAll(): void {
    this.listeners = [];
  }

  /** Emite o sinal. Retorna o número de listeners notificados. */
  emit(args: unknown[] = []): number {
    const list = [...this.listeners];
    let n = 0;
    for (const l of list) {
      if (l.once) this.disconnect(l.id);
      try {
        l.cb(...args);
        n++;
      } catch (err) {
        console.error(`[G.oni Signal] erro em listener de "${this.name}":`, err);
      }
    }
    // acordar corrotinas em await
    const waiting = this.waiters;
    this.waiters = [];
    for (const w of waiting) w.resolve(args);
    return n;
  }

  /** Promise resolvida no próximo disparo (usada por `await signal(...)`). */
  next(): Promise<unknown[]> {
    return new Promise((resolve) => this.waiters.push({ resolve }));
  }
}

/** Barramento de sinais por objeto. */
export class SignalBus {
  private signals = new Map<string, Signal>();

  static readonly NATIVE = [
    'on_ready', 'on_process', 'on_input', 'on_destroy',
    'on_collision_enter', 'on_collision_exit',
  ] as const;

  private getOrCreate(name: string): Signal {
    let s = this.signals.get(name);
    if (!s) {
      s = new Signal(name);
      this.signals.set(name, s);
    }
    return s;
  }

  declare(name: string): Signal {
    return this.getOrCreate(name);
  }

  connect(name: string, cb: SignalCallback, opts: { once?: boolean } = {}): number {
    return this.getOrCreate(name).connect(cb, opts);
  }

  disconnect(name: string, cbOrId: SignalCallback | number): boolean {
    const s = this.signals.get(name);
    return s ? s.disconnect(cbOrId) : false;
  }

  emit(name: string, args: unknown[] = []): number {
    const s = this.signals.get(name);
    return s ? s.emit(args) : 0;
  }

  waitSignal(name: string): Promise<unknown[]> {
    return this.getOrCreate(name).next();
  }

  has(name: string): boolean { return this.signals.has(name); }
  get(name: string): Signal | undefined { return this.signals.get(name); }
  list(): string[] { return [...this.signals.keys()]; }

  clear(): void {
    for (const s of this.signals.values()) s.disconnectAll();
    this.signals.clear();
  }
}
