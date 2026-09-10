/**
 * G.oni Llumni — Material PBR (workflow metal-roughness)
 * Albedo (cor + textura), roughness, metallic, emissive, normal map.
 */

import { Color } from '../math';
import { Texture } from './Texture';

export class Material {
  id: string;
  name = 'Material';

  albedo = new Color(0.8, 0.8, 0.8, 1);
  metallic = 0.0;
  roughness = 0.6;

  emissive = new Color(0, 0, 0, 1);
  emissiveIntensity = 0.0;

  opacity = 1.0;
  alphaCutoff = 0.0; // > 0 ativa modo recorte (folhagem etc.)
  doubleSided = false;

  albedoTexture: Texture | null = null;
  normalTexture: Texture | null = null;
  /** Camada de pintura 2D usada pelo editor de texturização (mesclada no albedo). */
  paintCanvas: HTMLCanvasElement | null = null;

  /** nome do shader de usuário (GLSL custom); null = PBR padrão */
  customShaderName: string | null = null;

  constructor(id: string = `mat_${Math.random().toString(36).slice(2, 10)}`) {
    this.id = id;
  }

  clone(): Material {
    const m = new Material(`mat_${Math.random().toString(36).slice(2, 10)}`);
    m.name = this.name + '_copia';
    m.albedo = this.albedo.clone();
    m.metallic = this.metallic;
    m.roughness = this.roughness;
    m.emissive = this.emissive.clone();
    m.emissiveIntensity = this.emissiveIntensity;
    m.opacity = this.opacity;
    m.alphaCutoff = this.alphaCutoff;
    m.doubleSided = this.doubleSided;
    m.albedoTexture = this.albedoTexture;
    m.normalTexture = this.normalTexture;
    m.paintCanvas = this.paintCanvas;
    return m;
  }

  serialize(): Record<string, unknown> {
    return {
      id: this.id,
      name: this.name,
      albedo: this.albedo.toArray(),
      metallic: this.metallic,
      roughness: this.roughness,
      emissive: this.emissive.toArray(),
      emissiveIntensity: this.emissiveIntensity,
      opacity: this.opacity,
      alphaCutoff: this.alphaCutoff,
      doubleSided: this.doubleSided,
      albedoTextureId: this.albedoTexture?.id ?? null,
      normalTextureId: this.normalTexture?.id ?? null,
    };
  }

  static deserialize(d: Record<string, unknown>): Material {
    const m = new Material(String(d.id ?? `mat_${Math.random().toString(36).slice(2, 10)}`));
    m.name = String(d.name ?? 'Material');
    const al = (d.albedo as number[]) ?? [0.8, 0.8, 0.8, 1];
    m.albedo = new Color(al[0], al[1], al[2], al[3] ?? 1);
    m.metallic = Number(d.metallic ?? 0);
    m.roughness = Number(d.roughness ?? 0.6);
    const em = (d.emissive as number[]) ?? [0, 0, 0, 1];
    m.emissive = new Color(em[0], em[1], em[2], em[3] ?? 1);
    m.emissiveIntensity = Number(d.emissiveIntensity ?? 0);
    m.opacity = Number(d.opacity ?? 1);
    m.alphaCutoff = Number(d.alphaCutoff ?? 0);
    m.doubleSided = Boolean(d.doubleSided);
    // ids de textura resolvidos depois do load (Engine aplica a textura real)
    (m as unknown as { pendingAlbedoId?: string }).pendingAlbedoId = (d.albedoTextureId as string) ?? undefined;
    (m as unknown as { pendingNormalId?: string }).pendingNormalId = (d.normalTextureId as string) ?? undefined;
    return m;
  }
}

/**
 * Registro de materiais do projeto. Texturas são resolvidas por id
 * no momento do carregamento (via TextureManager).
 */
export class MaterialLibrary {
  private materials = new Map<string, Material>();

  get(id: string): Material | undefined { return this.materials.get(id); }
  add(m: Material): void { this.materials.set(m.id, m); }
  remove(id: string): void { this.materials.delete(id); }
  list(): Material[] { return [...this.materials.values()]; }
  clear(): void { this.materials.clear(); }
}
