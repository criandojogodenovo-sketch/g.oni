/**
 * G.oni Eliminação Profissional — Destruição e limpeza.
 *
 * Recursos:
 *  - destroy(objeto): remove da cena, dispara on_destroy,
 *    libera referências e registra no log
 *  - Fila de destruição diferida (fim do frame, segura para callbacks)
 *  - Pooling de objetos (reuso sem alocação — performance mobile)
 *  - GC assistido: limpa pools ociosos
 *  - Log de destruição para depuração
 */

import { GOniObject } from './Objetos';
import { Scene } from './Scene';
import { Construt } from './Construt';

export interface DestructionLogEntry {
  time: number;
  objectName: string;
  objectId: string;
  reason: string;
  pooled: boolean;
}

interface PoolEntry {
  obj: GOniObject;
  releasedAt: number;
}

export class Eliminacao {
  log: DestructionLogEntry[] = [];
  maxLog = 200;

  private destroyQueue: { obj: GOniObject; reason: string; pool: boolean }[] = [];
  private pools = new Map<string, PoolEntry[]>();
  private construt: Construt;

  stats = {
    destroyed: 0,
    pooled: 0,
    reused: 0,
    poolHits: 0,
  };

  constructor(construt: Construt) {
    this.construt = construt;
  }

  private pushLog(name: string, id: string, reason: string, pooled: boolean): void {
    this.log.unshift({
      time: Date.now(),
      objectName: name,
      objectId: id,
      reason,
      pooled,
    });
    if (this.log.length > this.maxLog) this.log.length = this.maxLog;
  }

  /**
   * Destruição imediata: remove da cena, dispara on_destroy,
   * desconecta sinais e limpa componentes.
   * Em play mode, prefira queueDestroy (executado no fim do frame).
   */
  destroy(obj: GOniObject, scene: Scene, reason = 'manual', pool = false): void {
    // desanexa da hierarquia
    scene.remove(obj);

    // dispara sinais de destruição na subárvore
    obj.traverse((o) => {
      o.signals.emit('on_destroy', [o.name]);
      o.signals.clear();
    });

    // limpa referências de componentes runtime
    for (const c of obj.components) {
      if (c.type === 'script') (c as { runtime?: unknown }).runtime = null;
    }

    this.pushLog(obj.name, obj.id, reason, pool);
    this.stats.destroyed++;
    if (pool) {
      const key = obj.name;
      const list = this.pools.get(key) ?? [];
      obj.visible = false;
      list.push({ obj, releasedAt: performance.now() });
      this.pools.set(key, list);
      this.stats.pooled++;
    }
  }

  /** Destruição diferida — segura durante iterações/colisões. */
  queueDestroy(obj: GOniObject, reason = 'deferred', pool = false): void {
    if (this.destroyQueue.some((q) => q.obj === obj)) return;
    this.destroyQueue.push({ obj, reason, pool });
  }

  /** Executa a fila (chamado pela Engine no fim do frame). */
  flushDestroyQueue(scene: Scene): number {
    const items = this.destroyQueue.splice(0);
    for (const { obj, reason, pool } of items) {
      this.destroy(obj, scene, reason, pool);
    }
    return items.length;
  }

  /**
   * Pooling: reaproveita objeto do pool ou instancia o prefab.
   * "spawn com pooling" — evita GC em partículas/projéteis.
   */
  acquire(prefabName: string, scene: Scene): { obj: GOniObject | null; reused: boolean } {
    const list = this.pools.get(prefabName);
    if (list && list.length) {
      const entry = list.pop()!;
      entry.obj.visible = true;
      this.stats.reused++;
      this.stats.poolHits++;
      scene.add(entry.obj, null);
      return { obj: entry.obj, reused: true };
    }
    try {
      const obj = this.construt.instantiate(prefabName, scene);
      return { obj, reused: false };
    } catch {
      return { obj: null, reused: false };
    }
  }

  /** Devolve objeto ao pool (em vez de destruir). */
  release(obj: GOniObject, scene: Scene, poolKey?: string): void {
    const key = poolKey ?? obj.name;
    scene.remove(obj);
    obj.visible = false;
    obj.traverse((o) => o.signals.clear());
    const list = this.pools.get(key) ?? [];
    list.push({ obj, releasedAt: performance.now() });
    this.pools.set(key, list);
    this.pushLog(obj.name, obj.id, 'release→pool', true);
    this.stats.pooled++;
  }

  /** GC assistido: remove entradas de pool não usadas há idleMs. */
  gc(idleMs = 30000): number {
    const now = performance.now();
    let removed = 0;
    for (const [key, list] of this.pools) {
      const keep = list.filter((e) => now - e.releasedAt < idleMs);
      removed += list.length - keep.length;
      if (keep.length === 0) this.pools.delete(key);
      else this.pools.set(key, keep);
    }
    if (removed > 0) {
      this.pushLog(`${removed} objetos de pool`, 'gc', 'gc', false);
    }
    return removed;
  }

  poolStats(): { poolCount: number; pooledObjects: number } {
    let n = 0;
    for (const list of this.pools.values()) n += list.length;
    return { poolCount: this.pools.size, pooledObjects: n };
  }
}
