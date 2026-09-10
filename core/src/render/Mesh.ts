/**
 * G.oni Llumni — Malhas
 * MeshData: geometria na CPU (editável pelo editor de modelagem).
 * GpuMesh: buffers na GPU (VAO/VBO/IBO).
 */

export class MeshData {
  positions: number[] = [];
  normals: number[] = [];
  uvs: number[] = [];
  indices: number[] = [];

  constructor(positions?: number[], normals?: number[], uvs?: number[], indices?: number[]) {
    if (positions) this.positions = positions;
    if (normals) this.normals = normals;
    if (uvs) this.uvs = uvs;
    if (indices) this.indices = indices;
  }

  get vertexCount(): number { return this.positions.length / 3; }
  get triangleCount(): number { return this.indices.length / 3; }

  clone(): MeshData {
    return new MeshData(
      [...this.positions],
      [...this.normals],
      [...this.uvs],
      [...this.indices]
    );
  }

  /** Recalcula normais por face (flat) — adequado ao pipeline de edição. */
  computeNormals(): void {
    const n = this.vertexCount;
    this.normals = new Array(n * 3).fill(0);
    for (let i = 0; i < this.indices.length; i += 3) {
      const a = this.indices[i] * 3, b = this.indices[i + 1] * 3, c = this.indices[i + 2] * 3;
      const e1x = this.positions[b] - this.positions[a];
      const e1y = this.positions[b + 1] - this.positions[a + 1];
      const e1z = this.positions[b + 2] - this.positions[a + 2];
      const e2x = this.positions[c] - this.positions[a];
      const e2y = this.positions[c + 1] - this.positions[a + 1];
      const e2z = this.positions[c + 2] - this.positions[a + 2];
      const nx = e1y * e2z - e1z * e2y;
      const ny = e1z * e2x - e1x * e2z;
      const nz = e1x * e2y - e1y * e2x;
      for (const k of [a, b, c]) {
        this.normals[k] += nx;
        this.normals[k + 1] += ny;
        this.normals[k + 2] += nz;
      }
    }
    for (let i = 0; i < this.normals.length; i += 3) {
      const l = Math.hypot(this.normals[i], this.normals[i + 1], this.normals[i + 2]) || 1;
      this.normals[i] /= l; this.normals[i + 1] /= l; this.normals[i + 2] /= l;
    }
  }

  /** AABB local (min, max). */
  bounds(): { min: [number, number, number]; max: [number, number, number] } {
    const min: [number, number, number] = [Infinity, Infinity, Infinity];
    const max: [number, number, number] = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < this.positions.length; i += 3) {
      for (let k = 0; k < 3; k++) {
        const v = this.positions[i + k];
        if (v < min[k]) min[k] = v;
        if (v > max[k]) max[k] = v;
      }
    }
    if (min[0] === Infinity) return { min: [0, 0, 0], max: [0, 0, 0] };
    return { min, max };
  }

  /** Esfera envolvente local (centro + raio). */
  boundingSphere(): { cx: number; cy: number; cz: number; radius: number } {
    const { min, max } = this.bounds();
    const cx = (min[0] + max[0]) / 2, cy = (min[1] + max[1]) / 2, cz = (min[2] + max[2]) / 2;
    let r = 0;
    for (let i = 0; i < this.positions.length; i += 3) {
      r = Math.max(r, Math.hypot(this.positions[i] - cx, this.positions[i + 1] - cy, this.positions[i + 2] - cz));
    }
    return { cx, cy, cz, radius: r };
  }

  /**
   * Simplificação por agrupamento de vértices (vertex clustering).
   * Rápida, estável e adequada a LODs em mobile.
   * gridDivisions: número de células por eixo (menor = mais simples).
   */
  simplify(gridDivisions: number): MeshData {
    const { min, max } = this.bounds();
    const size = [max[0] - min[0] || 1e-6, max[1] - min[1] || 1e-6, max[2] - min[2] || 1e-6];
    const cell = size.map((s) => s / Math.max(1, gridDivisions));
    const map = new Map<string, number>();
    const newPos: number[] = [];
    const newUvs: number[] = [];
    const newIdx: number[] = [];
    const remap = new Array(this.vertexCount).fill(-1);

    for (let v = 0; v < this.vertexCount; v++) {
      const gx = Math.floor((this.positions[v * 3] - min[0]) / cell[0]);
      const gy = Math.floor((this.positions[v * 3 + 1] - min[1]) / cell[1]);
      const gz = Math.floor((this.positions[v * 3 + 2] - min[2]) / cell[2]);
      const key = `${gx}_${gy}_${gz}`;
      if (!map.has(key)) {
        map.set(key, newPos.length / 3);
        newPos.push(this.positions[v * 3], this.positions[v * 3 + 1], this.positions[v * 3 + 2]);
        newUvs.push(
          this.uvs[v * 2] ?? 0, this.uvs[v * 2 + 1] ?? 0
        );
      }
      remap[v] = map.get(key)!;
    }
    for (let i = 0; i < this.indices.length; i += 3) {
      const a = remap[this.indices[i]], b = remap[this.indices[i + 1]], c = remap[this.indices[i + 2]];
      if (a !== b && b !== c && a !== c) newIdx.push(a, b, c);
    }
    const out = new MeshData(newPos, [], newUvs, newIdx);
    out.computeNormals();
    return out;
  }

  serialize(): { p: number[]; n: number[]; u: number[]; i: number[] } {
    return { p: this.positions, n: this.normals, u: this.uvs, i: this.indices };
  }

  static deserialize(d: { p: number[]; n?: number[]; u?: number[]; i?: number[] }): MeshData {
    const m = new MeshData(d.p, d.n ?? [], d.u ?? [], d.i ?? []);
    if (!d.n || d.n.length !== d.p.length) m.computeNormals();
    return m;
  }
}

/** Buffers de GPU para desenho. */
export class GpuMesh {
  vao: WebGLVertexArrayObject;
  private vbo: WebGLBuffer;
  private ibo: WebGLBuffer;
  indexCount = 0;
  vertexCount = 0;
  /** Esfera envolvente no espaço do modelo (para frustum culling / picking). */
  bounding = { cx: 0, cy: 0, cz: 0, radius: 1 };

  private stride = 8 * 4; // pos(3) + normal(3) + uv(2), floats

  constructor(private gl: WebGL2RenderingContext) {
    this.vao = gl.createVertexArray()!;
    this.vbo = gl.createBuffer()!;
    this.ibo = gl.createBuffer()!;
  }

  upload(data: MeshData): void {
    const gl = this.gl;
    const vc = data.vertexCount;
    const interleaved = new Float32Array(vc * 8);
    for (let v = 0; v < vc; v++) {
      interleaved[v * 8 + 0] = data.positions[v * 3] ?? 0;
      interleaved[v * 8 + 1] = data.positions[v * 3 + 1] ?? 0;
      interleaved[v * 8 + 2] = data.positions[v * 3 + 2] ?? 0;
      interleaved[v * 8 + 3] = data.normals[v * 3] ?? 0;
      interleaved[v * 8 + 4] = data.normals[v * 3 + 1] ?? 0;
      interleaved[v * 8 + 5] = data.normals[v * 3 + 2] ?? 0;
      interleaved[v * 8 + 6] = data.uvs[v * 2] ?? 0;
      interleaved[v * 8 + 7] = data.uvs[v * 2 + 1] ?? 0;
    }

    gl.bindVertexArray(this.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    gl.bufferData(gl.ARRAY_BUFFER, interleaved, gl.DYNAMIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, this.stride, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, this.stride, 12);
    gl.enableVertexAttribArray(2);
    gl.vertexAttribPointer(2, 2, gl.FLOAT, false, this.stride, 24);

    const idx = data.indices.length > 65535 || data.vertexCount > 65535
      ? new Uint32Array(data.indices)
      : new Uint16Array(data.indices);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.ibo);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.DYNAMIC_DRAW);
    gl.bindVertexArray(null);

    this.indexCount = data.indices.length;
    this.vertexCount = vc;
    this.bounding = data.boundingSphere();
  }

  draw(): void {
    const gl = this.gl;
    gl.bindVertexArray(this.vao);
    if (this.indexCount > 0) {
      const type = this.vertexCount > 65535 ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
      gl.drawElements(gl.TRIANGLES, this.indexCount, type, 0);
    } else {
      gl.drawArrays(gl.TRIANGLES, 0, this.vertexCount);
    }
    gl.bindVertexArray(null);
  }

  dispose(): void {
    this.gl.deleteVertexArray(this.vao);
    this.gl.deleteBuffer(this.vbo);
    this.gl.deleteBuffer(this.ibo);
  }
}

/**
 * Registro central de malhas: compartilha GpuMesh entre objetos
 * (primitivas e assets) — crítico para reduzir draw calls no mobile.
 */
export class MeshRegistry {
  private meshes = new Map<string, { gpu: GpuMesh; data: MeshData }>();

  constructor(private gl: WebGL2RenderingContext) {}

  get(id: string): { gpu: GpuMesh; data: MeshData } | undefined {
    return this.meshes.get(id);
  }

  register(id: string, data: MeshData): GpuMesh {
    const existing = this.meshes.get(id);
    if (existing) {
      existing.gpu.upload(data);
      existing.data = data;
      return existing.gpu;
    }
    const gpu = new GpuMesh(this.gl);
    gpu.upload(data);
    this.meshes.set(id, { gpu, data });
    return gpu;
  }

  update(id: string, data: MeshData): void {
    const e = this.meshes.get(id);
    if (e) e.gpu.upload(data);
    else this.register(id, data);
  }

  dispose(id: string): void {
    this.meshes.get(id)?.gpu.dispose();
    this.meshes.delete(id);
  }

  disposeAll(): void {
    for (const [id] of this.meshes) this.dispose(id);
  }

  listIds(): string[] {
    return [...this.meshes.keys()];
  }
}
