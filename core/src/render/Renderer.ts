/**
 * G.oni Llumni — Pipeline de Renderização (WebGL2)
 *
 * Fluxo por frame:
 *   [shadow pass]  luz direcional → depth map 2048² (PCF 3x3 no shader PBR)
 *   [cena]         MSAA 4x + HDR (RGBA16F, fallback RGBA8)
 *                  1. céu procedural  2. opacos PBR (frustum culling)
 *                  3. transparentes   4. grid infinito  5. linhas (gizmos)
 *   [resolve]      blit MSAA → textura
 *   [pós]          bright pass → blur gaussiano (½ res) → composição
 *                  (bloom + ACES tonemap + exposição + vinheta + gamma)
 *
 * Otimizações mobile: frustum culling por esfera envolvente, limite de luzes,
 * bloom em meia resolução, DPR limitado, ordenação frente→trás dos opacos.
 */

import { Mat4, Vec3, Color } from '../math';
import { Shader } from './Shader';
import { MeshRegistry, GpuMesh } from './Mesh';
import { Material } from './Material';
import { Camera } from './Camera';
import { LightBatch, Light } from './Light';
import {
  PBR_VS, PBR_FS, SHADOW_VS, SHADOW_FS, SKY_VS, SKY_FS,
  GRID_FS, LINE_VS, LINE_FS, FLAT_VS, FLAT_FS,
  POST_VS, BRIGHT_FS, BLUR_FS, COMPOSITE_FS, BLIT_FS,
} from './shaders';

export interface RenderItem {
  model: Mat4;
  mesh: GpuMesh;
  material: Material;
  /** Centro da esfera envolvente no mundo. */
  worldCenter: Vec3;
  /** Raio da esfera envolvente no mundo (escala aplicada). */
  worldRadius: number;
  transparent: boolean;
  /** id do objeto (para debug/estatísticas). */
  ownerId?: string;
}

export interface RenderStats {
  drawCalls: number;
  triangles: number;
  culled: number;
}

/** Configurações visuais do mundo (serializáveis em .g.oni). */
export class RenderSettings {
  skyZenith = new Color(0.09, 0.13, 0.22);
  skyHorizon = new Color(0.35, 0.42, 0.52);
  ambientSky = new Color(0.16, 0.19, 0.26);
  ambientGround = new Color(0.08, 0.07, 0.06);
  ambientIntensity = 1.0;

  exposure = 1.15;
  bloomStrength = 0.55;
  bloomThreshold = 1.0;
  vignette = 0.25;

  shadowsEnabled = true;
  postEnabled = true;
  gridEnabled = true;

  serialize(): Record<string, unknown> {
    return {
      skyZenith: this.skyZenith.toArray(),
      skyHorizon: this.skyHorizon.toArray(),
      ambientSky: this.ambientSky.toArray(),
      ambientGround: this.ambientGround.toArray(),
      ambientIntensity: this.ambientIntensity,
      exposure: this.exposure,
      bloomStrength: this.bloomStrength,
      bloomThreshold: this.bloomThreshold,
      vignette: this.vignette,
      shadowsEnabled: this.shadowsEnabled,
      postEnabled: this.postEnabled,
    };
  }

  static deserialize(d: Record<string, unknown>): RenderSettings {
    const r = new RenderSettings();
    const c = (k: string, fallback: Color) => {
      const v = d[k] as number[] | undefined;
      return v ? new Color(v[0], v[1], v[2], v[3] ?? 1) : fallback;
    };
    r.skyZenith = c('skyZenith', r.skyZenith);
    r.skyHorizon = c('skyHorizon', r.skyHorizon);
    r.ambientSky = c('ambientSky', r.ambientSky);
    r.ambientGround = c('ambientGround', r.ambientGround);
    r.ambientIntensity = Number(d.ambientIntensity ?? 1);
    r.exposure = Number(d.exposure ?? 1.15);
    r.bloomStrength = Number(d.bloomStrength ?? 0.55);
    r.bloomThreshold = Number(d.bloomThreshold ?? 1);
    r.vignette = Number(d.vignette ?? 0.25);
    r.shadowsEnabled = Boolean(d.shadowsEnabled ?? true);
    r.postEnabled = Boolean(d.postEnabled ?? true);
    return r;
  }
}

/** Lote dinâmico de linhas coloridas (gizmos, colisores, outlines). */
export class LineBatch {
  data: number[] = []; // pos(3) + color(3) intercalados
  private vao: WebGLVertexArrayObject | null = null;
  private vbo: WebGLBuffer | null = null;
  private cap = 0;

  constructor(private gl: WebGL2RenderingContext) {}

  line(ax: number, ay: number, az: number, bx: number, by: number, bz: number, color: Color): void {
    this.data.push(ax, ay, az, color.r, color.g, color.b, bx, by, bz, color.r, color.g, color.b);
  }

  /** Caixa alinhada por eixos (min/max) transformada por matrix opcional. */
  box(min: [number, number, number], max: [number, number, number], color: Color, m?: Mat4): void {
    const corners: Vec3[] = [];
    for (const x of [min[0], max[0]]) for (const y of [min[1], max[1]]) for (const z of [min[2], max[2]]) {
      corners.push(m ? m.transformPoint(new Vec3(x, y, z)) : new Vec3(x, y, z));
    }
    // índices das arestas do cubo
    const edges = [[0, 1], [1, 3], [3, 2], [2, 0], [4, 5], [5, 7], [7, 6], [6, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
    // ordem dos cantos: x,y,z variam em sequência (bitmask)
    for (const [a, b] of edges) {
      this.line(corners[a].x, corners[a].y, corners[a].z, corners[b].x, corners[b].y, corners[b].z, color);
    }
  }

  circle(center: Vec3, radius: number, axis: 'x' | 'y' | 'z', color: Color, segments = 32, m?: Mat4): void {
    let prev: Vec3 | null = null;
    for (let i = 0; i <= segments; i++) {
      const t = (i / segments) * Math.PI * 2;
      let p: Vec3;
      if (axis === 'y') p = new Vec3(Math.cos(t) * radius, 0, Math.sin(t) * radius);
      else if (axis === 'x') p = new Vec3(0, Math.cos(t) * radius, Math.sin(t) * radius);
      else p = new Vec3(Math.cos(t) * radius, Math.sin(t) * radius, 0);
      p = p.add(center);
      if (m) p = m.transformPoint(p);
      if (prev) this.line(prev.x, prev.y, prev.z, p.x, p.y, p.z, color);
      prev = p;
    }
  }

  clear(): void { this.data.length = 0; }

  /** Envia para a GPU e desenha (chamado pelo Renderer). */
  flushDraw(view: Mat4, proj: Mat4): number {
    const gl = this.gl;
    if (this.data.length === 0) return 0;
    if (!this.vao) {
      this.vao = gl.createVertexArray();
      this.vbo = gl.createBuffer();
    }
    const arr = new Float32Array(this.data);
    gl.bindVertexArray(this.vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, this.vbo);
    if (arr.length > this.cap) {
      gl.bufferData(gl.ARRAY_BUFFER, arr, gl.DYNAMIC_DRAW);
      this.cap = arr.length;
    } else {
      gl.bufferSubData(gl.ARRAY_BUFFER, 0, arr);
    }
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);
    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);

    const sh = Renderer.lineShaderStatic!;
    sh.use();
    sh.setMat4('uView', view.m);
    sh.setMat4('uProj', proj.m);
    gl.drawArrays(gl.LINES, 0, arr.length / 6);
    gl.bindVertexArray(null);
    return arr.length / 6;
  }
}

/** Frustum para culling (6 planos extraídos da view-projection). */
class Frustum {
  planes: Float32Array[] = [];

  static fromViewProj(vp: Mat4): Frustum {
    // extração padrão: left, right, bottom, top, near, far
    const f = new Frustum();
    const m = vp.m;
    const mk = (a: number[], b: number[], sign: number) => {
      const pl = new Float32Array(4);
      for (let i = 0; i < 4; i++) pl[i] = b[i] + sign * a[i];
      return pl;
    };
    const row = (r: number) => [m[r], m[4 + r], m[8 + r], m[12 + r]];
    const r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    f.planes.push(mk(r0, r3, 1), mk(r0, r3, -1), mk(r1, r3, 1), mk(r1, r3, -1), mk(r2, r3, 1), mk(r2, r3, -1));
    return f;
  }

  containsSphere(c: Vec3, r: number): boolean {
    for (const pl of this.planes) {
      const d = pl[0] * c.x + pl[1] * c.y + pl[2] * c.z + pl[3];
      if (d < -r) return false;
    }
    return true;
  }
}

interface FBO {
  fbo: WebGLFramebuffer;
  width: number;
  height: number;
  colorTex: WebGLTexture | null;
  depthRB: WebGLRenderbuffer | null;
  msaaRB: WebGLRenderbuffer | null;
}

export class Renderer {
  gl!: WebGL2RenderingContext;
  canvas!: HTMLCanvasElement;
  dpr = 1;

  meshRegistry!: MeshRegistry;
  settings = new RenderSettings();
  stats: RenderStats = { drawCalls: 0, triangles: 0, culled: 0 };

  private pbr!: Shader;
  private shadow!: Shader;
  private sky!: Shader;
  private grid!: Shader;
  private flat!: Shader;
  private bright!: Shader;
  private blur!: Shader;
  private composite!: Shader;
  private blit!: Shader;
  static lineShaderStatic: Shader | null = null;

  private sceneMS: FBO | null = null;
  private resolveFBO: FBO | null = null;
  private bloomA: FBO | null = null;
  private bloomB: FBO | null = null;
  private shadowFBO: FBO | null = null;
  private hdr = true;
  private samples = 0;

  private whiteTex!: WebGLTexture;

  private lightVP = new Mat4();
  private tmpNormalMat = new Mat4();
  private tmpNormalArr = new Float32Array(9);
  private pArr = new Float32Array(12);
  private pcArr = new Float32Array(12);
  private piArr = new Float32Array(4);
  private prArr = new Float32Array(4);
  private sPos = new Float32Array(6);
  private sDir = new Float32Array(6);
  private sCol = new Float32Array(6);
  private sInt = new Float32Array(2);
  private sRange = new Float32Array(2);
  private sCos = new Float32Array(2);

  private ready = false;

  /** Painel de erros de contexto (para o editor exibir). */
  lastError: string | null = null;

  init(canvas: HTMLCanvasElement): void {
    this.canvas = canvas;
    const gl = canvas.getContext('webgl2', {
      alpha: false,
      antialias: false,
      depth: true,
      stencil: false,
      powerPreference: 'high-performance',
      preserveDrawingBuffer: false,
    });
    if (!gl) {
      this.lastError = 'WebGL2 não suportado neste navegador. A engine exige WebGL2.';
      throw new Error(this.lastError);
    }
    this.gl = gl;
    this.hdr = !!gl.getExtension('EXT_color_buffer_float');
    this.samples = Math.min(4, gl.getParameter(gl.MAX_SAMPLES) || 0);

    this.pbr = new Shader(gl, PBR_VS, PBR_FS);
    this.shadow = new Shader(gl, SHADOW_VS, SHADOW_FS);
    this.sky = new Shader(gl, SKY_VS, SKY_FS);
    this.grid = new Shader(gl, SKY_VS, GRID_FS);
    Renderer.lineShaderStatic = new Shader(gl, LINE_VS, LINE_FS);
    this.flat = new Shader(gl, FLAT_VS, FLAT_FS);
    this.bright = new Shader(gl, POST_VS, BRIGHT_FS);
    this.blur = new Shader(gl, POST_VS, BLUR_FS);
    this.composite = new Shader(gl, POST_VS, COMPOSITE_FS);
    this.blit = new Shader(gl, POST_VS, BLIT_FS);

    this.meshRegistry = new MeshRegistry(gl);

    // textura branca 1x1
    this.whiteTex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, this.whiteTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));

    // FBO de sombra
    this.shadowFBO = this.createDepthFBO(2048, 2048);

    this.ready = true;
    this.resize();
  }

  isReady(): boolean { return this.ready; }

  resize(): void {
    const gl = this.gl;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.dpr = dpr;
    const w = Math.max(1, Math.floor(this.canvas.clientWidth * dpr));
    const h = Math.max(1, Math.floor(this.canvas.clientHeight * dpr));
    if (this.canvas.width === w && this.canvas.height === h && this.sceneMS) return;
    this.canvas.width = w;
    this.canvas.height = h;

    this.destroyFBO(this.sceneMS);
    this.destroyFBO(this.resolveFBO);
    this.destroyFBO(this.bloomA);
    this.destroyFBO(this.bloomB);

    this.sceneMS = this.createSceneFBO(w, h, true);
    this.resolveFBO = this.createSceneFBO(w, h, false);
    this.bloomA = this.createSceneFBO(Math.max(1, w >> 1), Math.max(1, h >> 1), false);
    this.bloomB = this.createSceneFBO(Math.max(1, w >> 1), Math.max(1, h >> 1), false);
    void gl;
  }

  private destroyFBO(f: FBO | null): void {
    if (!f) return;
    const gl = this.gl;
    gl.deleteFramebuffer(f.fbo);
    if (f.colorTex) gl.deleteTexture(f.colorTex);
    if (f.depthRB) gl.deleteRenderbuffer(f.depthRB);
    if (f.msaaRB) gl.deleteRenderbuffer(f.msaaRB);
  }

  private createSceneFBO(w: number, h: number, multisample: boolean): FBO {
    const gl = this.gl;
    const fbo = gl.createFramebuffer()!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);

    const internal = this.hdr ? gl.RGBA16F : gl.RGBA8;
    const type = this.hdr ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE;

    let colorTex: WebGLTexture | null = null;
    let msaaRB: WebGLRenderbuffer | null = null;

    if (multisample && this.samples > 0) {
      msaaRB = gl.createRenderbuffer()!;
      gl.bindRenderbuffer(gl.RENDERBUFFER, msaaRB);
      gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, internal, w, h);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, msaaRB);
    } else {
      colorTex = gl.createTexture()!;
      gl.bindTexture(gl.TEXTURE_2D, colorTex);
      gl.texImage2D(gl.TEXTURE_2D, 0, internal, w, h, 0, gl.RGBA, type, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, colorTex, 0);
    }

    const depthRB = gl.createRenderbuffer()!;
    gl.bindRenderbuffer(gl.RENDERBUFFER, depthRB);
    if (multisample && this.samples > 0) {
      gl.renderbufferStorageMultisample(gl.RENDERBUFFER, this.samples, gl.DEPTH_COMPONENT24, w, h);
    } else {
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);
    }
    gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthRB);

    const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
    if (status !== gl.FRAMEBUFFER_COMPLETE && multisample) {
      // fallback sem MSAA
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.deleteFramebuffer(fbo);
      if (msaaRB) gl.deleteRenderbuffer(msaaRB);
      if (depthRB) gl.deleteRenderbuffer(depthRB);
      return this.createSceneFBO(w, h, false);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return { fbo, width: w, height: h, colorTex, depthRB, msaaRB };
  }

  private createDepthFBO(w: number, h: number): FBO {
    const gl = this.gl;
    const fbo = gl.createFramebuffer()!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const tex = gl.createTexture()!;
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.DEPTH_COMPONENT24, w, h, 0, gl.DEPTH_COMPONENT, gl.UNSIGNED_INT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, tex, 0);
    gl.drawBuffers([gl.NONE]);
    gl.readBuffer(gl.NONE);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    return { fbo, width: w, height: h, colorTex: tex, depthRB: null, msaaRB: null };
  }

  /** Renderiza um frame completo. lines: com profundidade; overlayLines: sempre visíveis (gizmos). */
  render(camera: Camera, items: RenderItem[], lights: LightBatch, lines?: LineBatch, overlayLines?: LineBatch): void {
    if (!this.ready) return;
    this.resize();
    const gl = this.gl;
    const s = this.settings;

    this.stats.drawCalls = 0;
    this.stats.triangles = 0;
    this.stats.culled = 0;

    const view = camera.view;
    const proj = camera.proj;
    const viewProj = Mat4.multiply(proj, view);
    const frustum = Frustum.fromViewProj(viewProj);

    // --- Frustum culling + ordenação ---
    const visible: RenderItem[] = [];
    const transparent: RenderItem[] = [];
    for (const it of items) {
      if (!frustum.containsSphere(it.worldCenter, it.worldRadius)) {
        this.stats.culled++;
        continue;
      }
      if (it.transparent) transparent.push(it);
      else visible.push(it);
    }
    const camPos = camera.eye;
    const dist = (a: RenderItem, b: RenderItem) =>
      a.worldCenter.distanceTo(camPos) - b.worldCenter.distanceTo(camPos);
    visible.sort(dist);
    transparent.sort((a, b) => dist(b, a)); // transparentes: de trás para frente

    // --- Pass de sombras ---
    const dir = lights.dir;
    const shadowsOn = s.shadowsEnabled && !!dir && dir.castShadows && dir.intensity > 0;
    if (shadowsOn && dir) {
      this.renderShadowMap(dir, items, camera);
    }

    // --- Pass da cena (MSAA) ---
    const target = this.sceneMS!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, target.fbo);
    gl.viewport(0, 0, target.width, target.height);
    gl.clearColor(0.02, 0.03, 0.05, 1);
    gl.clearDepth(1);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.disable(gl.CULL_FACE);

    // céu
    this.drawSky(camera, lights);

    // opacos
    for (const it of visible) {
      this.drawItem(it, camera, lights, shadowsOn, dir);
    }

    // transparentes
    if (transparent.length) {
      gl.enable(gl.BLEND);
      gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.depthMask(false);
      for (const it of transparent) this.drawItem(it, camera, lights, shadowsOn, dir);
      gl.depthMask(true);
      gl.disable(gl.BLEND);
    }

    // grid
    if (s.gridEnabled) this.drawGrid(camera, viewProj);
    // linhas com profundidade (colisores, bounds)
    if (lines) {
      const count = lines.flushDraw(view, proj);
      this.stats.drawCalls++;
      this.stats.triangles += count / 3;
    }
    // linhas de overlay (gizmos): sempre por cima
    if (overlayLines) {
      gl.clear(gl.DEPTH_BUFFER_BIT);
      const count = overlayLines.flushDraw(view, proj);
      this.stats.drawCalls++;
      this.stats.triangles += count / 3;
    }

    gl.bindFramebuffer(gl.FRAMEBUFFER, null);

    // --- Resolve MSAA ---
    if (target.msaaRB) {
      const res = this.resolveFBO!;
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, target.fbo);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, res.fbo);
      gl.blitFramebuffer(0, 0, target.width, target.height, 0, 0, res.width, res.height, gl.COLOR_BUFFER_BIT, gl.NEAREST);
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
    }

    // --- Pós-processamento ---
    const srcFBO = this.sceneMS!.msaaRB ? this.resolveFBO! : this.sceneMS!;
    const sceneTex = srcFBO.colorTex!;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);

    if (s.postEnabled) {
      // o bloom não deve ser oculto pelo depth do 3D
      gl.disable(gl.DEPTH_TEST);
      gl.depthMask(false);
      // bright pass → bloomA (½ res)
      const a = this.bloomA!;
      gl.bindFramebuffer(gl.FRAMEBUFFER, a.fbo);
      gl.viewport(0, 0, a.width, a.height);
      this.bright.use();
      this.bright.setTexture('uScene', sceneTex, 0);
      this.bright.setFloat('uThreshold', s.bloomThreshold);
      this.drawFullscreenTri();

      // blur horizontal → bloomB, vertical → bloomA (2 iterações)
      for (let i = 0; i < 2; i++) {
        const b = this.bloomB!;
        gl.bindFramebuffer(gl.FRAMEBUFFER, b.fbo);
        gl.viewport(0, 0, b.width, b.height);
        this.blur.use();
        this.blur.setTexture('uTex', a.colorTex, 0);
        this.blur.setVec2('uDir', 1 / a.width, 0);
        this.drawFullscreenTri();

        gl.bindFramebuffer(gl.FRAMEBUFFER, a.fbo);
        gl.viewport(0, 0, a.width, a.height);
        this.blur.use();
        this.blur.setTexture('uTex', b.colorTex, 0);
        this.blur.setVec2('uDir', 0, 1 / b.height);
        this.drawFullscreenTri();
      }

      // composição final
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, this.canvas.width, this.canvas.height);
      gl.disable(gl.DEPTH_TEST);
      this.composite.use();
      this.composite.setTexture('uScene', sceneTex, 0);
      this.composite.setTexture('uBloom', a.colorTex, 1);
      this.composite.setFloat('uBloomStrength', s.bloomStrength);
      this.composite.setFloat('uExposure', s.exposure);
      this.composite.setFloat('uVignette', s.vignette);
      this.drawFullscreenTri();
      gl.depthMask(true);
      gl.enable(gl.DEPTH_TEST);
    } else {
      gl.disable(gl.DEPTH_TEST);
      this.blit.use();
      this.blit.setTexture('uScene', sceneTex, 0);
      this.drawFullscreenTri();
      gl.enable(gl.DEPTH_TEST);
    }
  }

  private drawFullscreenTri(): void {
    const gl = this.gl;
    gl.bindVertexArray(null); // shader usa gl_VertexID
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    this.stats.drawCalls++;
  }

  private drawSky(camera: Camera, lights: LightBatch): void {
    const gl = this.gl;
    const invVP = Mat4.invert(Mat4.multiply(camera.proj, camera.view));
    gl.depthMask(false);
    this.sky.use();
    this.sky.setMat4('uInvViewProj', invVP.m);
    const s = this.settings;
    this.sky.setVec3('uZenith', s.skyZenith.r, s.skyZenith.g, s.skyZenith.b);
    this.sky.setVec3('uHorizon', s.skyHorizon.r, s.skyHorizon.g, s.skyHorizon.b);
    const d = lights.dir;
    this.sky.setVec3('uSunDir', d ? d.direction.x : 0, d ? d.direction.y : -1, d ? d.direction.z : 0);
    this.sky.setVec3('uSunColor', d ? d.color.r * Math.min(2, d.intensity) : 1, d ? d.color.g * Math.min(2, d.intensity) : 1, d ? d.color.b * Math.min(2, d.intensity) : 1);
    this.drawFullscreenTri();
    gl.depthMask(true);
  }

  private drawGrid(camera: Camera, viewProj: Mat4): void {
    const gl = this.gl;
    const invVP = Mat4.invert(viewProj);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    gl.depthMask(false);
    this.grid.use();
    this.grid.setMat4('uInvViewProj', invVP.m);
    this.grid.setMat4('uViewProj', viewProj.m);
    this.grid.setVec3('uCamPos', camera.eye.x, camera.eye.y, camera.eye.z);
    this.grid.setFloat('uCellSize', 0.5);
    this.grid.setFloat('uFadeDist', 120);
    this.drawFullscreenTri();
    gl.depthMask(true);
    gl.disable(gl.BLEND);
  }

  private drawItem(it: RenderItem, camera: Camera, lights: LightBatch, shadowsOn: boolean, dir: Light | null): void {
    const gl = this.gl;
    const sh = this.pbr;
    sh.use();

    sh.setMat4('uProj', camera.proj.m);
    sh.setMat4('uView', camera.view.m);
    sh.setMat4('uModel', it.model.m);

    // matriz normal = transpose(inverse(mat3(model))) — cálculo na CPU
    this.tmpNormalMat.copy(it.model).invert().transpose();
    const m = this.tmpNormalMat.m;
    const nm = this.tmpNormalArr;
    nm[0] = m[0]; nm[1] = m[4]; nm[2] = m[8];
    nm[3] = m[1]; nm[4] = m[5]; nm[5] = m[9];
    nm[6] = m[2]; nm[7] = m[6]; nm[8] = m[10];
    sh.setMat3('uNormalMat', nm);

    sh.setMat4('uLightVP', this.lightVP.m);
    sh.setVec3('uCameraPos', camera.eye.x, camera.eye.y, camera.eye.z);

    const mat = it.material;
    sh.setVec3('uAlbedoColor', mat.albedo.r, mat.albedo.g, mat.albedo.b);
    sh.setFloat('uMetallic', mat.metallic);
    sh.setFloat('uRoughness', mat.roughness);
    sh.setVec3('uEmissive', mat.emissive.r, mat.emissive.g, mat.emissive.b);
    sh.setFloat('uEmissiveIntensity', mat.emissiveIntensity);
    sh.setFloat('uOpacity', mat.opacity);
    sh.setFloat('uAlphaCutoff', mat.alphaCutoff);

    // texturas
    const albTex = mat.albedoTexture?.glTexture ?? this.whiteTex;
    sh.setTexture('uAlbedoMap', albTex, 0);
    sh.setTexture('uNormalMap', mat.normalTexture?.glTexture ?? this.whiteTex, 1);
    sh.setBool('uHasAlbedoMap', !!mat.albedoTexture);
    sh.setBool('uHasNormalMap', !!mat.normalTexture);

    // luzes
    if (dir) {
      sh.setVec3('uDirLightDir', dir.direction.x, dir.direction.y, dir.direction.z);
      sh.setVec3('uDirLightColor', dir.color.r, dir.color.g, dir.color.b);
      sh.setFloat('uDirLightIntensity', dir.intensity);
    } else {
      sh.setVec3('uDirLightDir', 0, -1, 0);
      sh.setVec3('uDirLightColor', 0, 0, 0);
      sh.setFloat('uDirLightIntensity', 0);
    }

    sh.setInt('uNumPointLights', lights.points.length);
    const pArr = this.pArr, pcArr = this.pcArr, piArr = this.piArr, prArr = this.prArr;
    pArr.fill(0); pcArr.fill(0); piArr.fill(0); prArr.fill(1);
    lights.points.forEach((l, i) => {
      pArr[i * 3] = l.worldPos.x; pArr[i * 3 + 1] = l.worldPos.y; pArr[i * 3 + 2] = l.worldPos.z;
      pcArr[i * 3] = l.color.r; pcArr[i * 3 + 1] = l.color.g; pcArr[i * 3 + 2] = l.color.b;
      piArr[i] = l.intensity;
      prArr[i] = l.range;
    });
    sh.setVec3Arr('uPointPos', pArr);
    sh.setVec3Arr('uPointColor', pcArr);
    sh.setFloatArr('uPointIntensity', piArr);
    sh.setFloatArr('uPointRange', prArr);

    sh.setInt('uNumSpotLights', lights.spots.length);
    const sPos = this.sPos, sDir = this.sDir, sCol = this.sCol;
    const sInt = this.sInt, sRange = this.sRange, sCos = this.sCos;
    sPos.fill(0); sDir.fill(0); sCol.fill(0); sInt.fill(0); sRange.fill(1); sCos.fill(0.9);
    lights.spots.forEach((l, i) => {
      sPos[i * 3] = l.worldPos.x; sPos[i * 3 + 1] = l.worldPos.y; sPos[i * 3 + 2] = l.worldPos.z;
      sDir[i * 3] = l.direction.x; sDir[i * 3 + 1] = l.direction.y; sDir[i * 3 + 2] = l.direction.z;
      sCol[i * 3] = l.color.r; sCol[i * 3 + 1] = l.color.g; sCol[i * 3 + 2] = l.color.b;
      sInt[i] = l.intensity;
      sRange[i] = l.range;
      sCos[i] = Math.cos(l.angle);
    });
    sh.setVec3Arr('uSpotPos', sPos);
    sh.setVec3Arr('uSpotDir', sDir);
    sh.setVec3Arr('uSpotColor', sCol);
    sh.setFloatArr('uSpotIntensity', sInt);
    sh.setFloatArr('uSpotRange', sRange);
    sh.setFloatArr('uSpotCosAngle', sCos);

    const s = this.settings;
    sh.setVec3('uAmbientSky', s.ambientSky.r, s.ambientSky.g, s.ambientSky.b);
    sh.setVec3('uAmbientGround', s.ambientGround.r, s.ambientGround.g, s.ambientGround.b);
    sh.setFloat('uAmbientIntensity', s.ambientIntensity);

    sh.setTexture('uShadowMap', this.shadowFBO?.colorTex ?? null, 2);
    sh.setFloat('uShadowStrength', dir ? dir.shadowStrength : 0);
    sh.setBool('uShadowsEnabled', shadowsOn);

    // face culling
    if (mat.doubleSided) gl.disable(gl.CULL_FACE);
    else {
      gl.enable(gl.CULL_FACE);
      gl.cullFace(gl.BACK);
    }

    it.mesh.draw();
    this.stats.drawCalls++;
    this.stats.triangles += it.mesh.indexCount / 3;
  }

  /** Gera o mapa de sombras da luz direcional cobrindo a cena. */
  private renderShadowMap(dir: Light, items: RenderItem[], camera: Camera): void {
    const gl = this.gl;
    const fbo = this.shadowFBO!;

    // bounds da cena (ou da vizinhança da câmera — o que for menor)
    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    for (const it of items) {
      const c = it.worldCenter, r = it.worldRadius;
      minX = Math.min(minX, c.x - r); maxX = Math.max(maxX, c.x + r);
      minY = Math.min(minY, c.y - r); maxY = Math.max(maxY, c.y + r);
      minZ = Math.min(minZ, c.z - r); maxZ = Math.max(maxZ, c.z + r);
    }
    if (minX === Infinity) {
      // cena vazia: caixa padrão em torno da câmera
      const e = camera.eye;
      minX = e.x - 10; maxX = e.x + 10; minY = e.y - 10; maxY = e.y + 10; minZ = e.z - 10; maxZ = e.z + 10;
    }
    const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2, cz = (minZ + maxZ) / 2;
    const radius = Math.max(maxX - minX, maxY - minY, maxZ - minZ) * 0.72 + 2;

    const lightDir = dir.direction.clone().norm();
    const dist = radius * 3;
    const eye = new Vec3(cx - lightDir.x * dist, cy - lightDir.y * dist, cz - lightDir.z * dist);
    const lightView = Mat4.lookAt(eye, new Vec3(cx, cy, cz), new Vec3(0, 1, 0));
    const lightProj = Mat4.ortho(-radius, radius, -radius, radius, 0.5, dist * 2 + radius);
    this.lightVP.copy(lightProj).multiply(lightView);

    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo.fbo);
    gl.viewport(0, 0, fbo.width, fbo.height);
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.clear(gl.DEPTH_BUFFER_BIT);

    this.shadow.use();
    this.shadow.setMat4('uLightVP', this.lightVP.m);
    for (const it of items) {
      // ignora transparentes no shadow map
      if (it.transparent) continue;
      this.shadow.setMat4('uModel', it.model.m);
      it.mesh.draw();
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    void gl;
  }
}
