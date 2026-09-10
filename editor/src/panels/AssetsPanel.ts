/**
 * G.oni Llumni — Painel de Assets e Configurações do Projeto
 * Explora o sistema de arquivos virtual, importa texturas,
 * edita configurações de render/física e exporta o .g.oni.
 */

import { el, toast } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { Color } from '@core/math';
import { MeshComponent } from '@core/goni/Objetos';
import { STANDARD_FOLDERS } from '@projects/storage/vfs';

export class AssetsPanel {
  constructor(private app: EditorApp) {}

  async render(body: HTMLElement): Promise<void> {
    body.innerHTML = '';
    const files = await this.app.vfs.list();

    // --------- navegador de arquivos ---------
    body.append(el('div', { class: 'section-title' }, 'Arquivos do projeto'));
    for (const folder of STANDARD_FOLDERS) {
      const folderFiles = files.filter((f) => f.path.startsWith(`${folder}/`));
      const group = el('div', { class: 'insp-group' },
        el('h3', {}, `${folder}/ · ${folderFiles.length} arquivo${folderFiles.length === 1 ? '' : 's'}`),
      );
      if (folderFiles.length === 0) {
        group.append(el('p', { class: 'hint', style: 'margin:4px 0' }, 'vazio'));
      } else {
        for (const f of folderFiles) {
          const sizeKb = (new Blob([f.data]).size / 1024).toFixed(1);
          group.append(el('div', {
            style: 'display:flex;justify-content:space-between;padding:6px 4px;font-size:12px;border-bottom:1px solid var(--border)',
          },
            el('span', { style: 'overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1' }, f.path.split('/').pop() ?? f.path),
            el('span', { style: 'color:var(--text-3);flex:none' }, `${sizeKb} kB`),
          ));
        }
      }
      body.append(group);
    }

    // --------- importar imagem ---------
    body.append(el('div', { class: 'section-title' }, 'Importar'));
    const importBtn = el('button', {
      style: 'width:100%',
      onclick: () => void this.importImage(),
    }, '🖼 Importar imagem (textura)');
    body.append(importBtn);

    // --------- configurações do mundo ---------
    body.append(el('div', { class: 'section-title' }, 'Mundo — Render'));
    const env = this.app.engine.scene.environment;
    const renderGroup = el('div', { class: 'insp-group' }, el('h3', {}, 'Céu e pós-processamento'));
    renderGroup.append(colorSetting(renderGroup, 'Zênite', env.skyZenith));
    renderGroup.append(colorSetting(renderGroup, 'Horizonte', env.skyHorizon));
    renderGroup.append(colorSetting(renderGroup, 'Ambiente céu', env.ambientSky));
    renderGroup.append(colorSetting(renderGroup, 'Ambiente chão', env.ambientGround));
    renderGroup.append(slider('Ambiente', env.ambientIntensity, 0, 3, 0.05, (v) => { env.ambientIntensity = v; }));
    renderGroup.append(slider('Exposição', env.exposure, 0.2, 3, 0.05, (v) => { env.exposure = v; }));
    renderGroup.append(slider('Bloom', env.bloomStrength, 0, 2, 0.05, (v) => { env.bloomStrength = v; }));
    renderGroup.append(slider('Bloom limiar', env.bloomThreshold, 0, 3, 0.05, (v) => { env.bloomThreshold = v; }));
    renderGroup.append(slider('Vinheta', env.vignette, 0, 1, 0.02, (v) => { env.vignette = v; }));
    renderGroup.append(toggle('Sombras', env.shadowsEnabled, (v) => { env.shadowsEnabled = v; }));
    renderGroup.append(toggle('Pós-processamento', env.postEnabled, (v) => { env.postEnabled = v; }));
    body.append(renderGroup);

    const physGroup = el('div', { class: 'insp-group' }, el('h3', {}, 'Física'));
    physGroup.append(slider('Gravidade', this.app.engine.scene.gravity, -30, 0, 0.5, (v) => {
      this.app.engine.scene.gravity = v;
    }));
    body.append(physGroup);

    const goniGroup = el('div', { class: 'insp-group' }, el('h3', {}, 'G.oni Construt'));
    goniGroup.append(slider('Snap grid', this.app.engine.construt.gridSnap, 0, 2, 0.25, (v) => {
      this.app.engine.construt.gridSnap = v;
    }));
    const prefabNames = [...this.app.engine.construt.prefabs.keys()];
    goniGroup.append(el('p', { class: 'hint' }, `Prefabs: ${prefabNames.length ? prefabNames.join(', ') : 'nenhum'}`));
    body.append(goniGroup);

    // --------- exportar ---------
    body.append(el('div', { class: 'section-title' }, 'Exportar'));
    body.append(el('button', {
      class: 'primary',
      style: 'width:100%',
      onclick: () => void this.app.exportProject(),
    }, '⬇ Exportar projeto (.g.oni)'));
  }

  private async importImage(): Promise<void> {
    const input = el('input', { type: 'file', accept: 'image/*' }) as HTMLInputElement;
    input.style.display = 'none';
    document.body.append(input);
    input.addEventListener('change', async () => {
      const file = input.files?.[0];
      input.remove();
      if (!file) return;
      try {
        const bmp = await createImageBitmap(file);
        const id = `tex_${Date.now().toString(36)}`;
        this.app.engine.registerTexture(id, bmp);
        // guarda dataURL no VFS
        const c = document.createElement('canvas');
        c.width = bmp.width;
        c.height = bmp.height;
        c.getContext('2d')?.drawImage(bmp, 0, 0);
        await this.app.vfs.write(`textures/${id}.json`, JSON.stringify({ dataUrl: c.toDataURL('image/png') }));
        // aplica no objeto selecionado
        const sel = this.app.viewport.selected;
        const meshComp = sel?.getComponent<MeshComponent>('mesh');
        if (meshComp && sel) {
          const tex = this.app.engine.registerTexture(id, c);
          meshComp.material.albedoTexture = tex;
          toast(`Textura aplicada em ${sel.name}`);
        } else {
          toast('Textura importada (selecione um objeto e aplique no Inspetor)');
        }
        this.app.markDirty();
      } catch (err) {
        toast(`Falha ao importar: ${(err as Error).message}`, 'err');
      }
    });
    input.click();
  }
}

function colorSetting(_group: HTMLElement, label: string, color: Color): HTMLElement {
  const input = el('input', { type: 'color', value: color.toCSS() }) as HTMLInputElement;
  input.addEventListener('input', () => {
    const nc = Color.hex(input.value);
    color.r = nc.r; color.g = nc.g; color.b = nc.b;
  });
  return el('div', { class: 'insp-row' }, el('label', {}, label), el('div', { class: 'val' }, input));
}

function slider(label: string, value: number, min: number, max: number, step: number, onChange: (v: number) => void): HTMLElement {
  const val = el('span', { style: 'font-size:11px;color:var(--text-3);min-width:38px;text-align:right;font-family:var(--font-mono)' }, value.toFixed(2));
  const r = el('input', { type: 'range', min, max, step, value }) as HTMLInputElement;
  r.addEventListener('input', () => {
    const v = parseFloat(r.value);
    val.textContent = v.toFixed(2);
    onChange(v);
  });
  return el('div', { class: 'insp-row' }, el('label', {}, label), el('div', { class: 'val' }, el('div', { style: 'display:flex;align-items:center;gap:8px' }, r, val)));
}

function toggle(label: string, value: boolean, onChange: (v: boolean) => void): HTMLElement {
  const c = el('input', { type: 'checkbox' }) as HTMLInputElement;
  c.checked = value;
  c.style.width = '22px';
  c.style.height = '22px';
  c.addEventListener('change', () => onChange(c.checked));
  return el('div', { class: 'insp-row' }, el('label', {}, label), el('div', { class: 'val' }, c));
}
