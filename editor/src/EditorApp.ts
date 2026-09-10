/**
 * G.oni Llumni — EditorApp
 * Layout mobile-first: topbar + viewport + dock + bottom sheets.
 * Comandos: seleção, gizmos, play, undo/redo, salvar, exportar.
 */

import { el, toast, iconBtn, Icons, confirmModal } from '@editor/ui/dom';
import './ui/style.css';
import { Engine, EngineCallbacks } from '@core/goni/Engine';
import { Viewport } from '@editor/viewport/Viewport';
import { GizmoMode } from '@editor/viewport/Gizmo';
import { HierarchyPanel } from '@editor/panels/HierarchyPanel';
import { InspectorPanel } from '@editor/panels/InspectorPanel';
import { ScriptPanel } from '@editor/panels/ScriptPanel';
import { AssetsPanel } from '@editor/panels/AssetsPanel';
import { VisualScriptingPanel } from '@editor/goni/VisualScripting';
import { ModelingPanel } from '@editor/modeling/ModelingPanel';
import { AnimationPanel } from '@editor/animation/AnimationPanel';
import { TexturePanel } from '@editor/texturing/TexturePanel';
import { PlayOverlay } from '@editor/play/PlayOverlay';
import { VirtualFS } from '@projects/storage/vfs';
import { GOniObject, MeshComponent, LightComponent, CameraComponent, ColliderComponent, RigidBodyComponent, ScriptComponent } from '@core/goni/Objetos';
import { Scene } from '@core/goni/Scene';
import { Camera } from '@core/render/Camera';
import { Color, Vec3 } from '@core/math';
import { PRIMITIVES } from '@core/render/primitives';
import { downloadProject } from '@projects/ProjectsScreen';

type SheetId = 'hierarchy' | 'inspector' | 'scripts' | 'visual' | 'modeling' | 'animation' | 'paint' | 'assets';

export class EditorApp {
  engine: Engine;
  viewport!: Viewport;
  vfs: VirtualFS;
  projectId: string;
  projectName: string;

  hierarchy: HierarchyPanel;
  inspector: InspectorPanel;
  scripts: ScriptPanel;
  assets: AssetsPanel;
  visual: VisualScriptingPanel;
  modeling: ModelingPanel;
  animation: AnimationPanel;
  texture: TexturePanel;
  playOverlay!: PlayOverlay;

  /** callback de pintura no viewport (painel de texturização) */
  onViewportPaint: ((x: number, y: number, w: number, h: number, isStart: boolean) => void) | null = null;

  private root!: HTMLElement;
  private sheet!: HTMLElement;
  private sheetBackdrop!: HTMLElement;
  private sheetTitle!: HTMLElement;
  private sheetBody!: HTMLElement;
  private openSheetId: SheetId | null = null;
  private undoStack: string[] = [];
  private redoStack: string[] = [];
  private dirty = false;
  private playBtn!: HTMLButtonElement;
  private lastSave = 0;

  engineCallbacks: EngineCallbacks = {};

  constructor(container: HTMLElement, projectId: string) {
    this.projectId = projectId;
    this.vfs = new VirtualFS(projectId);

    this.engine = new Engine(this.engineCallbacks);

    this.hierarchy = new HierarchyPanel(this);
    this.inspector = new InspectorPanel(this);
    this.scripts = new ScriptPanel(this);
    this.assets = new AssetsPanel(this);
    this.visual = new VisualScriptingPanel(this);
    this.modeling = new ModelingPanel(this);
    this.animation = new AnimationPanel(this);
    this.texture = new TexturePanel(this);

    void this.boot(container);
  }

  private async boot(container: HTMLElement): Promise<void> {
    // carrega projeto do VFS
    const data = await this.vfs.loadProjectData();
    if (!data) {
      toast('Projeto vazio — criando cena padrão', 'err');
    }

    // layout
    this.root = el('div', { class: 'editor-app' });

    const viewportWrap = el('div', { class: 'ed-viewport-wrap' });
    const canvas = el('canvas') as HTMLCanvasElement;
    viewportWrap.append(canvas);

    // HUD
    const hud = el('div', { class: 'ed-hud' }, el('span', { class: 'chip' }, 'iniciando…'));

    // toolbar lateral (gizmos)
    const toolbar = el('div', { class: 'ed-toolbar' });
    const gizmoBtns: Record<GizmoMode, HTMLButtonElement> = {} as Record<GizmoMode, HTMLButtonElement>;
    const modes: [GizmoMode, keyof typeof Icons, string][] = [
      ['select', 'cursor', 'Selecionar'],
      ['translate', 'move', 'Mover'],
      ['rotate', 'rotate', 'Rotacionar'],
      ['scale', 'scale', 'Escalar'],
    ];
    for (const [mode, icon, title] of modes) {
      const b = iconBtn(icon, title, () => this.setGizmoMode(mode));
      gizmoBtns[mode] = b;
      toolbar.append(b);
    }
    toolbar.append(iconBtn('eye', 'Mostrar colisores', () => {
      this.viewport.showColliders = !this.viewport.showColliders;
    }));

    // topbar
    const playBtn = el('button', { class: 'primary', title: 'Play' }) as HTMLButtonElement;
    playBtn.append(Icons.play());
    playBtn.addEventListener('click', () => this.togglePlay());
    this.playBtn = playBtn;

    const topbar = el('div', { class: 'ed-topbar' },
      iconBtn('back', 'Voltar aos projetos', () => void this.backToProjects()),
      el('div', { class: 'title', id: 'ed-project-name' }, '—'),
      iconBtn('undo', 'Desfazer', () => this.undo()),
      iconBtn('save', 'Salvar', () => void this.save()),
      playBtn,
    );

    // dock inferior
    const dock = el('div', { class: 'ed-dock' });
    const dockItems: [SheetId | 'add', keyof typeof Icons, string][] = [
      ['add', 'plus', 'Adicionar'],
      ['hierarchy', 'hierarchy', 'Cena'],
      ['inspector', 'inspector', 'Objeto'],
      ['scripts', 'script', 'Script'],
      ['visual', 'visual', 'Nós'],
      ['modeling', 'cube', 'Modelar'],
      ['paint', 'brush', 'Pintar'],
      ['animation', 'clock', 'Animar'],
      ['assets', 'assets', 'Assets'],
    ];
    const dockBtns = new Map<string, HTMLButtonElement>();
    for (const [id, icon, label] of dockItems) {
      const b = el('button', { class: 'dock-item', title: label }) as HTMLButtonElement;
      b.append(Icons[icon]());
      b.append(el('span', {}, label));
      b.addEventListener('click', () => {
        if (id === 'add') this.openAddObjectMenu();
        else this.openSheet(id as SheetId);
      });
      dockBtns.set(id, b);
      dock.append(b);
    }

    // sheet
    this.sheetBackdrop = el('div', { class: 'sheet-backdrop' });
    this.sheetBackdrop.addEventListener('click', () => this.closeSheet());
    this.sheet = el('div', { class: 'sheet' },
      el('div', { class: 'sheet-handle' }),
      el('div', { class: 'sheet-head' },
        this.sheetTitle = el('h2', {}, '—'),
        el('button', { class: 'icon-btn ghost', onclick: () => this.closeSheet() }, '✕'),
      ),
      this.sheetBody = el('div', { class: 'sheet-body' }),
    );

    const toastZone = el('div', { class: 'toast-zone' });

    viewportWrap.append(hud, toolbar, this.sheetBackdrop, this.sheet, toastZone);
    this.root.append(topbar, viewportWrap, dock);
    container.append(this.root);

    // engine + viewport
    try {
      this.engine.init(canvas);
    } catch (err) {
      toast((err as Error).message, 'err');
      container.append(el('div', {
        style: 'position:absolute;inset:0;display:grid;place-items:center;padding:30px;text-align:center;color:var(--red)',
      }, (err as Error).message));
      return;
    }

    this.viewport = new Viewport(canvas.parentElement as HTMLElement, this.engine);
    this.viewport.hudElement = hud.querySelector('.chip') as HTMLElement;
    this.viewport.onSelectionChange = () => {
      if (this.openSheetId === 'inspector' || this.openSheetId === 'hierarchy') this.refresh();
    };
    this.viewport.gizmo.onBeginEdit = () => this.pushUndo();

    this.playOverlay = new PlayOverlay(viewportWrap, this.engine);

    // carrega dados
    if (data) {
      this.projectName = data.name;
      this.engine.projectName = data.name;
      this.engine.loadProject(data);
    } else {
      this.projectName = 'Projeto';
    }
    (topbar.querySelector('#ed-project-name') as HTMLElement).textContent = this.projectName;

    // autosave a cada 45s
    setInterval(() => {
      if (this.dirty && !this.engine.playing) void this.save(true);
    }, 45000);

    // aviso ao sair com alterações
    window.addEventListener('beforeunload', (e) => {
      if (this.dirty) {
        e.preventDefault();
        e.returnValue = '';
      }
    });

    // teclado: undo/redo/save
    window.addEventListener('keydown', (e) => {
      const target = e.target as HTMLElement;
      if (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.tagName === 'SELECT') return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        this.undo();
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') {
        e.preventDefault();
        this.redo();
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        void this.save();
      }
    });

    // pinta via viewport quando o painel de pintura estiver ativo
    canvas.addEventListener('pointerdown', (e) => {
      if (this.onViewportPaint && this.texture.painting && !this.engine.playing) {
        const r = canvas.getBoundingClientRect();
        this.onViewportPaint(e.clientX - r.left, e.clientY - r.top, r.width, r.height, true);
      }
    });
    canvas.addEventListener('pointermove', (e) => {
      if (this.onViewportPaint && this.texture.painting && !this.engine.playing && e.buttons > 0) {
        const r = canvas.getBoundingClientRect();
        this.onViewportPaint(e.clientX - r.left, e.clientY - r.top, r.width, r.height, false);
      }
    });

    this.pushUndo();
  }

  // ---------------- sheets ----------------

  openSheet(id: SheetId): void {
    if (this.openSheetId === id) {
      this.closeSheet();
      return;
    }
    this.openSheetId = id;
    this.sheetTitle.textContent = {
      hierarchy: 'Hierarquia da Cena',
      inspector: 'Propriedades',
      scripts: 'Scripts G.oni',
      visual: 'G.oni Visual (nós)',
      modeling: 'Modelagem',
      animation: 'Animação',
      paint: 'Texturização',
      assets: 'Assets & Configurações',
    }[id];
    this.sheet.classList.add('open');
    this.sheetBackdrop.classList.add('open');
    this.refresh();
  }

  closeSheet(): void {
    this.openSheetId = null;
    this.sheet.classList.remove('open');
    this.sheetBackdrop.classList.remove('open');
  }

  refresh(): void {
    if (!this.openSheetId) return;
    this.sheetBody.innerHTML = '';
    switch (this.openSheetId) {
      case 'hierarchy': this.hierarchy.render(this.sheetBody); break;
      case 'inspector': this.inspector.render(this.sheetBody); break;
      case 'scripts': this.scripts.render(this.sheetBody); break;
      case 'visual': this.visual.render(this.sheetBody); break;
      case 'modeling': this.modeling.render(this.sheetBody); break;
      case 'animation': this.animation.render(this.sheetBody); break;
      case 'paint': this.texture.render(this.sheetBody); break;
      case 'assets': void this.assets.render(this.sheetBody); break;
    }
  }

  // ---------------- seleção / objetos ----------------

  selectObject(obj: GOniObject | null): void {
    this.viewport.select(obj);
    if (this.openSheetId === 'inspector' || this.openSheetId === 'hierarchy') this.refresh();
  }

  setGizmoMode(mode: GizmoMode): void {
    this.viewport.setGizmoMode(mode);
    // atualiza botões ativos
    const toolbar = this.root.querySelector('.ed-toolbar');
    const btns = toolbar ? [...toolbar.querySelectorAll('button.icon-btn')] : [];
    const modes: GizmoMode[] = ['select', 'translate', 'rotate', 'scale'];
    btns.forEach((b, i) => {
      if (i < modes.length) b.classList.toggle('active', modes[i] === mode);
    });
  }

  openAddObjectMenu(): void {
    // painel de criação via sheet hierarquia
    this.openSheet('hierarchy');
    setTimeout(() => this.showAddMenu(), 150);
  }

  private showAddMenu(): void {
    const menu = el('div', { class: 'insp-group' }, el('h3', {}, 'Adicionar à cena'));
    const grid = el('div', { class: 'chip-row' });

    for (const prim of Object.keys(PRIMITIVES)) {
      grid.append(el('button', {
        class: 'chip-btn',
        onclick: () => { this.addPrimitive(prim); menu.remove(); },
      }, prim));
    }
    grid.append(el('button', {
      class: 'chip-btn',
      onclick: () => { this.addLight('point'); menu.remove(); },
    }, 'luz pontual'));
    grid.append(el('button', {
      class: 'chip-btn',
      onclick: () => { this.addLight('spot'); menu.remove(); },
    }, 'luz spot'));
    grid.append(el('button', {
      class: 'chip-btn',
      onclick: () => { this.addLight('directional'); menu.remove(); },
    }, 'luz direcional'));
    grid.append(el('button', {
      class: 'chip-btn',
      onclick: () => { this.addCamera(); menu.remove(); },
    }, 'câmera'));
    grid.append(el('button', {
      class: 'chip-btn',
      onclick: () => {
        const obj = new GOniObject();
        obj.name = this.engine.scene.uniqueName('Vazio');
        this.engine.scene.add(obj);
        this.selectObject(obj);
        this.markDirty();
        menu.remove();
      },
    }, 'objeto vazio'));

    // prefabs
    for (const name of this.engine.construt.prefabs.keys()) {
      grid.append(el('button', {
        class: 'chip-btn',
        onclick: () => {
          const obj = this.engine.spawnObject(name, new Vec3(0, 1, 0));
          if (obj) this.selectObject(obj);
          this.markDirty();
          menu.remove();
        },
      }, `prefab: ${name}`));
    }

    menu.append(grid);
    this.sheetBody.prepend(menu);
  }

  private addPrimitive(prim: string): void {
    const obj = new GOniObject();
    obj.name = this.engine.scene.uniqueName(prim.charAt(0).toUpperCase() + prim.slice(1));
    const mesh = new MeshComponent();
    mesh.meshId = `prim:${prim}`;
    this.engine.ensureMesh(mesh.meshId);
    if (prim === 'plane') {
      mesh.material.albedo = new Color(0.32, 0.36, 0.42);
      obj.transform.position.set(0, 0, 0);
    } else {
      obj.transform.position.set(0, 1.2, 0);
    }
    obj.addComponent(mesh);
    this.engine.scene.add(obj);
    this.selectObject(obj);
    this.markDirty();
    toast(`${obj.name} adicionado`);
  }

  private addLight(type: 'point' | 'spot' | 'directional'): void {
    const obj = new GOniObject();
    obj.name = this.engine.scene.uniqueName(type === 'point' ? 'Luz' : type === 'spot' ? 'Spot' : 'Sol');
    obj.transform.position.set(0, 3, 0);
    const light = new LightComponent();
    light.light.type = type;
    if (type === 'point') light.light.intensity = 3;
    if (type === 'spot') light.light.intensity = 6;
    if (type === 'directional') light.light.intensity = 1.5;
    obj.addComponent(light);
    this.engine.scene.add(obj);
    this.selectObject(obj);
    this.markDirty();
    toast(`${obj.name} adicionada`);
  }

  private addCamera(): void {
    const obj = new GOniObject();
    obj.name = this.engine.scene.uniqueName('Câmera');
    obj.transform.position.set(0, 2, 5);
    const cam = new CameraComponent();
    cam.primary = true;
    obj.addComponent(cam);
    this.engine.scene.add(obj);
    this.selectObject(obj);
    this.markDirty();
    toast('Câmera adicionada');
  }

  duplicateSelected(): void {
    const sel = this.viewport.selected;
    if (!sel) return;
    this.pushUndo();
    const copy = GOniObject.deserialize(JSON.parse(JSON.stringify(sel.serialize())));
    copy.name = this.engine.scene.uniqueName(`${sel.name}_cópia`);
    copy.transform.position.x += 1.2;
    this.engine.scene.add(copy, sel.parent);
    this.selectObject(copy);
    this.markDirty();
    toast(`Duplicado: ${copy.name}`);
  }

  deleteSelected(): void {
    const sel = this.viewport.selected;
    if (!sel) return;
    this.pushUndo();
    this.engine.eliminacao.destroy(sel, this.engine.scene, 'editor');
    this.engine.physics.removeBody(sel);
    this.selectObject(null);
    this.markDirty();
    this.refresh();
    toast(`${sel.name} excluído`);
  }

  focusSelected(): void {
    this.viewport.focusSelected();
  }

  async createPrefabFromSelected(): Promise<void> {
    const sel = this.viewport.selected;
    if (!sel) return;
    this.engine.construt.registerFromObject(sel);
    this.markDirty();
    toast(`Prefab "${sel.name}" criado (spawnável via script ou menu Adicionar)`);
    this.refresh();
  }

  autoFitCollider(): void {
    const sel = this.viewport.selected;
    if (!sel) return;
    const mesh = sel.getComponent<MeshComponent>('mesh');
    const colComp = sel.getComponent<ColliderComponent>('collider');
    if (!mesh || !colComp) return;
    const entry = this.engine.renderer.meshRegistry.get(mesh.meshId);
    if (!entry) return;
    const { min, max } = entry.data.bounds();
    const col = colComp.collider;
    col.shape = 'box';
    col.size.set((max[0] - min[0]) / 2 * sel.transform.scale.x, (max[1] - min[1]) / 2 * sel.transform.scale.y, (max[2] - min[2]) / 2 * sel.transform.scale.z);
    col.offset.set(((min[0] + max[0]) / 2) * sel.transform.scale.x, ((min[1] + max[1]) / 2) * sel.transform.scale.y, ((min[2] + max[2]) / 2) * sel.transform.scale.z);
    this.refresh();
    toast('Colisor ajustado à malha');
  }

  async importTextureFor(mesh: MeshComponent): Promise<void> {
    const input = el('input', { type: 'file', accept: 'image/*' }) as HTMLInputElement;
    input.style.display = 'none';
    document.body.append(input);
    input.addEventListener('change', async () => {
      const file = input.files?.[0];
      input.remove();
      if (!file) return;
      const bmp = await createImageBitmap(file);
      const c = document.createElement('canvas');
      c.width = bmp.width; c.height = bmp.height;
      c.getContext('2d')?.drawImage(bmp, 0, 0);
      const id = `tex_${Date.now().toString(36)}`;
      const tex = this.engine.registerTexture(id, c);
      mesh.material.albedoTexture = tex;
      await this.vfs.write(`textures/${id}.json`, JSON.stringify({ dataUrl: c.toDataURL('image/png') }));
      this.markDirty();
      this.refresh();
      toast('Textura importada e aplicada');
    });
    input.click();
  }

  // ---------------- play ----------------

  togglePlay(): void {
    if (this.engine.playing) {
      this.engine.stop();
      this.playOverlay.show(false);
      this.playBtn.innerHTML = '';
      this.playBtn.append(Icons.play());
      this.playBtn.classList.add('primary');
      this.playBtn.title = 'Play';
      this.viewport.gizmo.mode = this.lastGizmoMode;
      this.dirty = true;
    } else {
      void this.save(true);
      this.lastGizmoMode = this.viewport.gizmo.mode;
      // câmera do jogo
      this.engine.activeCamera = new Camera();
      this.engine.play();
      this.playOverlay.show(true);
      this.playBtn.innerHTML = '';
      this.playBtn.append(Icons.stop());
      this.playBtn.classList.remove('primary');
      this.playBtn.title = 'Parar';
      this.closeSheet();
    }
  }

  private lastGizmoMode: GizmoMode = 'select';

  openScriptEditor(name: string): void {
    this.scripts.openScript(name);
  }

  openAnimationEditor(): void {
    this.openSheet('animation');
  }

  // ---------------- undo/redo ----------------

  pushUndo(): void {
    const snap = JSON.stringify(this.engine.scene.serialize());
    if (this.undoStack[this.undoStack.length - 1] === snap) return;
    this.undoStack.push(snap);
    if (this.undoStack.length > 30) this.undoStack.shift();
    this.redoStack.length = 0;
  }

  undo(): void {
    if (this.undoStack.length < 2) return;
    const current = this.undoStack.pop()!;
    this.redoStack.push(current);
    const prev = this.undoStack[this.undoStack.length - 1];
    this.restoreScene(prev);
    this.selectObject(null);
    this.markDirty();
    this.refresh();
    toast('Desfeito');
  }

  redo(): void {
    const snap = this.redoStack.pop();
    if (!snap) return;
    this.undoStack.push(snap);
    this.restoreScene(snap);
    this.selectObject(null);
    this.markDirty();
    this.refresh();
    toast('Refeito');
  }

  private restoreScene(snap: string): void {
    const scene = Scene.deserialize(JSON.parse(snap));
    this.engine.scene = scene;
    this.engine.renderer.settings = scene.environment;
    scene.updateWorldMatrices();
  }

  // ---------------- persistência ----------------

  markDirty(): void {
    this.dirty = true;
  }

  async save(silent = false): Promise<void> {
    if (this.engine.playing) return;
    const now = Date.now();
    if (now - this.lastSave < 2000) return;
    this.lastSave = now;
    const project = this.engine.toProject();
    project.name = this.projectName;
    project.version = '1.0';
    await this.vfs.saveProjectData(project);
    this.dirty = false;
    if (!silent) toast('Projeto salvo ✓');
  }

  async exportProject(): Promise<void> {
    const project = this.engine.toProject();
    project.name = this.projectName;
    await downloadProject(project);
  }

  private async backToProjects(): Promise<void> {
    if (this.dirty || this.engine.playing) {
      const ok = await confirmModal('Sair do projeto', 'Salva e volta para a tela de projetos?', 'Sair');
      if (!ok) return;
    }
    await this.save(true);
    localStorage.removeItem('goni_active_project');
    location.reload();
  }
}
