/**
 * G.oni Llumni — Painel de Scripts (G.oni Script)
 * Lista, cria, edita (com numeração de linhas), compila e anexa scripts.
 * Hot-reload: recompila em play mode.
 */

import { el, toast, promptModal, confirmModal } from '@editor/ui/dom';
import { EditorApp } from '@editor/EditorApp';
import { GOniObject, ScriptComponent } from '@core/goni/Objetos';
import { parse } from '@core/script/Parser';

export const SCRIPT_TEMPLATE = `# G.oni Script — linguagem própria da engine
# Docs: docs/GONI-SCRIPT.md

var velocidade = 45.0
const TAG = "exemplo"

signal pulo

func on_ready():
    print("Pronto: " + self.name)

func on_process(delta):
    self.rotate_y(velocidade * delta)

func on_collision_enter(other, info):
    print("Colidiu com " + other.name)
    emit("pulo", other.name)
`;

export class ScriptPanel {
  currentScript: string | null = null;
  private editorArea: HTMLTextAreaElement | null = null;
  private gutter: HTMLElement | null = null;
  private consoleBox: HTMLElement | null = null;

  constructor(private app: EditorApp) {
    // coleta logs da engine para o console do painel
    this.app.engineCallbacks.onLog = (msg) => this.appendLog(msg);
  }

  render(body: HTMLElement): void {
    body.innerHTML = '';
    const sources = [...this.app.engine.scriptSources.keys()].sort();

    // barra de ferramentas
    const tools = el('div', { class: 'code-tools' },
      el('button', { class: 'small primary', onclick: () => void this.newScript() }, '+ Novo'),
      el('button', {
        class: 'small',
        onclick: () => {
          const sel = this.app.viewport.selected;
          if (!sel) { toast('Selecione um objeto para anexar', 'err'); return; }
          if (!this.currentScript) { toast('Abra um script primeiro', 'err'); return; }
          let comp = sel.getComponent<ScriptComponent>('script');
          if (!comp) comp = sel.addComponent(new ScriptComponent());
          comp.scriptName = this.currentScript;
          toast(`"${this.currentScript}" anexado a ${sel.name}`);
          this.app.refresh();
        },
      }, 'Anexar ao objeto'),
    );
    body.append(tools);

    // seletor de scripts
    const sel = el('select', { style: 'margin-bottom:10px' }) as HTMLSelectElement;
    sel.append(el('option', { value: '' }, '— escolher script —'));
    for (const name of sources) sel.append(el('option', { value: name }, name));
    sel.value = this.currentScript ?? '';
    sel.addEventListener('change', () => {
      this.currentScript = sel.value || null;
      this.render(body);
    });
    body.append(sel);

    if (sources.length === 0) {
      body.append(el('p', { class: 'hint' }, 'Nenhum script no projeto. Crie um para começar a programar comportamentos.'));
      return;
    }

    if (!this.currentScript && sources.length) {
      this.currentScript = sources[0];
    }

    if (this.currentScript) {
      // editor
      const wrap = el('div', { class: 'code-editor' });
      const gutter = el('div', { class: 'code-gutter' }, '1');
      const area = el('textarea', {
        class: 'code-area',
        spellcheck: 'false',
        autocapitalize: 'off',
        autocomplete: 'off',
      }) as HTMLTextAreaElement;
      area.value = this.app.engine.scriptSources.get(this.currentScript) ?? '';
      this.editorArea = area;
      this.gutter = gutter;

      const updateGutter = () => {
        const lines = area.value.split('\n').length;
        gutter.textContent = Array.from({ length: lines }, (_, i) => String(i + 1)).join('\n');
        gutter.scrollTop = area.scrollTop;
      };
      area.addEventListener('input', updateGutter);
      area.addEventListener('scroll', () => { gutter.scrollTop = area.scrollTop; });
      updateGutter();

      // Tab insere 4 espaços
      area.addEventListener('keydown', (e) => {
        const ev = e as KeyboardEvent;
        if (ev.key === 'Tab') {
          ev.preventDefault();
          const s = area.selectionStart;
          area.value = area.value.slice(0, s) + '    ' + area.value.slice(area.selectionEnd);
          area.selectionStart = area.selectionEnd = s + 4;
          updateGutter();
        }
      });

      const areaWrap = el('div', { class: 'code-area-wrap' }, gutter, area);
      wrap.append(areaWrap);

      // ações do script
      wrap.append(el('div', { style: 'display:flex;gap:8px;margin-top:8px;flex:none;flex-wrap:wrap' },
        el('button', { class: 'small primary', onclick: () => this.save(true) }, '💾 Salvar'),
        el('button', {
          class: 'small',
          onclick: () => {
            const obj = this.app.viewport.selected;
            if (obj) {
              const comp = obj.getComponent<ScriptComponent>('script');
              if (comp?.scriptName === this.currentScript) {
                this.app.engine.scriptInstancesTouch();
                toast('Hot-reload aplicado');
                return;
              }
            }
            toast('Salve para recompilar. O hot-reload total ocorre ao reiniciar o play.');
          },
        }, 'Hot-reload'),
        el('button', {
          class: 'small danger',
          onclick: () => void this.deleteScript(),
        }, 'Excluir'),
      ));

      body.append(wrap);
    }

    // console
    body.append(el('div', { class: 'section-title' }, 'Console'));
    const consoleBox = el('div', { class: 'console-box' }) as HTMLElement;
    for (const l of this.app.engine.console.slice(-60)) {
      consoleBox.append(logLine(l));
    }
    this.consoleBox = consoleBox;
    body.append(consoleBox);
  }

  private appendLog(msg: string): void {
    if (!this.consoleBox) return;
    this.consoleBox.append(logLine(msg));
    while (this.consoleBox.childElementCount > 120) {
      this.consoleBox.firstChild?.remove();
    }
    this.consoleBox.scrollTop = this.consoleBox.scrollHeight;
  }

  save(showToast: boolean): void {
    if (!this.currentScript || !this.editorArea) return;
    const src = this.editorArea.value;
    try {
      parse(src); // valida sintaxe
    } catch (err) {
      toast((err as Error).message, 'err');
      return;
    }
    this.app.engine.scriptSources.set(this.currentScript, src);
    // invalida cache de AST
    this.app.engine.astCacheTouch();
    if (showToast) toast(`"${this.currentScript}" salvo`);
    this.app.markDirty();
  }

  private async newScript(): Promise<void> {
    const name = await promptModal('Nome do script', 'meu_script');
    if (!name) return;
    if (!/^[a-z0-9_]+$/i.test(name)) {
      toast('Use apenas letras, números e _', 'err');
      return;
    }
    if (this.app.engine.scriptSources.has(name)) {
      toast('Já existe um script com esse nome', 'err');
      return;
    }
    this.app.engine.scriptSources.set(name, SCRIPT_TEMPLATE);
    this.currentScript = name;
    this.app.markDirty();
    this.app.refresh();
  }

  private async deleteScript(): Promise<void> {
    if (!this.currentScript) return;
    const ok = await confirmModal('Excluir script', `Excluir "${this.currentScript}"?`, 'Excluir');
    if (!ok) return;
    this.app.engine.scriptSources.delete(this.currentScript);
    // remove anexações
    for (const obj of this.app.engine.scene.all()) {
      for (const comp of obj.getComponents<ScriptComponent>('script')) {
        if (comp.scriptName === this.currentScript) comp.scriptName = '';
      }
    }
    this.currentScript = null;
    this.app.markDirty();
    this.app.refresh();
  }

  openScript(name: string): void {
    if (this.app.engine.scriptSources.has(name)) {
      this.currentScript = name;
      this.app.openSheet('scripts');
    }
  }
}

function logLine(msg: string): HTMLElement {
  const isErr = msg.startsWith('✖');
  const isOk = msg.startsWith('▶') || msg.startsWith('■');
  const d = el('div', { class: isErr ? 'err' : isOk ? 'ok' : '' }, msg);
  return d;
}
