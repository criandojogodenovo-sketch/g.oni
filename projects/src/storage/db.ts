/**
 * G.oni Llumni — Camada IndexedDB
 * Duas stores: "projects" (metadados) e "files" (sistema de arquivos virtual).
 */

const DB_NAME = 'goni-llumni';
const DB_VERSION = 1;

export interface ProjectMeta {
  id: string;
  name: string;
  createdAt: number;
  updatedAt: number;
}

export interface FileRecord {
  /** chave composta: "<projectId>/<path>" */
  key: string;
  projectId: string;
  path: string;
  data: string;
  updatedAt: number;
}

let dbPromise: Promise<IDBDatabase> | null = null;

export function openDB(): Promise<IDBDatabase> {
  if (dbPromise) return dbPromise;
  dbPromise = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION);
    req.onupgradeneeded = () => {
      const db = req.result;
      if (!db.objectStoreNames.contains('projects')) {
        db.createObjectStore('projects', { keyPath: 'id' });
      }
      if (!db.objectStoreNames.contains('files')) {
        const files = db.createObjectStore('files', { keyPath: 'key' });
        files.createIndex('projectId', 'projectId', { unique: false });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
  return dbPromise;
}

async function tx<T>(store: string, mode: IDBTransactionMode, fn: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  const db = await openDB();
  return new Promise<T>((resolve, reject) => {
    const t = db.transaction(store, mode);
    const req = fn(t.objectStore(store));
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

export const db = {
  async putProject(meta: ProjectMeta): Promise<void> {
    await tx('projects', 'readwrite', (s) => s.put(meta) as IDBRequest<IDBValidKey>);
  },
  async getProject(id: string): Promise<ProjectMeta | undefined> {
    return tx('projects', 'readonly', (s) => s.get(id) as IDBRequest<ProjectMeta | undefined>);
  },
  async listProjects(): Promise<ProjectMeta[]> {
    const all = await tx('projects', 'readonly', (s) => s.getAll() as IDBRequest<ProjectMeta[]>);
    return all.sort((a, b) => b.updatedAt - a.updatedAt);
  },
  async deleteProject(id: string): Promise<void> {
    await tx('projects', 'readwrite', (s) => s.delete(id) as unknown as IDBRequest<undefined>);
    // remove arquivos
    const dbh = await openDB();
    await new Promise<void>((resolve, reject) => {
      const t = dbh.transaction('files', 'readwrite');
      const store = t.objectStore('files');
      const idx = store.index('projectId');
      const cursorReq = idx.openCursor(IDBKeyRange.only(id));
      cursorReq.onsuccess = () => {
        const cursor = cursorReq.result;
        if (cursor) {
          cursor.delete();
          cursor.continue();
        }
      };
      t.oncomplete = () => resolve();
      t.onerror = () => reject(t.error);
    });
  },

  async putFile(projectId: string, path: string, data: string): Promise<void> {
    const key = `${projectId}/${path}`;
    await tx('files', 'readwrite', (s) => s.put({ key, projectId, path, data, updatedAt: Date.now() } satisfies FileRecord) as IDBRequest<IDBValidKey>);
  },
  async getFile(projectId: string, path: string): Promise<string | null> {
    const rec = await tx('files', 'readonly', (s) => s.get(`${projectId}/${path}`) as IDBRequest<FileRecord | undefined>);
    return rec?.data ?? null;
  },
  async listFiles(projectId: string): Promise<FileRecord[]> {
    const dbh = await openDB();
    return new Promise((resolve, reject) => {
      const t = dbh.transaction('files', 'readonly');
      const idx = t.objectStore('files').index('projectId');
      const req = idx.getAll(IDBKeyRange.only(projectId));
      req.onsuccess = () => resolve((req.result as FileRecord[]).sort((a, b) => a.path.localeCompare(b.path)));
      req.onerror = () => reject(req.error);
    });
  },
};
