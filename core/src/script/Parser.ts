/**
 * G.oni Llumni — Parser do G.oni Script
 * Constrói a AST a partir dos tokens (blocos por indentação).
 */

import { Tok, Token, tokenize } from './Lexer';

export type Node =
  | { kind: 'program'; body: Node[] }
  | { kind: 'vardecl'; name: string; value: Node | null; isConst: boolean; line: number }
  | { kind: 'funcdecl'; name: string; params: { name: string; def: Node | null }[]; body: Node[]; line: number }
  | { kind: 'classdecl'; name: string; parent: string | null; body: Node[]; line: number }
  | { kind: 'signaldecl'; name: string; line: number }
  | { kind: 'if'; cond: Node; then: Node[]; elifs: { cond: Node; body: Node[] }[]; else: Node[] | null; line: number }
  | { kind: 'while'; cond: Node; body: Node[]; line: number }
  | { kind: 'for'; varName: string; iter: Node; body: Node[]; line: number }
  | { kind: 'return'; value: Node | null; line: number }
  | { kind: 'break'; line: number }
  | { kind: 'continue'; line: number }
  | { kind: 'pass'; line: number }
  | { kind: 'exprstmt'; expr: Node; line: number }
  | { kind: 'await'; expr: Node; line: number }
  | { kind: 'assign'; target: Node; op: string; value: Node; line: number }
  // expressões
  | { kind: 'num'; value: number; line: number }
  | { kind: 'str'; value: string; line: number }
  | { kind: 'bool'; value: boolean; line: number }
  | { kind: 'null'; line: number }
  | { kind: 'ident'; name: string; line: number }
  | { kind: 'noderef'; path: string; line: number }
  | { kind: 'self'; line: number }
  | { kind: 'binop'; op: string; left: Node; right: Node; line: number }
  | { kind: 'unary'; op: string; operand: Node; line: number }
  | { kind: 'call'; callee: Node; args: Node[]; line: number }
  | { kind: 'member'; obj: Node; name: string; line: number }
  | { kind: 'index'; obj: Node; index: Node; line: number }
  | { kind: 'list'; items: Node[]; line: number }
  | { kind: 'dict'; entries: { key: Node; value: Node }[]; line: number }
  | { kind: 'lambda'; params: string[]; body: Node[]; line: number };

export class ParseError extends Error {
  constructor(msg: string, public line: number) {
    super(`[G.oni Script] Erro de sintaxe (linha ${line}): ${msg}`);
  }
}

export function parse(src: string): Node {
  const tokens = tokenize(src);
  const parser = new Parser(tokens);
  return parser.parseProgram();
}

class Parser {
  private pos = 0;

  constructor(private tokens: Token[]) {}

  private peek(offset = 0): Token {
    return this.tokens[Math.min(this.pos + offset, this.tokens.length - 1)];
  }

  private next(): Token {
    const t = this.tokens[this.pos];
    if (this.pos < this.tokens.length - 1) this.pos++;
    return t;
  }

  private check(type: Tok): boolean { return this.peek().type === type; }

  private match(type: Tok): Token | null {
    if (this.check(type)) return this.next();
    return null;
  }

  private expect(type: Tok, msg?: string): Token {
    if (this.check(type)) return this.next();
    const got = this.peek();
    throw new ParseError(`${msg ?? `esperado ${Tok[type]}`}, encontrado ${got.value || Tok[got.type]}`, got.line);
  }

  private skipNewlines(): void {
    while (this.check(Tok.NEWLINE)) this.next();
  }

  parseProgram(): Node {
    const body: Node[] = [];
    this.skipNewlines();
    while (!this.check(Tok.EOF)) {
      body.push(this.statement());
      this.skipNewlines();
    }
    return { kind: 'program', body };
  }

  // ---------------- Statements ----------------

  private statement(): Node {
    const t = this.peek();
    switch (t.type) {
      case Tok.VAR: return this.varDecl(false);
      case Tok.CONST: return this.varDecl(true);
      case Tok.FUNC: return this.funcDecl();
      case Tok.CLASS: return this.classDecl();
      case Tok.SIGNAL: return this.signalDecl();
      case Tok.IF: return this.ifStmt();
      case Tok.WHILE: return this.whileStmt();
      case Tok.FOR: return this.forStmt();
      case Tok.RETURN: {
        this.next();
        let value: Node | null = null;
        if (!this.check(Tok.NEWLINE) && !this.check(Tok.EOF) && !this.check(Tok.DEDENT)) {
          value = this.expression();
        }
        return { kind: 'return', value, line: t.line };
      }
      case Tok.BREAK: this.next(); return { kind: 'break', line: t.line };
      case Tok.CONTINUE: this.next(); return { kind: 'continue', line: t.line };
      case Tok.PASS: this.next(); return { kind: 'pass', line: t.line };
      case Tok.AWAIT: {
        this.next();
        const expr = this.expression();
        return { kind: 'await', expr, line: t.line };
      }
      default: return this.exprOrAssign();
    }
  }

  private varDecl(isConst: boolean): Node {
    const kw = this.next();
    const name = this.expect(Tok.IDENT, 'esperado nome de variável').value;
    let value: Node | null = null;
    if (this.match(Tok.ASSIGN)) value = this.expression();
    return { kind: 'vardecl', name, value, isConst, line: kw.line };
  }

  private funcDecl(): Node {
    const kw = this.next();
    const name = this.expect(Tok.IDENT, 'esperado nome de função').value;
    this.expect(Tok.LPAREN, 'esperado (');
    const params: { name: string; def: Node | null }[] = [];
    if (!this.check(Tok.RPAREN)) {
      do {
        const pn = this.expect(Tok.IDENT, 'esperado nome de parâmetro').value;
        let def: Node | null = null;
        if (this.match(Tok.ASSIGN)) def = this.expression();
        params.push({ name: pn, def });
      } while (this.match(Tok.COMMA));
    }
    this.expect(Tok.RPAREN, 'esperado )');
    this.expect(Tok.COLON, 'esperado : após assinatura da função');
    const body = this.blockOrInline();
    return { kind: 'funcdecl', name, params, body, line: kw.line };
  }

  private classDecl(): Node {
    const kw = this.next();
    const name = this.expect(Tok.IDENT, 'esperado nome de classe').value;
    let parent: string | null = null;
    if (this.match(Tok.EXTENDS)) {
      parent = this.expect(Tok.IDENT, 'esperado nome da classe base').value;
    }
    this.expect(Tok.COLON, 'esperado : após cabeçalho da classe');
    const body = this.blockOrInline();
    return { kind: 'classdecl', name, parent, body, line: kw.line };
  }

  private signalDecl(): Node {
    const kw = this.next();
    const name = this.expect(Tok.IDENT, 'esperado nome do sinal').value;
    return { kind: 'signaldecl', name, line: kw.line };
  }

  private ifStmt(): Node {
    const kw = this.next();
    const cond = this.expression();
    this.expect(Tok.COLON, 'esperado : após condição');
    const then = this.blockOrInline();
    const elifs: { cond: Node; body: Node[] }[] = [];
    let elseBody: Node[] | null = null;

    // nota: elif/else vêm após NEWLINE+DEDENT — o lexer já emitiu
    while (this.check(Tok.ELIF)) {
      this.next();
      const c = this.expression();
      this.expect(Tok.COLON, 'esperado : após condição elif');
      elifs.push({ cond: c, body: this.blockOrInline() });
    }
    if (this.check(Tok.ELSE)) {
      this.next();
      this.expect(Tok.COLON, 'esperado : após else');
      elseBody = this.blockOrInline();
    }
    return { kind: 'if', cond, then, elifs, else: elseBody, line: kw.line };
  }

  private whileStmt(): Node {
    const kw = this.next();
    const cond = this.expression();
    this.expect(Tok.COLON, 'esperado : após condição');
    const body = this.blockOrInline();
    return { kind: 'while', cond, body, line: kw.line };
  }

  private forStmt(): Node {
    const kw = this.next();
    const varName = this.expect(Tok.IDENT, 'esperado nome da variável do laço').value;
    this.expect(Tok.IN, 'esperado "in"');
    const iter = this.expression();
    this.expect(Tok.COLON, 'esperado : após iteração');
    const body = this.blockOrInline();
    return { kind: 'for', varName, iter, body, line: kw.line };
  }

  /** Bloco: `:` NEWLINE INDENT stmts DEDENT, ou `:` stmt na mesma linha. */
  private blockOrInline(): Node[] {
    this.skipNewlines();
    if (this.check(Tok.INDENT)) {
      this.next();
      const body: Node[] = [];
      this.skipNewlines();
      while (!this.check(Tok.DEDENT) && !this.check(Tok.EOF)) {
        body.push(this.statement());
        this.skipNewlines();
      }
      this.match(Tok.DEDENT);
      return body;
    }
    // inline: uma única instrução
    if (this.check(Tok.EOF) || this.check(Tok.DEDENT)) return [];
    return [this.statement()];
  }

  private exprOrAssign(): Node {
    const line = this.peek().line;
    const expr = this.expression();
    const ops: Record<string, string> = {
      [Tok.ASSIGN]: '=', [Tok.PLUS_EQ]: '+=', [Tok.MINUS_EQ]: '-=',
      [Tok.STAR_EQ]: '*=', [Tok.SLASH_EQ]: '/=',
    };
    const t = this.peek();
    if (t.type in ops) {
      this.next();
      const value = this.expression();
      if (expr.kind !== 'ident' && expr.kind !== 'member' && expr.kind !== 'index' && expr.kind !== 'noderef') {
        throw new ParseError('alvo de atribuição inválido', line);
      }
      return { kind: 'assign', target: expr, op: ops[t.type], value, line };
    }
    return { kind: 'exprstmt', expr, line };
  }

  // ---------------- Expressions ----------------

  private expression(): Node {
    return this.orExpr();
  }

  private orExpr(): Node {
    let left = this.andExpr();
    while (this.check(Tok.OR)) {
      const t = this.next();
      const right = this.andExpr();
      left = { kind: 'binop', op: 'or', left, right, line: t.line };
    }
    return left;
  }

  private andExpr(): Node {
    let left = this.notExpr();
    while (this.check(Tok.AND)) {
      const t = this.next();
      const right = this.notExpr();
      left = { kind: 'binop', op: 'and', left, right, line: t.line };
    }
    return left;
  }

  private notExpr(): Node {
    if (this.check(Tok.NOT)) {
      const t = this.next();
      return { kind: 'unary', op: 'not', operand: this.notExpr(), line: t.line };
    }
    return this.comparison();
  }

  private comparison(): Node {
    let left = this.additive();
    const ops: Tok[] = [Tok.EQ, Tok.NEQ, Tok.LT, Tok.GT, Tok.LTE, Tok.GTE];
    while (ops.some((o) => this.check(o))) {
      const t = this.next();
      const right = this.additive();
      const opMap: Record<number, string> = {
        [Tok.EQ]: '==', [Tok.NEQ]: '!=', [Tok.LT]: '<', [Tok.GT]: '>', [Tok.LTE]: '<=', [Tok.GTE]: '>=',
      };
      left = { kind: 'binop', op: opMap[t.type], left, right, line: t.line };
    }
    if (this.check(Tok.IN)) {
      const t = this.next();
      const right = this.additive();
      left = { kind: 'binop', op: 'in', left, right, line: t.line };
    }
    return left;
  }

  private additive(): Node {
    let left = this.multiplicative();
    while (this.check(Tok.PLUS) || this.check(Tok.MINUS)) {
      const t = this.next();
      const right = this.multiplicative();
      left = { kind: 'binop', op: t.type === Tok.PLUS ? '+' : '-', left, right, line: t.line };
    }
    return left;
  }

  private multiplicative(): Node {
    let left = this.unary();
    while (this.check(Tok.STAR) || this.check(Tok.SLASH) || this.check(Tok.PERCENT)) {
      const t = this.next();
      const right = this.unary();
      const op = t.type === Tok.STAR ? '*' : t.type === Tok.SLASH ? '/' : '%';
      left = { kind: 'binop', op, left, right, line: t.line };
    }
    return left;
  }

  private unary(): Node {
    if (this.check(Tok.MINUS)) {
      const t = this.next();
      return { kind: 'unary', op: '-', operand: this.unary(), line: t.line };
    }
    if (this.check(Tok.NOT)) {
      const t = this.next();
      return { kind: 'unary', op: 'not', operand: this.unary(), line: t.line };
    }
    return this.postfix();
  }

  private postfix(): Node {
    let expr = this.primary();
    for (;;) {
      if (this.check(Tok.DOT)) {
        const t = this.next();
        const name = this.expect(Tok.IDENT, 'esperado nome de membro').value;
        expr = { kind: 'member', obj: expr, name, line: t.line };
      } else if (this.check(Tok.LPAREN)) {
        const t = this.next();
        const args: Node[] = [];
        this.skipNewlines();
        if (!this.check(Tok.RPAREN)) {
          do {
            this.skipNewlines();
            args.push(this.expression());
            this.skipNewlines();
          } while (this.match(Tok.COMMA));
        }
        this.expect(Tok.RPAREN, 'esperado )');
        expr = { kind: 'call', callee: expr, args, line: t.line };
      } else if (this.check(Tok.LBRACKET)) {
        const t = this.next();
        const index = this.expression();
        this.expect(Tok.RBRACKET, 'esperado ]');
        expr = { kind: 'index', obj: expr, index, line: t.line };
      } else break;
    }
    return expr;
  }

  private primary(): Node {
    const t = this.peek();
    switch (t.type) {
      case Tok.NUMBER: this.next(); return { kind: 'num', value: t.num ?? 0, line: t.line };
      case Tok.STRING: this.next(); return { kind: 'str', value: t.value, line: t.line };
      case Tok.TRUE: this.next(); return { kind: 'bool', value: true, line: t.line };
      case Tok.FALSE: this.next(); return { kind: 'bool', value: false, line: t.line };
      case Tok.NULL: this.next(); return { kind: 'null', line: t.line };
      case Tok.SELF: this.next(); return { kind: 'self', line: t.line };
      case Tok.DOLLAR: this.next(); return { kind: 'noderef', path: t.value, line: t.line };
      case Tok.IDENT: this.next(); return { kind: 'ident', name: t.value, line: t.line };
      case Tok.LPAREN: {
        this.next();
        const e = this.expression();
        this.expect(Tok.RPAREN, 'esperado )');
        return e;
      }
      case Tok.LBRACKET: {
        this.next();
        const items: Node[] = [];
        this.skipNewlines();
        if (!this.check(Tok.RBRACKET)) {
          do {
            this.skipNewlines();
            items.push(this.expression());
            this.skipNewlines();
          } while (this.match(Tok.COMMA));
        }
        this.expect(Tok.RBRACKET, 'esperado ]');
        return { kind: 'list', items, line: t.line };
      }
      case Tok.LBRACE: {
        this.next();
        const entries: { key: Node; value: Node }[] = [];
        this.skipNewlines();
        if (!this.check(Tok.RBRACE)) {
          do {
            this.skipNewlines();
            const key = this.expression();
            this.expect(Tok.COLON, 'esperado : em dicionário');
            const value = this.expression();
            entries.push({ key, value });
            this.skipNewlines();
          } while (this.match(Tok.COMMA));
        }
        this.expect(Tok.RBRACE, 'esperado }');
        return { kind: 'dict', entries, line: t.line };
      }
      case Tok.FUNC: {
        // expressão lambda: func(a, b): expressão
        this.next();
        const params: string[] = [];
        this.expect(Tok.LPAREN);
        if (!this.check(Tok.RPAREN)) {
          do {
            params.push(this.expect(Tok.IDENT, 'esperado nome de parâmetro').value);
          } while (this.match(Tok.COMMA));
        }
        this.expect(Tok.RPAREN);
        this.expect(Tok.COLON);
        const body = [this.statement()];
        return { kind: 'lambda', params, body, line: t.line };
      }
      default:
        throw new ParseError(`expressão inesperada: ${t.value || Tok[t.type]}`, t.line);
    }
  }
}
