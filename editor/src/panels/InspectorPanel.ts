/**
 * G.oni Llumni — Inspetor (propriedades do objeto selecionado)
 * Transform, componentes, material PBR, física, câmera, luz, script, animação.
 */

import { el, toast, numberField, promptModal } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { GOniObject, MeshComponent, LightComponent, CameraComponent, ColliderComponent, RigidBodyComponent, ScriptComponent, AnimationComponent } from '@core/goni/Objetos';
import { Color, MathUtils, Vec3 } from '@core/math';
import { PRIMITIVES } from '@core/render/primitives';

export class InspectorPanel {
  constructor(private app: EditorApp) {}

  render(body: HTMLElement): void {
    body.innerHTML = '';
    const obj = this.app.viewport.selected;
    if (!obj) {
      body.append(el('p', { class: 'hint' }, 'Nenhum objeto selecionado. Toque em um objeto no viewport ou na hierarquia.'));
      return;
    }

    // ---------- identidade ----------
    const idGroup = group('Objeto');
    const nameInput = el('input', { type: 'text', value: obj.name }) as HTMLInputElement;
    nameInput.addEventListener('change', () => {
      obj.name = nameInput.value.trim() || 'Objeto';
      this.app.refresh();
    });
    idGroup.append(row('Nome', nameInput));
    idGroup.append(row('Visível', checkbox(obj.visible, (v) => {
      obj.visible = v;
      obj.markDirty();
    })));
    const tagInput = el('input', {
      type: 'text', value: obj.tags.join(', '), placeholder: 'tags separadas por vírgula',
    }) as HTMLInputElement;
    tagInput.addEventListener('change', () => {
      obj.tags = tagInput.value.split(',').map((t) => t.trim()).filter(Boolean);
    });
    idGroup.append(row('Tags', tagInput));
    body.append(idGroup);

    // ---------- transform ----------
    const tg = group('Transform');
    tg.append(vecRow('Posição', obj.transform.position, 0.1, (v) => { obj.transform.position.copy(v); obj.markDirty(); }));
    const rotDeg = new Vec3(
      MathUtils.radToDeg(obj.transform.rotation.x),
      MathUtils.radToDeg(obj.transform.rotation.y),
      MathUtils.radToDeg(obj.transform.rotation.z)
    );
    tg.append(vecRow('Rotação°', rotDeg, 5, (v) => {
      obj.transform.rotation.set(MathUtils.degToRad(v.x), MathUtils.degToRad(v.y), MathUtils.degToRad(v.z));
      obj.markDirty();
    }));
    tg.append(vecRow('Escala', obj.transform.scale, 0.05, (v) => { obj.transform.scale.copy(v); obj.markDirty(); }));
    body.append(tg);

    // ---------- componentes ----------
    for (const comp of obj.components) {
      switch (comp.type) {
        case 'mesh': this.renderMesh(body, obj, comp as MeshComponent); break;
        case 'light': this.renderLight(body, comp as LightComponent); break;
        case 'camera': this.renderCamera(body, comp as CameraComponent); break;
        case 'collider': this.renderCollider(body, comp as ColliderComponent); break;
        case 'rigidbody': this.renderRigidBody(body, comp as RigidBodyComponent); break;
        case 'script': this.renderScript(body, obj, comp as ScriptComponent); break;
        case 'animation': this.renderAnimation(body, obj, comp as AnimationComponent); break;
      }
    }

    // ---------- adicionar componentes ----------
    const addGroup = group('Adicionar componente');
    const chips = el('div', { class: 'chip-row' });
    const existing = new Set(obj.components.map((c) => c.type));
    const options: [string, () => void][] = [
      ...(!existing.has('mesh') ? [['Malha', () => {
        const m = new MeshComponent();
        m.meshId = 'prim:cube';
        obj.addComponent(m);
        this.app.refresh();
      }]] : []),
      ...(!existing.has('light') ? [['Luz', () => {
        obj.addComponent(new LightComponent());
        this.app.refresh();
      }]] : []),
      ...(!existing.has('camera') ? [['Câmera', () => {
        const c = new CameraComponent();
        c.primary = !this.app.engine.scene.all().some((o) => o.getComponent('camera'));
        obj.addComponent(c);
        this.app.refresh();
      }]] : []),
      ...(!existing.has('collider') ? [['Colisor', () => {
        const col = new ColliderComponent();
        const mesh = obj.getComponent<MeshComponent>('mesh');
        if (mesh?.meshId === 'prim:sphere') col.collider.shape = 'sphere';
        if (mesh?.meshId === 'prim:capsule') col.collider.shape = 'capsule';
        obj.addComponent(col);
        this.app.refresh();
      }]] : []),
      ...(!existing.has('rigidbody') ? [['Corpo rígido', () => {
        const rb = new RigidBodyComponent();
        obj.addComponent(rb);
        this.app.refresh();
      }]] : []),
      ...(!existing.has('script') ? [['Script', () => {
        obj.addComponent(new ScriptComponent());
        this.app.refresh();
      }]] : []),
      ...(!existing.has('animation') ? [['Animação', () => {
        obj.addComponent(new AnimationComponent());
        this.app.refresh();
      }]] : []),
    ] as [string, () => void][];
    for (const [label, fn] of options) {
      chips.append(el('button', { class: 'chip-btn', onclick: fn }, label));
    }
    addGroup.append(chips);
    body.append(addGroup);

    // ---------- ações ----------
    const ag = group('Ações');
    const actions = el('div', { style: 'display:flex;flex-wrap:wrap;gap:8px' });
    actions.append(
      el('button', { onclick: () => this.app.duplicateSelected() }, 'Duplicar'),
      el('button', { onclick: () => void this.app.createPrefabFromSelected() }, 'Criar prefab'),
      el('button', { onclick: () => this.app.focusSelected() }, 'Focar'),
      el('button', {
        class: 'danger',
        onclick: () => this.app.deleteSelected(),
      }, 'Excluir'),
    );
    ag.append(actions);
    body.append(ag);
  }

  // ---------------- renderizadores por componente ----------------

  private renderMesh(body: HTMLElement, obj: GOniObject, comp: MeshComponent): void {
    const g = group('Malha');
    const select = el('select') as HTMLSelectElement;
    const customMeshes = this.app.engine.renderer.meshRegistry.listIds().filter((id) => id.startsWith('asset:'));
    for (const prim of Object.keys(PRIMITIVES)) {
      select.append(el('option', { value: `prim:${prim}` }, `Primitiva: ${prim}`));
    }
    for (const id of customMeshes) {
      select.append(el('option', { value: id }, `Editada: ${id.slice(6)}`));
    }
    select.value = comp.meshId;
    if (![...select.options].some((o) => o.value === comp.meshId)) {
      select.append(el('option', { value: comp.meshId, selected: '' }, comp.meshId));
    }
    select.addEventListener('change', () => {
      comp.meshId = select.value;
      this.app.engine.ensureMesh(comp.meshId);
      obj.markDirty();
      this.app.refresh();
    });
    g.append(row('Forma', select));

    // material PBR
    const mat = comp.material;
    g.append(colorRow('Cor base', mat.albedo, (c) => { mat.albedo = c; }));
    g.append(sliderRow('Rugosidade', mat.roughness, 0, 1, 0.01, (v) => { mat.roughness = v; }));
    g.append(sliderRow('Metálico', mat.metallic, 0, 1, 0.01, (v) => { mat.metallic = v; }));
    g.append(sliderRow('Opacidade', mat.opacity, 0.05, 1, 0.01, (v) => { mat.opacity = v; }));
    g.append(colorRow('Emissivo', mat.emissive, (c) => { mat.emissive = c; }));
    g.append(sliderRow('Emiss. força', mat.emissiveIntensity, 0, 4, 0.05, (v) => { mat.emissiveIntensity = v; }));
    g.append(row('2 lados', checkbox(mat.doubleSided, (v) => { mat.doubleSided = v; })));

    if (mat.paintCanvas) {
      g.append(el('p', { class: 'hint' }, 'Textura de pintura ativa (painel Pintar).'));
    }

    // LOD
    g.append(sliderRow('LOD dist.', comp.lodDistance, 0, 60, 1, (v) => { comp.lodDistance = v; }));
    if (comp.lodDistance > 0) {
      const lodSel = el('select') as HTMLSelectElement;
      lodSel.append(el('option', { value: '' }, '— gerar automático —'));
      for (const id of customMeshes) lodSel.append(el('option', { value: id }, id.slice(6)));
      lodSel.value = comp.lodMeshId ?? '';
      lodSel.addEventListener('change', () => { comp.lodMeshId = lodSel.value || null; });
      g.append(row('LOD malha', lodSel));
      g.append(el('button', {
        class: 'small',
        style: 'width:100%',
        onclick: () => {
          const entry = this.app.engine.renderer.meshRegistry.get(comp.meshId);
          if (!entry) return;
          const lodId = `asset:lod_${Date.now().toString(36)}`;
          this.app.engine.registerCustomMesh(entry.data.simplify(10), lodId);
          comp.lodMeshId = lodId;
          toast('LOD simplificado gerado');
          this.app.refresh();
        },
      }, 'Gerar LOD automático'));
    }

    // textura albedo importada
    const texBtn = el('button', {
      class: 'small',
      style: 'width:100%',
      onclick: () => void this.app.importTextureFor(comp),
    }, mat.albedoTexture ? 'Trocar textura…' : 'Importar textura…');
    g.append(texBtn);
    if (mat.albedoTexture) {
      g.append(el('button', {
        class: 'small ghost',
        style: 'width:100%;margin-top:6px',
        onclick: () => { mat.albedoTexture = null; this.app.refresh(); },
      }, 'Remover textura'));
    }

    body.append(g);
  }

  private renderLight(body: HTMLElement, comp: LightComponent): void {
    const g = group('Luz');
    const l = comp.light;
    const typeSel = el('select') as HTMLSelectElement;
    for (const t of ['directional', 'point', 'spot']) {
      typeSel.append(el('option', { value: t }, t === 'directional' ? 'Direcional' : t === 'point' ? 'Pontual' : 'Spot'));
    }
    typeSel.value = l.type;
    typeSel.addEventListener('change', () => { l.type = typeSel.value as typeof l.type; this.app.refresh(); });
    g.append(row('Tipo', typeSel));
    g.append(colorRow('Cor', l.color, (c) => { l.color = c; }));
    g.append(sliderRow('Intensidade', l.intensity, 0, 8, 0.05, (v) => { l.intensity = v; }));
    if (l.type !== 'directional') {
      g.append(sliderRow('Alcance', l.range, 0.5, 60, 0.5, (v) => { l.range = v; }));
    }
    if (l.type === 'spot') {
      g.append(sliderRow('Ângulo°', MathUtils.radToDeg(l.angle), 5, 80, 1, (v) => { l.angle = MathUtils.degToRad(v); }));
    }
    if (l.type === 'directional') {
      g.append(row('Sombras', checkbox(l.castShadows, (v) => { l.castShadows = v; })));
      g.append(sliderRow('Sombra força', l.shadowStrength, 0, 1, 0.05, (v) => { l.shadowStrength = v; }));
    }
    body.append(g);
  }

  private renderCamera(body: HTMLElement, comp: CameraComponent): void {
    const g = group('Câmera');
    g.append(sliderRow('FOV°', comp.fovDegrees, 20, 110, 1, (v) => { comp.fovDegrees = v; }));
    g.append(row('Principal', checkbox(comp.primary, (v) => { comp.primary = v; })));
    g.append(sliderRow('Near', comp.near, 0.05, 2, 0.05, (v) => { comp.near = v; }));
    g.append(sliderRow('Far', comp.far, 50, 2000, 10, (v) => { comp.far = v; }));
    body.append(g);
  }

  private renderCollider(body: HTMLElement, comp: ColliderComponent): void {
    const g = group('Colisor');
    const c = comp.collider;
    const sel = el('select') as HTMLSelectElement;
    for (const s of ['box', 'sphere', 'capsule']) {
      sel.append(el('option', { value: s }, s === 'box' ? 'Caixa' : s === 'sphere' ? 'Esfera' : 'Cápsula'));
    }
    sel.value = c.shape;
    sel.addEventListener('change', () => { c.shape = sel.value as typeof c.shape; this.app.refresh(); });
    g.append(row('Forma', sel));
    if (c.shape === 'box') {
      g.append(vecRow('Meia-ext.', c.size, 0.05, (v) => c.size.copy(v)));
    } else {
      g.append(sliderRow('Raio', c.radius, 0.05, 5, 0.05, (v) => { c.radius = v; }));
    }
    if (c.shape === 'capsule') {
      g.append(sliderRow('Altura', c.height, 0.2, 4, 0.1, (v) => { c.height = v; }));
    }
    g.append(row('Gatilho', checkbox(c.isTrigger, (v) => { c.isTrigger = v; })));
    g.append(el('button', {
      class: 'small',
      style: 'width:100%',
      onclick: () => this.app.autoFitCollider(),
    }, 'Ajustar à malha'));
    body.append(g);
  }

  private renderRigidBody(body: HTMLElement, comp: RigidBodyComponent): void {
    const g = group('Corpo rígido');
    const rb = comp.body;
    const sel = el('select') as HTMLSelectElement;
    for (const t of ['dynamic', 'static', 'kinematic']) {
      sel.append(el('option', { value: t }, t === 'dynamic' ? 'Dinâmico' : t === 'static' ? 'Estático' : 'Cinemático'));
    }
    sel.value = rb.type;
    sel.addEventListener('change', () => { rb.type = sel.value as typeof rb.type; this.app.refresh(); });
    g.append(row('Tipo', sel));
    if (rb.type === 'dynamic') {
      g.append(sliderRow('Massa', rb.mass, 0.1, 100, 0.1, (v) => { rb.mass = v; }));
      g.append(sliderRow('Restituição', rb.restitution, 0, 1, 0.05, (v) => { rb.restitution = v; }));
      g.append(sliderRow('Atrito', rb.friction, 0, 1, 0.05, (v) => { rb.friction = v; }));
      g.append(row('Gravidade', checkbox(rb.useGravity, (v) => { rb.useGravity = v; })));
      g.append(row('Congelar rotação', checkbox(rb.freezeRotation, (v) => { rb.freezeRotation = v; })));
    }
    g.append(el('p', { class: 'hint' }, 'Em objetos com colisor cápsula, o play mode cria um character controller (cc.move()).'));
    body.append(g);
  }

  private renderScript(body: HTMLElement, obj: GOniObject, comp: ScriptComponent): void {
    const g = group('Script');
    const sel = el('select') as HTMLSelectElement;
    sel.append(el('option', { value: '' }, '— nenhum —'));
    for (const name of [...this.app.engine.scriptSources.keys()].sort()) {
      sel.append(el('option', { value: name }, name));
    }
    sel.value = comp.scriptName;
    sel.addEventListener('change', () => {
      comp.scriptName = sel.value;
      this.app.refresh();
    });
    g.append(row('Script', sel));
    g.append(el('button', {
      class: 'small',
      style: 'width:100%',
      onclick: () => this.app.openScriptEditor(comp.scriptName),
    }, 'Abrir editor de script'));
    g.append(el('button', {
      class: 'small ghost',
      style: 'width:100%;margin-top:6px',
      onclick: () => {
        obj.removeComponent(comp);
        this.app.refresh();
      },
    }, 'Remover componente'));
    body.append(g);
  }

  private renderAnimation(body: HTMLElement, obj: GOniObject, comp: AnimationComponent): void {
    const g = group('Animação');
    for (const clip of comp.clips) {
      g.append(el('div', {
        style: 'display:flex;gap:8px;align-items:center;margin-bottom:6px',
      },
        el('span', { style: 'flex:1' }, `${clip.name} (${clip.duration.toFixed(1)}s · ${clip.tracks.length} trilhas)`),
        el('button', {
          class: 'small',
          onclick: () => { comp.playing = clip.name; },
        }, 'Tocar'),
        el('button', {
          class: 'small danger',
          onclick: () => {
            comp.clips = comp.clips.filter((c) => c !== clip);
            this.app.refresh();
          },
        }, '✕'),
      ));
    }
    g.append(row('Loop', checkbox(comp.loop, (v) => { comp.loop = v; })));
    g.append(row('Auto-play', checkbox(comp.autoplay, (v) => { comp.autoplay = v; })));
    g.append(el('button', {
      class: 'small',
      style: 'width:100%',
      onclick: () => this.app.openAnimationEditor(),
    }, 'Editor de animação'));
    body.append(g);
  }
}

// ---------------- helpers de UI ----------------

function group(title: string): HTMLElement {
  return el('div', { class: 'insp-group' }, el('h3', {}, title));
}

function row(label: string, control: HTMLElement): HTMLElement {
  return el('div', { class: 'insp-row' }, el('label', {}, label), el('div', { class: 'val' }, control));
}

function checkbox(value: boolean, onChange: (v: boolean) => void): HTMLInputElement {
  const c = el('input', { type: 'checkbox' }) as HTMLInputElement;
  c.checked = value;
  c.style.width = '22px';
  c.style.height = '22px';
  c.addEventListener('change', () => onChange(c.checked));
  return c;
}

function sliderRow(label: string, value: number, min: number, max: number, step: number, onChange: (v: number) => void): HTMLElement {
  const val = el('span', { style: 'font-size:11px;color:var(--text-3);min-width:38px;text-align:right;font-family:var(--font-mono)' }, value.toFixed(2));
  const r = el('input', { type: 'range', min, max, step, value }) as HTMLInputElement;
  r.addEventListener('input', () => {
    const v = parseFloat(r.value);
    val.textContent = v.toFixed(2);
    onChange(v);
  });
  const wrap = el('div', { style: 'display:flex;align-items:center;gap:8px;flex:1' }, r, val);
  return row(label, wrap);
}

function colorRow(label: string, value: Color, onChange: (c: Color) => void): HTMLElement {
  const input = el('input', { type: 'color', value: value.toCSS() }) as HTMLInputElement;
  input.addEventListener('input', () => {
    onChange(Color.hex(input.value));
  });
  return row(label, input);
}

function vecRow(label: string, value: Vec3, step: number, onChange: (v: Vec3) => void): HTMLElement {
  const grid = el('div', { class: 'vec3-row' });
  const axes: ('x' | 'y' | 'z')[] = ['x', 'y', 'z'];
  const inputs: HTMLInputElement[] = [];
  for (const axis of axes) {
    const axisDiv = el('div', { class: `axis ${axis}` });
    const input = el('input', { type: 'number', step }) as HTMLInputElement;
    input.value = value[axis].toFixed(2);
    input.addEventListener('input', () => {
      const v = parseFloat(input.value);
      if (!isNaN(v)) {
        const nv = new Vec3(inputs[0] ? parseFloat(inputs[0].value) || 0 : 0, inputs[1] ? parseFloat(inputs[1].value) || 0 : 0, inputs[2] ? parseFloat(inputs[2].value) || 0 : 0);
        onChange(nv);
      }
    });
    inputs.push(input);
    axisDiv.append(input);
    grid.append(axisDiv);
  }
  return row(label, grid);
}

export { numberField };
