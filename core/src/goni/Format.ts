/**
 * G.oni Llumni — Formato de arquivo .g.oni
 *
 * Estrutura (JSON):
 * {
 *   "version": "1.0",
 *   "engine": "G.oni Llumni",
 *   "name": "MeuProjeto",
 *   "scenes": [...],
 *   "scripts": [{"name": "...", "source": "..."}],
 *   "assets": { "meshes": {...}, "textures": {...} },
 *   "settings": { "render": {...}, "physics": {...}, "construt": {...}, "links": {...} }
 * }
 *
 * Exportação: gzip (via CompressionStream da Web) com cabeçalho mágico.
 * Importação: detecta gzip pelo cabeçalho e descomprime; senão lê JSON puro.
 */

export const GONI_VERSION = '1.0';
export const GONI_ENGINE = 'G.oni Llumni';
export const GONI_MAGIC = 0x474f4e31; // "GON1"

export interface GOniScriptDef {
  name: string;
  source: string;
}

export interface GOniProject {
  version: string;
  engine: string;
  name: string;
  createdAt: number;
  updatedAt: number;
  scenes: Record<string, unknown>[];
  scripts: GOniScriptDef[];
  assets: {
    meshes: Record<string, { p: number[]; n: number[]; u: number[]; i: number[] }>;
    textures: Record<string, string>; // id → dataURL (PNG/JPEG)
  };
  settings: {
    render?: Record<string, unknown>;
    physics?: Record<string, unknown>;
    construt?: Record<string, unknown>;
    links?: Record<string, unknown>;
  };
}

export class GOniFormat {
  /** Cria a estrutura mínima de um projeto novo. */
  static newProject(name: string): GOniProject {
    const now = Date.now();
    return {
      version: GONI_VERSION,
      engine: GONI_ENGINE,
      name,
      createdAt: now,
      updatedAt: now,
      scenes: [
        {
          name: 'main',
          gravity: -9.81,
          environment: {},
          roots: [],
        },
      ],
      scripts: [],
      assets: { meshes: {}, textures: {} },
      settings: {},
    };
  }

  /** Valida a estrutura básica. */
  static validate(data: unknown): data is GOniProject {
    const d = data as GOniProject;
    return !!d && typeof d === 'object' && Array.isArray(d.scenes) && Array.isArray(d.scripts);
  }

  static toJSON(project: GOniProject): string {
    project.updatedAt = Date.now();
    return JSON.stringify(project);
  }

  static fromJSON(text: string): GOniProject {
    const data = JSON.parse(text);
    if (!GOniFormat.validate(data)) {
      throw new Error('Arquivo .g.oni inválido: estrutura não reconhecida');
    }
    return data;
  }

  /** Exporta como Blob gzip (com cabeçalho mágico + versão). */
  static async exportGzip(project: GOniProject): Promise<Blob> {
    const json = GOniFormat.toJSON(project);
    const jsonBytes = new TextEncoder().encode(json);

    const header = new ArrayBuffer(8);
    const dv = new DataView(header);
    dv.setUint32(0, GONI_MAGIC, true);
    dv.setUint32(4, jsonBytes.length, true);

    const CS = (globalThis as { CompressionStream?: typeof CompressionStream }).CompressionStream;
    if (!CS) {
      // navegador sem CompressionStream: exporta JSON puro
      return new Blob([header, jsonBytes], { type: 'application/octet-stream' });
    }
    const gz = new Blob([jsonBytes]).stream().pipeThrough(new CS('gzip'));
    const gzBytes = new Uint8Array(await new Response(gz).arrayBuffer());
    return new Blob([header, gzBytes], { type: 'application/octet-stream' });
  }

  /** Importa de ArrayBuffer (detecta gzip automaticamente). */
  static async importBuffer(buf: ArrayBuffer): Promise<GOniProject> {
    const bytes = new Uint8Array(buf);
    if (bytes.length < 8) throw new Error('Arquivo .g.oni vazio ou truncado');

    const dv = new DataView(buf);
    const magic = dv.getUint32(0, true);
    if (magic === GONI_MAGIC) {
      const jsonLen = dv.getUint32(4, true);
      const payload = bytes.slice(8);
      const DS = (globalThis as { DecompressionStream?: typeof DecompressionStream }).DecompressionStream;
      // alguns arquivos podem ser JSON puro com cabeçalho (fallback de export)
      let text: string;
      if (DS && payload.length !== jsonLen) {
        const stream = new Blob([payload]).stream().pipeThrough(new DS('gzip'));
        text = await new Response(stream).text();
      } else {
        text = new TextDecoder().decode(payload);
      }
      return GOniFormat.fromJSON(text);
    }
    // sem cabeçalho: tenta gzip puro, senão JSON
    if (bytes[0] === 0x1f && bytes[1] === 0x8b) {
      const DS = (globalThis as { DecompressionStream?: typeof DecompressionStream }).DecompressionStream;
      if (DS) {
        const stream = new Blob([bytes]).stream().pipeThrough(new DS('gzip'));
        const text = await new Response(stream).text();
        return GOniFormat.fromJSON(text);
      }
    }
    return GOniFormat.fromJSON(new TextDecoder().decode(bytes));
  }

  static async importFile(file: File): Promise<GOniProject> {
    return GOniFormat.importBuffer(await file.arrayBuffer());
  }

  /** Nome de arquivo sanitizado. */
  static fileName(projectName: string): string {
    const safe = projectName.trim().toLowerCase().replace(/[^a-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '');
    return `${safe || 'projeto'}.g.oni`;
  }
}
