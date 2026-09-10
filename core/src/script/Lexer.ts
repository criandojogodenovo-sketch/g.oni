/**
 * G.oni Llumni — Lexer do G.oni Script
 *
 * Linguagem inspirada em GDScript:
 *   - blocos por indentação (INDENT/DEDENT)
 *   - comentários com #
 *   - strings 'simples' e "duplas"
 *   - `$CaminhoDoNo` para referenciar objetos da cena
 */

export enum Tok {
  EOF, NEWLINE, INDENT, DEDENT,
  IDENT, NUMBER, STRING,
  // keywords
  VAR, CONST, FUNC, CLASS, SIGNAL, IF, ELIF, ELSE, WHILE, FOR, IN,
  RETURN, BREAK, CONTINUE, AWAIT, AND, OR, NOT, TRUE, FALSE, NULL,
  SELF, PASS, EXTENDS,
  // operadores / pontuação
  PLUS, MINUS, STAR, SLASH, PERCENT,
  ASSIGN, PLUS_EQ, MINUS_EQ, STAR_EQ, SLASH_EQ,
  EQ, NEQ, LT, GT, LTE, GTE,
  LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
  COMMA, COLON, DOT, DOLLAR,
}

export interface Token {
  type: Tok;
  value: string;
  line: number;
  num?: number;
}

const KEYWORDS: Record<string, Tok> = {
  var: Tok.VAR, const: Tok.CONST, func: Tok.FUNC, class: Tok.CLASS, signal: Tok.SIGNAL,
  if: Tok.IF, elif: Tok.ELIF, else: Tok.ELSE, while: Tok.WHILE, for: Tok.FOR, in: Tok.IN,
  return: Tok.RETURN, break: Tok.BREAK, continue: Tok.CONTINUE, await: Tok.AWAIT,
  and: Tok.AND, or: Tok.OR, not: Tok.NOT,
  true: Tok.TRUE, false: Tok.FALSE, null: Tok.NULL, self: Tok.SELF, pass: Tok.PASS,
  extends: Tok.EXTENDS,
};

export class LexError extends Error {
  constructor(msg: string, public line: number) {
    super(`[G.oni Script] Erro léxico (linha ${line}): ${msg}`);
  }
}

export function tokenize(src: string): Token[] {
  const tokens: Token[] = [];
  let i = 0;
  let line = 1;
  const n = src.length;

  const indents: number[] = [0];
  let atLineStart = true;
  let parenDepth = 0;

  const emit = (type: Tok, value = '', l = line, num?: number) => {
    tokens.push({ type, value, line: l, num });
  };

  while (i < n) {
    const ch = src[i];

    // ---- controle de linha / indentação ----
    if (ch === '\n') {
      if (parenDepth === 0) {
        // emitir NEWLINE apenas se a linha teve conteúdo
        const last = tokens[tokens.length - 1];
        if (last && last.type !== Tok.NEWLINE && last.type !== Tok.INDENT && last.type !== Tok.DEDENT) {
          emit(Tok.NEWLINE, '', line);
        }
      }
      line++;
      i++;
      atLineStart = true;
      continue;
    }

    if (ch === ' ' || ch === '\t' || ch === '\r') {
      i++;
      continue;
    }

    if (atLineStart) {
      atLineStart = false;
      // medir indentação (tabs = 4)
      let indent = 0;
      let j = i;
      while (j < n) {
        if (src[j] === ' ') { indent++; j++; }
        else if (src[j] === '\t') { indent += 4; j++; }
        else break;
      }
      if (j < n && src[j] !== '\n' && src[j] !== '#') {
        // linha com conteúdo: processa indentação
        if (indent > indents[indents.length - 1]) {
          indents.push(indent);
          emit(Tok.INDENT, '', line);
        } else {
          while (indent < indents[indents.length - 1]) {
            indents.pop();
            emit(Tok.DEDENT, '', line);
          }
          if (indent !== indents[indents.length - 1]) {
            throw new LexError(`indentação inconsistente (esperada ${indents[indents.length - 1]}, encontrada ${indent})`, line);
          }
        }
      } else if (j >= n) {
        // fim do arquivo em linha em branco
        break;
      }
      if (src[j] === '#') {
        // comentário logo após indentação: pula comentário
        while (j < n && src[j] !== '\n') j++;
        i = j;
        atLineStart = true;
        continue;
      }
      continue;
    }

    // ---- comentário ----
    if (ch === '#') {
      while (i < n && src[i] !== '\n') i++;
      continue;
    }

    // ---- continuação de linha com \ ----
    if (ch === '\\' && src[i + 1] === '\n') {
      i += 2;
      line++;
      continue;
    }

    // ---- strings ----
    if (ch === '"' || ch === "'") {
      const quote = ch;
      let s = '';
      i++;
      while (i < n && src[i] !== quote) {
        if (src[i] === '\n') throw new LexError('string não terminada na mesma linha', line);
        if (src[i] === '\\') {
          i++;
          const esc = src[i];
          s += esc === 'n' ? '\n' : esc === 't' ? '\t' : esc === '\\' ? '\\' : esc === '"' ? '"' : esc === "'" ? "'" : esc;
          i++;
        } else {
          s += src[i++];
        }
      }
      if (i >= n) throw new LexError('string não terminada', line);
      i++;
      emit(Tok.STRING, s, line);
      continue;
    }

    // ---- números ----
    if (/[0-9]/.test(ch) || (ch === '.' && /[0-9]/.test(src[i + 1] ?? ''))) {
      let s = '';
      let sawDot = false;
      while (i < n && (/[0-9]/.test(src[i]) || (src[i] === '.' && !sawDot && /[0-9]/.test(src[i + 1] ?? '')))) {
        if (src[i] === '.') sawDot = true;
        s += src[i++];
      }
      // notação exponencial
      if (i < n && (src[i] === 'e' || src[i] === 'E') && /[0-9+\-]/.test(src[i + 1] ?? '')) {
        s += src[i++];
        if (src[i] === '+' || src[i] === '-') s += src[i++];
        while (i < n && /[0-9]/.test(src[i])) s += src[i++];
      }
      emit(Tok.NUMBER, s, line, parseFloat(s));
      continue;
    }

    // ---- identificadores / keywords ----
    if (/[A-Za-z_]/.test(ch)) {
      let s = '';
      while (i < n && /[A-Za-z0-9_]/.test(src[i])) s += src[i++];
      const kw = KEYWORDS[s];
      if (kw) emit(kw, s, line);
      else emit(Tok.IDENT, s, line);
      continue;
    }

    // ---- $Caminho ----
    if (ch === '$') {
      i++;
      let s = '';
      while (i < n && /[A-Za-z0-9_\/]/.test(src[i])) s += src[i++];
      if (!s) throw new LexError('esperado nome de nó após $', line);
      // guarda como STRING especial marcada; parser converte
      emit(Tok.DOLLAR, s, line);
      continue;
    }

    // ---- operadores ----
    const two = src.slice(i, i + 2);
    if (two === '==') { emit(Tok.EQ, two, line); i += 2; continue; }
    if (two === '!=') { emit(Tok.NEQ, two, line); i += 2; continue; }
    if (two === '<=') { emit(Tok.LTE, two, line); i += 2; continue; }
    if (two === '>=') { emit(Tok.GTE, two, line); i += 2; continue; }
    if (two === '+=') { emit(Tok.PLUS_EQ, two, line); i += 2; continue; }
    if (two === '-=') { emit(Tok.MINUS_EQ, two, line); i += 2; continue; }
    if (two === '*=') { emit(Tok.STAR_EQ, two, line); i += 2; continue; }
    if (two === '/=') { emit(Tok.SLASH_EQ, two, line); i += 2; continue; }

    const singles: Record<string, Tok> = {
      '+': Tok.PLUS, '-': Tok.MINUS, '*': Tok.STAR, '/': Tok.SLASH, '%': Tok.PERCENT,
      '=': Tok.ASSIGN, '<': Tok.LT, '>': Tok.GT,
      '(': Tok.LPAREN, ')': Tok.RPAREN, '[': Tok.LBRACKET, ']': Tok.RBRACKET,
      '{': Tok.LBRACE, '}': Tok.RBRACE, ',': Tok.COMMA, ':': Tok.COLON, '.': Tok.DOT,
    };
    if (singles[ch]) {
      if (ch === '(' || ch === '[' || ch === '{') parenDepth++;
      if (ch === ')' || ch === ']' || ch === '}') parenDepth = Math.max(0, parenDepth - 1);
      emit(singles[ch], ch, line);
      i++;
      continue;
    }

    throw new LexError(`caractere inesperado "${ch}"`, line);
  }

  // NEWLINE final + DEDENTs
  const last = tokens[tokens.length - 1];
  if (last && last.type !== Tok.NEWLINE) emit(Tok.NEWLINE, '', line);
  while (indents.length > 1) {
    indents.pop();
    emit(Tok.DEDENT, '', line);
  }
  emit(Tok.EOF, '', line);
  return tokens;
}
