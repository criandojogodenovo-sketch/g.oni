/**
 * G.oni Llumni — Wrapper de shader WebGL2
 * Compilação + cache de uniforms + helpers de tipo.
 */

export class ShaderError extends Error {
  constructor(public readonly kind: 'vertex' | 'fragment' | 'program', public readonly log: string) {
    super(`Falha ao compilar shader (${kind}):\n${log}`);
  }
}

export class Shader {
  readonly program: WebGLProgram;
  private locCache = new Map<string, WebGLUniformLocation | null>();
  private gl: WebGL2RenderingContext;

  constructor(gl: WebGL2RenderingContext, vsSrc: string, fsSrc: string) {
    this.gl = gl;
    const vs = Shader.compile(gl, gl.VERTEX_SHADER, vsSrc, 'vertex');
    const fs = Shader.compile(gl, gl.FRAGMENT_SHADER, fsSrc, 'fragment');
    const prog = gl.createProgram()!;
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      throw new ShaderError('program', gl.getProgramInfoLog(prog) ?? 'link desconhecido');
    }
    gl.deleteShader(vs);
    gl.deleteShader(fs);
    this.program = prog;
  }

  private static compile(gl: WebGL2RenderingContext, type: number, src: string, kind: 'vertex' | 'fragment'): WebGLShader {
    const sh = gl.createShader(type)!;
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      const log = gl.getShaderInfoLog(sh) ?? 'erro desconhecido';
      gl.deleteShader(sh);
      throw new ShaderError(kind, log);
    }
    return sh;
  }

  use(): void {
    this.gl.useProgram(this.program);
  }

  loc(name: string): WebGLUniformLocation | null {
    if (!this.locCache.has(name)) {
      this.locCache.set(name, this.gl.getUniformLocation(this.program, name));
    }
    return this.locCache.get(name) ?? null;
  }

  setMat4(name: string, m: Float32Array | ArrayLike<number>): void {
    const l = this.loc(name);
    if (l) this.gl.uniformMatrix4fv(l, false, m as Float32Array);
  }
  setMat3(name: string, m: Float32Array | ArrayLike<number>): void {
    const l = this.loc(name);
    if (l) this.gl.uniformMatrix3fv(l, false, m as Float32Array);
  }
  setVec3(name: string, x: number, y: number, z: number): void {
    const l = this.loc(name);
    if (l) this.gl.uniform3f(l, x, y, z);
  }
  setVec2(name: string, x: number, y: number): void {
    const l = this.loc(name);
    if (l) this.gl.uniform2f(l, x, y);
  }
  setVec3Arr(name: string, arr: Float32Array | number[]): void {
    const l = this.loc(name);
    if (l) this.gl.uniform3fv(l, arr as Float32Array);
  }
  setFloat(name: string, v: number): void {
    const l = this.loc(name);
    if (l) this.gl.uniform1f(l, v);
  }
  setFloatArr(name: string, arr: Float32Array | number[]): void {
    const l = this.loc(name);
    if (l) this.gl.uniform1fv(l, arr as Float32Array);
  }
  setInt(name: string, v: number): void {
    const l = this.loc(name);
    if (l) this.gl.uniform1i(l, v);
  }
  setBool(name: string, v: boolean): void {
    this.setInt(name, v ? 1 : 0);
  }

  /** Vincula uma textura a uma unidade e define o uniform da unidade. */
  setTexture(name: string, texture: WebGLTexture | null, unit: number, target = 0): void {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    this.setInt(name, unit);
    void target;
  }

  dispose(): void {
    this.gl.deleteProgram(this.program);
    this.locCache.clear();
  }
}
