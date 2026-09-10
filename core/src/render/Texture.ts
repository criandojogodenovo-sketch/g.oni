/**
 * G.oni Llumni — Texturas WebGL2
 * Suporte a imagens, canvas e cores sólidas, com cache por id.
 */

export type TextureSource = HTMLImageElement | HTMLCanvasElement | ImageBitmap | HTMLVideoElement;

export class Texture {
  readonly glTexture: WebGLTexture;
  width = 1;
  height = 1;
  /** id lógico para serialização em .g.oni (ex.: "textures/minha_textura") */
  id: string | null = null;

  constructor(private gl: WebGL2RenderingContext) {
    this.glTexture = gl.createTexture()!;
    this.bind();
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([255, 255, 255, 255]));
    this.setDefaultParams();
  }

  bind(unit = 0): void {
    const gl = this.gl;
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, this.glTexture);
  }

  private setDefaultParams(): void {
    const gl = this.gl;
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
  }

  /** Carrega uma fonte de imagem (canvas 2D, ImageBitmap, etc.). */
  upload(source: TextureSource, generateMipmaps = true): void {
    const gl = this.gl;
    this.bind();
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source as TexImageSource);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    this.width = (source as { width?: number }).width ?? 1;
    this.height = (source as { height?: number }).height ?? 1;
    if (generateMipmaps) {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.generateMipmap(gl.TEXTURE_2D);
    }
  }

  dispose(): void {
    this.gl.deleteTexture(this.glTexture);
  }

  /** Cria textura a partir de URL (async). */
  static fromUrl(gl: WebGL2RenderingContext, url: string): Promise<Texture> {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.crossOrigin = 'anonymous';
      img.onload = () => {
        const t = new Texture(gl);
        t.upload(img);
        resolve(t);
      };
      img.onerror = () => reject(new Error(`Falha ao carregar textura: ${url}`));
      img.src = url;
    });
  }

  static fromCanvas(gl: WebGL2RenderingContext, canvas: HTMLCanvasElement, mips = true): Texture {
    const t = new Texture(gl);
    t.upload(canvas, mips);
    return t;
  }
}
