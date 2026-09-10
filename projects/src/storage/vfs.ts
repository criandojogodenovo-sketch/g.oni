/**
 * G.oni Llumni — Sistema de arquivos virtual (IndexedDB)
 *
 * Estrutura persistente por projeto:
 *   MeuProjeto.g.oni/
 *   ├── scenes/main.json
 *   ├── scripts/<nome>.goni
 *   ├── models/<id>.json
 *   ├── textures/<id>.json
 *   ├── animations/<id>.json
 *   └── project.json
 *
 * Tenta usar OPFS quando disponível para dados grandes (futuro);
 * v1 mantém tudo em IndexedDB (suporte universal, inclusive iOS Safari).
 */

import { db, ProjectMeta } from './db';
import { GOniProject, GOniFormat } from '@core/goni/Format';
import { createDefaultScene } from '@core/goni/Engine';

export const STANDARD_FOLDERS = ['scenes', 'scripts', 'models', 'textures', 'animations'] as const;

export interface VFile {
  path: string;
  data: string;
  updatedAt: number;
}

export function newId(): string {
  return `p${Date.now().toString(36)}${Math.random().toString(36).slice(2, 7)}`;
}

export class VirtualFS {
  constructor(public projectId: string) {}

  // ---- operações de arquivo ----
  async write(path: string, data: string): Promise<void> {
    await db.putFile(this.projectId, path, data);
    const meta = await db.getProject(this.projectId);
    if (meta) {
      meta.updatedAt = Date.now();
      await db.putProject(meta);
    }
  }

  async read(path: string): Promise<string | null> {
    return db.getFile(this.projectId, path);
  }

  async list(): Promise<VFile[]> {
    const recs = await db.listFiles(this.projectId);
    return recs.map((r) => ({ path: r.path, data: r.data, updatedAt: r.updatedAt }));
  }

  async delete(path: string): Promise<void> {
    const dbh = await (await import('./db')).openDB();
    await new Promise<void>((resolve) => {
      const t = dbh.transaction('files', 'readwrite');
      t.objectStore('files').delete(`${this.projectId}/${path}`);
      t.oncomplete = () => resolve();
    });
  }

  // ---- montagem do projeto ----

  /** Lê todos os arquivos e monta o GOniProject completo. */
  async loadProjectData(): Promise<GOniProject | null> {
    const projectJson = await this.read('project.json');
    if (!projectJson) return null;
    const meta = JSON.parse(projectJson) as { name: string; createdAt: number };
    const files = await this.list();

    const scripts = files
      .filter((f) => f.path.startsWith('scripts/') && f.path.endsWith('.goni'))
      .map((f) => ({ name: f.path.slice(8, -5), source: f.data }));

    const meshes: Record<string, { p: number[]; n: number[]; u: number[]; i: number[] }> = {};
    for (const f of files.filter((x) => x.path.startsWith('models/'))) {
      try { meshes[`asset:${f.path.slice(7, -5)}`] = JSON.parse(f.data); } catch { /* ignora */ }
    }

    const textures: Record<string, string> = {};
    for (const f of files.filter((x) => x.path.startsWith('textures/'))) {
      try {
        const d = JSON.parse(f.data) as { dataUrl?: string };
        if (d.dataUrl) textures[f.path.slice(9, -5)] = d.dataUrl;
      } catch { /* ignora */ }
    }

    const sceneJson = await this.read('scenes/main.json');
    const settingsJson = await this.read('settings.json');

    const project: GOniProject = {
      version: GOniFormat.newProject('x').version,
      engine: 'G.oni Llumni',
      name: meta.name,
      createdAt: meta.createdAt,
      updatedAt: Date.now(),
      scenes: sceneJson ? [JSON.parse(sceneJson)] : [],
      scripts,
      assets: { meshes, textures },
      settings: settingsJson ? JSON.parse(settingsJson) : {},
    };
    return project;
  }

  /** Persiste o projeto inteiro a partir do estado da engine. */
  async saveProjectData(project: GOniProject): Promise<void> {
    await this.write('project.json', JSON.stringify({
      name: project.name,
      createdAt: project.createdAt,
      engine: 'G.oni Llumni',
      version: project.version,
    }));
    if (project.scenes[0]) {
      await this.write('scenes/main.json', JSON.stringify(project.scenes[0]));
    }
    // scripts: diff simples — reescreve todos
    const existingScripts = (await this.list()).filter((f) => f.path.startsWith('scripts/'));
    const currentNames = new Set(project.scripts.map((s) => `scripts/${s.name}.goni`));
    for (const s of project.scripts) {
      await this.write(`scripts/${s.name}.goni`, s.source);
    }
    for (const f of existingScripts) {
      if (!currentNames.has(f.path)) await this.delete(f.path);
    }
    for (const [id, md] of Object.entries(project.assets.meshes)) {
      const shortId = id.replace(/^asset:/, '');
      await this.write(`models/${shortId}.json`, JSON.stringify(md));
    }
    for (const [id, dataUrl] of Object.entries(project.assets.textures)) {
      await this.write(`textures/${id}.json`, JSON.stringify({ dataUrl }));
    }
    if (project.settings && Object.keys(project.settings).length) {
      await this.write('settings.json', JSON.stringify(project.settings));
    }
    const meta = await db.getProject(this.projectId);
    if (meta) {
      meta.updatedAt = Date.now();
      meta.name = project.name;
      await db.putProject(meta);
    }
  }

  // ---- projetos ----

  static async createProject(name: string, withTemplate = true): Promise<ProjectMeta> {
    const id = newId();
    const meta: ProjectMeta = { id, name, createdAt: Date.now(), updatedAt: Date.now() };
    await db.putProject(meta);

    const vfs = new VirtualFS(id);
    await vfs.write('project.json', JSON.stringify({ name, createdAt: meta.createdAt, engine: 'G.oni Llumni', version: GONI_VERSION }));

    if (withTemplate) {
      const scene = createDefaultScene();
      await vfs.write('scenes/main.json', JSON.stringify(scene.serialize()));
      // script de exemplo
      await vfs.write('scripts/girar.goni', EXAMPLE_SCRIPT);
    }
    return meta;
  }

  static async listProjects(): Promise<ProjectMeta[]> {
    return db.listProjects();
  }

  static async deleteProject(id: string): Promise<void> {
    await db.deleteProject(id);
  }

  static async renameProject(id: string, name: string): Promise<void> {
    const meta = await db.getProject(id);
    if (meta) {
      meta.name = name;
      meta.updatedAt = Date.now();
      await db.putProject(meta);
      const vfs = new VirtualFS(id);
      const projectJson = await vfs.read('project.json');
      if (projectJson) {
        const d = JSON.parse(projectJson);
        d.name = name;
        await vfs.write('project.json', JSON.stringify(d));
      }
    }
  }

  /** Importa um arquivo .g.oni como projeto novo. */
  static async importProject(project: GOniProject): Promise<ProjectMeta> {
    const meta = await VirtualFS.createProject(project.name, false);
    const vfs = new VirtualFS(meta.id);
    // sobrescreve com os dados importados
    await vfs.saveProjectData(project);
    return meta;
  }
}

const GONI_VERSION = '1.0';

export const EXAMPLE_SCRIPT = `# Script de exemplo — G.oni Script
# Anexe este script a um objeto para girá-lo.

var velocidade = 45.0

func on_ready():
    print("Objeto pronto: " + self.name)

func on_process(delta):
    self.rotate_y(velocidade * delta)
`;
