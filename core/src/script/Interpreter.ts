/**
 * G.oni Llumni — Interpretador do G.oni Script
 *
 * Tree-walking interpreter assíncrono:
 *  - todas as avaliações retornam Promise → `await` nativo dentro de qualquer função
 *  - corrotinas: on_process pode aguardar sinais/timers sem travar a engine
 *  - classes com herança simples, instâncias, closures
 *  - valores: number, string, bool, null, list, dict, Vec3, GOniObject,
 *    funções, classes, instâncias
 */

import { Node } from './Parser';
import { Vec3, MathUtils } from '../math';

// ---------------- Valores de runtime ----------------

export class FuncValue {
  constructor(
    public name: string,
    public params: { name: string; def: Node | null }[],
    public body: Node[],
    public closure: Environment,
    public isLambda = false
  ) {}
}

export class ClassValue {
  methods = new Map<string, FuncValue>();
  fieldDefs = new Map<string, Node | null>();
  constructor(public name: string, public parent: ClassValue | null) {}
  findMethod(name: string): FuncValue | null {
    if (this.methods.has(name)) return this.methods.get(name)!;
    return this.parent?.findMethod(name) ?? null;
  }
}

export class Instance {
  fields = new Environment(null);
  constructor(public cls: ClassValue) {}
}

export class NativeFunc {
  constructor(
    public name: string,
    public fn: (args: unknown[], env: Environment) => unknown | Promise<unknown>
  ) {}
}

export class GOniRuntimeError extends Error {
  constructor(msg: string, public line?: number) {
    super(`[G.oni Script] ${msg}${line ? ` (linha ${line})` : ''}`);
  }
}

// ---------------- Ambiente ----------------

export class Environment {
  vars = new Map<string, { value: unknown; isConst: boolean }>();
  constructor(public parent: Environment | null) {}

  define(name: string, value: unknown, isConst = false): void {
    this.vars.set(name, { value, isConst });
  }

  get(name: string, line?: number): unknown {
    let e: Environment | null = this;
    while (e) {
      if (e.vars.has(name)) return e.vars.get(name)!.value;
      e = e.parent;
    }
    throw new GOniRuntimeError(`variável "${name}" não definida`, line);
  }

  has(name: string): boolean {
    let e: Environment | null = this;
    while (e) {
      if (e.vars.has(name)) return true;
      e = e.parent;
    }
    return false;
  }

  set(name: string, value: unknown, line?: number): void {
    let e: Environment | null = this;
    while (e) {
      const slot = e.vars.get(name);
      if (slot) {
        if (slot.isConst) throw new GOniRuntimeError(`"${name}" é constante e não pode ser reatribuída`, line);
        slot.value = value;
        return;
      }
      e = e.parent;
    }
    // define na env atual (comportamento pragmático)
    this.define(name, value);
  }
}

// ---------------- Sinais de controle ----------------

class BreakSignal { }
class ContinueSignal { }
class ReturnSignal { constructor(public value: unknown) {} }

// ---------------- Bridge para objetos da engine ----------------

export interface ObjectBridge {
  /** propriedades get (position, name, ...) */
  get(obj: unknown, name: string): unknown | undefined;
  /** propriedades set */
  set(obj: unknown, name: string, value: unknown): boolean;
  /** métodos (translate, look_at, ...) */
  call(obj: unknown, method: string, args: unknown[], line?: number): unknown;
  /** o valor é um objeto da engine? */
  isObject(obj: unknown): boolean;
  /** nome para mensagens de erro */
  typeName(obj: unknown): string;
}

// ---------------- Interpreter ----------------

export class Interpreter {
  globals = new Environment(null);
  objectBridge: ObjectBridge | null = null;

  /** orçamento de instruções por chamada (proteção contra loop infinito) */
  private budget = 0;
  static readonly MAX_STATEMENTS = 200000;

  constructor(globals?: Environment) {
    if (globals) this.globals = globals;
  }

  /** Executa um programa no ambiente dado (top-level de um script). */
  async run(program: Node, env: Environment): Promise<void> {
    this.budget = Interpreter.MAX_STATEMENTS;
    await this.execBlock((program as { body: Node[] }).body, env);
  }

  /** Chama uma função por nome em um ambiente (método de script/instância). */
  async callByName(env: Environment, name: string, args: unknown[]): Promise<unknown | null> {
    if (!env.has(name)) return null;
    const fn = env.get(name);
    if (fn instanceof FuncValue) {
      return this.callFunction(fn, args, env);
    }
    return null;
  }

  async callFunction(fn: FuncValue, args: unknown[], envForSelf?: Environment): Promise<unknown> {
    const callEnv = new Environment(fn.closure);
    if (envForSelf) {
      // permite recursão e resolve self
      callEnv.define(fn.name, fn);
    }
    this.bindParams(fn, args, callEnv);
    this.budget = Interpreter.MAX_STATEMENTS;
    try {
      await this.execBlock(fn.body, callEnv);
    } catch (e) {
      if (e instanceof ReturnSignal) return e.value;
      throw e;
    }
    return null;
  }

  private bindParams(fn: FuncValue, args: unknown[], env: Environment): void {
    for (let i = 0; i < fn.params.length; i++) {
      const p = fn.params[i];
      if (i < args.length) env.define(p.name, args[i]);
      else if (p.def) env.define(p.name, this.evalSyncOrThrow(p.def, env));
      else env.define(p.name, null);
    }
  }

  /** Avaliação síncrona para valores default (literais simples). */
  private evalSyncOrThrow(node: Node, env: Environment): unknown {
    if (node.kind === 'num') return (node as { value: number }).value;
    if (node.kind === 'str') return (node as { value: string }).value;
    if (node.kind === 'bool') return (node as { value: boolean }).value;
    if (node.kind === 'null') return null;
    // fallback: assume null (defaults complexos são raros)
    void env;
    void node;
    return null;
  }

  // ---------------- Execução de statements ----------------

  private async execBlock(body: Node[], env: Environment): Promise<void> {
    for (const stmt of body) {
      if (--this.budget <= 0) {
        throw new GOniRuntimeError('limite de instruções excedido (loop infinito?)');
      }
      await this.exec(stmt, env);
    }
  }

  private async exec(node: Node, env: Environment): Promise<void> {
    switch (node.kind) {
      case 'vardecl': {
        const d = node as { name: string; value: Node | null; isConst: boolean; line: number };
        const value = d.value ? await this.eval(d.value, env) : null;
        env.define(d.name, value, d.isConst);
        return;
      }
      case 'funcdecl': {
        const d = node as { name: string; params: { name: string; def: Node | null }[]; body: Node[]; line: number };
        env.define(d.name, new FuncValue(d.name, d.params, d.body, env));
        return;
      }
      case 'classdecl': {
        const d = node as { name: string; parent: string | null; body: Node[]; line: number };
        const parentClass = d.parent
          ? (env.get(d.parent) instanceof ClassValue ? (env.get(d.parent) as ClassValue) : null)
          : null;
        const cls = new ClassValue(d.name, parentClass);
        // processa corpo da classe em env temporário
        const classEnv = new Environment(env);
        for (const stmt of d.body) {
          if (stmt.kind === 'vardecl') {
            const v = stmt as { name: string; value: Node | null };
            cls.fieldDefs.set(v.name, v.value);
          } else if (stmt.kind === 'funcdecl') {
            const f = stmt as { name: string; params: { name: string; def: Node | null }[]; body: Node[] };
            cls.methods.set(f.name, new FuncValue(f.name, f.params, f.body, env));
          } else if (stmt.kind === 'signaldecl') {
            const s = stmt as { name: string };
            cls.fieldDefs.set(`__signal_${s.name}`, null);
          }
        }
        void classEnv;
        env.define(d.name, cls);
        return;
      }
      case 'signaldecl': {
        const d = node as { name: string };
        // declaração de sinal no nível do script → registrada no objeto via bridge
        if (env.has('self') && this.objectBridge) {
          const self = env.get('self');
          this.objectBridge.call(self, '__declare_signal', [d.name], 0);
        }
        return;
      }
      case 'if': {
        const d = node as { cond: Node; then: Node[]; elifs: { cond: Node; body: Node[] }[]; else: Node[] | null; line: number };
        if (truthy(await this.eval(d.cond, env))) {
          await this.execBlock(d.then, new Environment(env));
        } else {
          let matched = false;
          for (const elif of d.elifs) {
            if (truthy(await this.eval(elif.cond, env))) {
              await this.execBlock(elif.body, new Environment(env));
              matched = true;
              break;
            }
          }
          if (!matched && d.else) {
            await this.execBlock(d.else, new Environment(env));
          }
        }
        return;
      }
      case 'while': {
        const d = node as { cond: Node; body: Node[]; line: number };
        while (truthy(await this.eval(d.cond, env))) {
          try {
            await this.execBlock(d.body, new Environment(env));
          } catch (e) {
            if (e instanceof BreakSignal) break;
            if (e instanceof ContinueSignal) continue;
            throw e;
          }
        }
        return;
      }
      case 'for': {
        const d = node as { varName: string; iter: Node; body: Node[]; line: number };
        const iterable = await this.eval(d.iter, env);
        for (const item of toIterable(iterable, d.line)) {
          const loopEnv = new Environment(env);
          loopEnv.define(d.varName, item);
          try {
            await this.execBlock(d.body, loopEnv);
          } catch (e) {
            if (e instanceof BreakSignal) break;
            if (e instanceof ContinueSignal) continue;
            throw e;
          }
        }
        return;
      }
      case 'return': {
        const d = node as { value: Node | null };
        throw new ReturnSignal(d.value ? await this.eval(d.value, env) : null);
      }
      case 'break': throw new BreakSignal();
      case 'continue': throw new ContinueSignal();
      case 'pass': return;
      case 'await': {
        const d = node as { expr: Node };
        await this.eval(d.expr, env);
        return;
      }
      case 'exprstmt': {
        const d = node as { expr: Node };
        await this.eval(d.expr, env);
        return;
      }
      case 'assign': {
        const d = node as { target: Node; op: string; value: Node; line: number };
        await this.assign(d.target, d.op, d.value, env, d.line);
        return;
      }
      default:
        throw new GOniRuntimeError(`statement não suportado: ${node.kind}`);
    }
  }

  // ---------------- Atribuição ----------------

  private async assign(target: Node, op: string, valueNode: Node, env: Environment, line: number): Promise<void> {
    if (target.kind === 'ident') {
      const name = (target as { name: string }).name;
      let value = await this.eval(valueNode, env);
      if (op !== '=') {
        const cur = env.get(name, line);
        value = applyOp(op, cur, value, line);
      }
      env.set(name, value, line);
      return;
    }
    if (target.kind === 'noderef') {
      const path = (target as { path: string }).path;
      // $No.propriedade = valor → resolve nó primeiro
      const obj = this.resolveNodeRef(path, env, line);
      // o target completo é member(noderef, prop) — tratado fora
      throw new GOniRuntimeError('atribuição direta a $nó não suportada; use $nó.propriedade', line);
    }
    if (target.kind === 'member') {
      const m = target as { obj: Node; name: string };
      const obj = await this.eval(m.obj, env);
      let value = await this.eval(valueNode, env);
      if (op !== '=') {
        const cur = this.memberGet(obj, m.name, line);
        value = applyOp(op, cur, value, line);
      }
      this.memberSet(obj, m.name, value, line);
      return;
    }
    if (target.kind === 'index') {
      const ix = target as { obj: Node; index: Node };
      const obj = await this.eval(ix.obj, env);
      const idx = await this.eval(ix.index, env);
      let value = await this.eval(valueNode, env);
      if (Array.isArray(obj)) {
        const i = Math.trunc(Number(idx));
        if (i < 0 || i >= obj.length) throw new GOniRuntimeError(`índice fora do intervalo: ${i}`, line);
        if (op !== '=') value = applyOp(op, obj[i], value, line);
        obj[i] = value;
        return;
      }
      if (obj instanceof Map) {
        if (op !== '=') {
          const cur = obj.get(idx);
          value = applyOp(op, cur, value, line);
        }
        obj.set(idx, value);
        return;
      }
      throw new GOniRuntimeError('alvo de indexação não é lista nem dicionário', line);
    }
    throw new GOniRuntimeError('alvo de atribuição inválido', line);
  }

  // ---------------- Avaliação ----------------

  async eval(node: Node, env: Environment): Promise<unknown> {
    switch (node.kind) {
      case 'num': return (node as { value: number }).value;
      case 'str': return (node as { value: string }).value;
      case 'bool': return (node as { value: boolean }).value;
      case 'null': return null;
      case 'self': return env.has('self') ? env.get('self') : null;
      case 'ident': return env.get((node as { name: string }).name, (node as { line: number }).line);
      case 'noderef': {
        return this.resolveNodeRef((node as { path: string }).path, env, (node as { line: number }).line);
      }
      case 'list': {
        const items: unknown[] = [];
        for (const item of (node as { items: Node[] }).items) {
          items.push(await this.eval(item, env));
        }
        return items;
      }
      case 'dict': {
        const dict = new Map<unknown, unknown>();
        for (const { key, value } of (node as { entries: { key: Node; value: Node }[] }).entries) {
          dict.set(await this.eval(key, env), await this.eval(value, env));
        }
        return dict;
      }
      case 'lambda': {
        const d = node as { params: string[]; body: Node[]; line: number };
        return new FuncValue('<lambda>', d.params.map((p) => ({ name: p, def: null })), d.body, env, true);
      }
      case 'unary': {
        const d = node as { op: string; operand: Node; line: number };
        const v = await this.eval(d.operand, env);
        if (d.op === '-') {
          if (typeof v === 'number') return -v;
          if (v instanceof Vec3) return v.clone().neg();
          throw new GOniRuntimeError('negação exige número ou vec3', d.line);
        }
        if (d.op === 'not') return !truthy(v);
        throw new GOniRuntimeError(`operador unário desconhecido: ${d.op}`, d.line);
      }
      case 'binop': {
        const d = node as { op: string; left: Node; right: Node; line: number };
        // curto-circuito
        if (d.op === 'and') {
          const l = await this.eval(d.left, env);
          if (!truthy(l)) return l;
          return this.eval(d.right, env);
        }
        if (d.op === 'or') {
          const l = await this.eval(d.left, env);
          if (truthy(l)) return l;
          return this.eval(d.right, env);
        }
        const l = await this.eval(d.left, env);
        const r = await this.eval(d.right, env);
        return binop(d.op, l, r, d.line);
      }
      case 'member': {
        const d = node as { obj: Node; name: string; line: number };
        const obj = await this.eval(d.obj, env);
        return this.memberGet(obj, d.name, d.line);
      }
      case 'index': {
        const d = node as { obj: Node; index: Node; line: number };
        const obj = await this.eval(d.obj, env);
        const idx = await this.eval(d.index, env);
        return indexGet(obj, idx, d.line);
      }
      case 'call': {
        const d = node as { callee: Node; args: Node[]; line: number };
        const callee = await this.eval(d.callee, env);
        const args: unknown[] = [];
        for (const a of d.args) args.push(await this.eval(a, env));
        return this.callValue(callee, args, env, d.line);
      }
      case 'await': {
        const d = node as { expr: Node };
        return this.eval(d.expr, env);
      }
      default:
        throw new GOniRuntimeError(`expressão não suportada: ${node.kind}`);
    }
  }

  async callValue(callee: unknown, args: unknown[], env: Environment, line: number): Promise<unknown> {
    if (callee instanceof NativeFunc) {
      return callee.fn(args, env);
    }
    if (callee instanceof FuncValue) {
      return this.callFunction(callee, args);
    }
    if (callee instanceof ClassValue) {
      // Player(...) também instancia
      return this.instantiate(callee, args);
    }
    if (this.objectBridge?.isObject(callee)) {
      return this.objectBridge.call(callee, '__call__', args, line);
    }
    throw new GOniRuntimeError('valor não é chamável', line);
  }

  instantiate(cls: ClassValue, _args: unknown[]): Instance {
    const inst = new Instance(cls);
    // inicializa campos da cadeia de classes
    const chain: ClassValue[] = [];
    let c: ClassValue | null = cls;
    while (c) { chain.unshift(c); c = c.parent; }
    for (const k of chain) {
      for (const [name, defNode] of k.fieldDefs) {
        if (name.startsWith('__signal_')) continue;
        // default literal resolvido de forma simples
        const value = defNode ? this.evalSyncOrThrow(defNode, inst.fields) : null;
        inst.fields.define(name, value);
      }
    }
    return inst;
  }

  // ---------------- Acesso a membros ----------------

  memberGet(obj: unknown, name: string, line: number): unknown {
    // Vec3
    if (obj instanceof Vec3) {
      if (name === 'x') return obj.x;
      if (name === 'y') return obj.y;
      if (name === 'z') return obj.z;
      if (name === 'length') return new NativeFunc('length', () => obj.len());
      if (name === 'normalized') return new NativeFunc('normalized', () => obj.clone().norm());
      if (name === 'distance_to') return new NativeFunc('distance_to', (a) => obj.distanceTo(a[0] as Vec3));
      if (name === 'dot') return new NativeFunc('dot', (a) => obj.dot(a[0] as Vec3));
      if (name === 'cross') return new NativeFunc('cross', (a) => obj.crossVec(a[0] as Vec3));
      if (name === 'lerp') return new NativeFunc('lerp', (a) => obj.clone().lerp(obj, a[0] as Vec3, Number(a[1])));
      throw new GOniRuntimeError(`Vec3 não tem membro "${name}"`, line);
    }
    // Instância de classe script
    if (obj instanceof Instance) {
      if (obj.fields.has(name)) return obj.fields.get(name);
      const method = obj.cls.findMethod(name);
      if (method) {
        return new NativeFunc(name, (args) => {
          const callEnv = new Environment(obj.fields);
          callEnv.define('self', obj);
          this.bindParams(method, args, callEnv);
          return this.callFunctionWithEnv(method, callEnv);
        });
      }
      throw new GOniRuntimeError(`"${obj.cls.name}" não tem membro "${name}"`, line);
    }
    // Classe → .new()
    if (obj instanceof ClassValue) {
      if (name === 'new') {
        return new NativeFunc('new', (args) => this.instantiate(obj, args));
      }
      const method = obj.findMethod(name);
      if (method) return method;
      throw new GOniRuntimeError(`classe "${obj.name}" não tem membro "${name}"`, line);
    }
    // Objeto da engine (GOniObject etc.)
    if (this.objectBridge?.isObject(obj)) {
      const v = this.objectBridge.get(obj, name);
      if (v !== undefined) return v;
      // método: retorna função nativa que chama a bridge
      return new NativeFunc(name, (args) => this.objectBridge!.call(obj, name, args, line));
    }
    // dict
    if (obj instanceof Map) {
      if (obj.has(name)) return obj.get(name);
      if (name === 'keys') return new NativeFunc('keys', () => [...obj.keys()]);
      if (name === 'values') return new NativeFunc('values', () => [...obj.values()]);
      if (name === 'has') return new NativeFunc('has', (a) => obj.has(a[0]));
      throw new GOniRuntimeError(`dicionário não tem chave "${name}"`, line);
    }
    // list
    if (Array.isArray(obj)) {
      if (name === 'length') return new NativeFunc('length', () => obj.length);
      if (name === 'push') return new NativeFunc('push', (a) => { obj.push(a[0]); return obj.length; });
      if (name === 'pop') return new NativeFunc('pop', () => obj.pop() ?? null);
      if (name === 'contains') return new NativeFunc('contains', (a) => obj.includes(a[0]));
      if (name === 'join') return new NativeFunc('join', (a) => obj.join(String(a[0] ?? ',')));
      if (name === 'size') return obj.length;
      throw new GOniRuntimeError(`lista não tem método "${name}"`, line);
    }
    // string
    if (typeof obj === 'string') {
      if (name === 'length') return new NativeFunc('length', () => obj.length);
      if (name === 'upper') return new NativeFunc('upper', () => obj.toUpperCase());
      if (name === 'lower') return new NativeFunc('lower', () => obj.toLowerCase());
      if (name === 'split') return new NativeFunc('split', (a) => obj.split(String(a[0] ?? ' ')));
      if (name === 'contains') return new NativeFunc('contains', (a) => obj.includes(String(a[0])));
      throw new GOniRuntimeError(`string não tem método "${name}"`, line);
    }
    throw new GOniRuntimeError(`valor de tipo ${typeName(obj)} não tem membro "${name}"`, line);
  }

  memberSet(obj: unknown, name: string, value: unknown, line: number): void {
    if (obj instanceof Vec3) {
      if (name === 'x') { obj.x = Number(value); return; }
      if (name === 'y') { obj.y = Number(value); return; }
      if (name === 'z') { obj.z = Number(value); return; }
      throw new GOniRuntimeError(`Vec3 não tem campo gravável "${name}"`, line);
    }
    if (obj instanceof Instance) {
      if (obj.fields.has(name)) {
        obj.fields.set(name, value, line);
        return;
      }
      obj.fields.define(name, value);
      return;
    }
    if (this.objectBridge?.isObject(obj)) {
      if (this.objectBridge.set(obj, name, value)) return;
      throw new GOniRuntimeError(`objeto não aceita propriedade "${name}"`, line);
    }
    if (obj instanceof Map) {
      obj.set(name, value);
      return;
    }
    throw new GOniRuntimeError(`valor de tipo ${typeName(obj)} não aceita atribuição de membro`, line);
  }

  /** Executa função com ambiente pré-construído (métodos de instância). */
  private async callFunctionWithEnv(fn: FuncValue, callEnv: Environment): Promise<unknown> {
    this.budget = Interpreter.MAX_STATEMENTS;
    try {
      await this.execBlock(fn.body, callEnv);
    } catch (e) {
      if (e instanceof ReturnSignal) return e.value;
      throw e;
    }
    return null;
  }

  /** Resolve $Caminho (nó da cena). Implementado via bridge/get_node. */
  private resolveNodeRef(path: string, env: Environment, line: number): unknown {
    if (!env.has('self')) {
      // usa get_node global se existir
      if (this.globals.has('get_node')) {
        const fn = this.globals.get('get_node');
        if (fn instanceof NativeFunc) return fn.fn([path], env);
      }
      throw new GOniRuntimeError('não é possível resolver $nó fora de um script de objeto', line);
    }
    const self = env.get('self');
    if (this.objectBridge?.isObject(self)) {
      return this.objectBridge.call(self, '__get_node', [path], line);
    }
    throw new GOniRuntimeError('self não é um objeto de cena', line);
  }
}

// ---------------- Operadores ----------------

export function truthy(v: unknown): boolean {
  if (v === null || v === undefined) return false;
  if (typeof v === 'boolean') return v;
  if (typeof v === 'number') return v !== 0;
  if (typeof v === 'string') return v.length > 0;
  if (Array.isArray(v)) return v.length > 0;
  if (v instanceof Vec3) return v.lenSq() > 0;
  return true;
}

export function equals(l: unknown, r: unknown): boolean {
  if (l instanceof Vec3 && r instanceof Vec3) return l.equals(r);
  if (Array.isArray(l) && Array.isArray(r)) {
    return l.length === r.length && l.every((v, i) => equals(v, r[i]));
  }
  return l === r;
}

function binop(op: string, l: unknown, r: unknown, line: number): unknown {
  switch (op) {
    case '+': {
      if (typeof l === 'number' && typeof r === 'number') return l + r;
      if (typeof l === 'string' || typeof r === 'string') return toStr(l) + toStr(r);
      if (l instanceof Vec3 && r instanceof Vec3) return l.clone().add(r);
      if (Array.isArray(l) && Array.isArray(r)) return [...l, ...r];
      throw new GOniRuntimeError(`tipos incompatíveis para +: ${typeName(l)} e ${typeName(r)}`, line);
    }
    case '-': {
      if (typeof l === 'number' && typeof r === 'number') return l - r;
      if (l instanceof Vec3 && r instanceof Vec3) return l.clone().sub(r);
      throw new GOniRuntimeError(`tipos incompatíveis para -: ${typeName(l)} e ${typeName(r)}`, line);
    }
    case '*': {
      if (typeof l === 'number' && typeof r === 'number') return l * r;
      if (l instanceof Vec3 && typeof r === 'number') return l.clone().mul(r);
      if (typeof l === 'number' && r instanceof Vec3) return r.clone().mul(l);
      throw new GOniRuntimeError(`tipos incompatíveis para *: ${typeName(l)} e ${typeName(r)}`, line);
    }
    case '/': {
      if (typeof l === 'number' && typeof r === 'number') {
        if (r === 0) throw new GOniRuntimeError('divisão por zero', line);
        return l / r;
      }
      if (l instanceof Vec3 && typeof r === 'number') {
        if (r === 0) throw new GOniRuntimeError('divisão por zero', line);
        return l.clone().mul(1 / r);
      }
      throw new GOniRuntimeError(`tipos incompatíveis para /: ${typeName(l)} e ${typeName(r)}`, line);
    }
    case '%': {
      if (typeof l === 'number' && typeof r === 'number') {
        if (r === 0) throw new GOniRuntimeError('módulo por zero', line);
        return l % r;
      }
      throw new GOniRuntimeError('% exige números', line);
    }
    case '==': return equals(l, r);
    case '!=': return !equals(l, r);
    case '<': return compare(l, r, '<', line);
    case '>': return compare(l, r, '>', line);
    case '<=': return compare(l, r, '<=', line);
    case '>=': return compare(l, r, '>=', line);
    case 'in': {
      if (Array.isArray(r)) return r.some((v) => equals(v, l));
      if (r instanceof Map) return r.has(l);
      if (typeof r === 'string') return r.includes(toStr(l));
      throw new GOniRuntimeError('"in" exige lista, dicionário ou string', line);
    }
    default:
      throw new GOniRuntimeError(`operador desconhecido: ${op}`, line);
  }
}

function compare(l: unknown, r: unknown, op: string, line: number): boolean {
  if (typeof l === 'number' && typeof r === 'number') {
    switch (op) {
      case '<': return l < r;
      case '>': return l > r;
      case '<=': return l <= r;
      case '>=': return l >= r;
    }
  }
  if (typeof l === 'string' && typeof r === 'string') {
    switch (op) {
      case '<': return l < r;
      case '>': return l > r;
      case '<=': return l <= r;
      case '>=': return l >= r;
    }
  }
  throw new GOniRuntimeError(`comparação ${op} exige números ou strings`, line);
}

function applyOp(op: string, cur: unknown, value: unknown, line: number): unknown {
  switch (op) {
    case '+=': return binop('+', cur, value, line);
    case '-=': return binop('-', cur, value, line);
    case '*=': return binop('*', cur, value, line);
    case '/=': return binop('/', cur, value, line);
    default: throw new GOniRuntimeError(`operador de atribuição desconhecido: ${op}`, line);
  }
}

function indexGet(obj: unknown, idx: unknown, line: number): unknown {
  if (Array.isArray(obj)) {
    const i = Math.trunc(Number(idx));
    if (i < 0 || i >= obj.length) return null;
    return obj[i];
  }
  if (obj instanceof Map) return obj.has(idx) ? obj.get(idx) : null;
  if (typeof obj === 'string') {
    const i = Math.trunc(Number(idx));
    if (i < 0 || i >= obj.length) return null;
    return obj[i];
  }
  if (obj instanceof Vec3) {
    const i = Math.trunc(Number(idx));
    return [obj.x, obj.y, obj.z][i] ?? null;
  }
  throw new GOniRuntimeError('valor não é indexável', line);
}

function toIterable(v: unknown, line: number): unknown[] {
  if (Array.isArray(v)) return v;
  if (v instanceof Map) return [...v.keys()];
  if (typeof v === 'string') return v.split('');
  if (v === null || v === undefined) return [];
  throw new GOniRuntimeError(`valor de tipo ${typeName(v)} não é iterável`, line);
}

export function toStr(v: unknown): string {
  if (v === null || v === undefined) return 'null';
  if (typeof v === 'string') return v;
  if (typeof v === 'number') {
    return Number.isInteger(v) ? String(v) : String(Math.round(v * 1e6) / 1e6);
  }
  if (typeof v === 'boolean') return v ? 'true' : 'false';
  if (v instanceof Vec3) return v.toString();
  if (Array.isArray(v)) return `[${v.map(toStr).join(', ')}]`;
  if (v instanceof Map) {
    return `{${[...v.entries()].map(([k, val]) => `${toStr(k)}: ${toStr(val)}`).join(', ')}}`;
  }
  if (v instanceof FuncValue) return `<func ${v.name}>`;
  if (v instanceof NativeFunc) return `<nativa ${v.name}>`;
  if (v instanceof ClassValue) return `<classe ${v.name}>`;
  if (v instanceof Instance) return `<${v.cls.name}>`;
  if (v && typeof v === 'object' && 'name' in (v as object)) return String((v as { name: unknown }).name);
  return String(v);
}

export function typeName(v: unknown): string {
  if (v === null) return 'null';
  if (v === undefined) return 'null';
  if (typeof v === 'number') return 'número';
  if (typeof v === 'string') return 'string';
  if (typeof v === 'boolean') return 'bool';
  if (v instanceof Vec3) return 'vec3';
  if (Array.isArray(v)) return 'lista';
  if (v instanceof Map) return 'dicionário';
  if (v instanceof FuncValue || v instanceof NativeFunc) return 'função';
  if (v instanceof ClassValue) return 'classe';
  if (v instanceof Instance) return 'instância';
  return typeof v;
}

export { MathUtils };
