/**
 * G.oni Llumni — Tela de Projetos
 * Lista de projetos recentes (IndexedDB), novo projeto com template,
 * importar/exportar .g.oni, renomear e excluir.
 */

import { el, toast, Icons, confirmModal, promptModal } from '@editor/ui/dom';
import { VirtualFS, VFile } from '@projects/storage/vfs';
import { db, ProjectMeta } from '@projects/storage/db';
import { GOniFormat, GOniProject } from '@core/goni/Format';

export class ProjectsScreen {
  private root: HTMLElement;

  constructor(
    private container: HTMLElement,
    private onOpenProject: (projectId: string) => void
  ) {
    this.root = el('div', { class: 'projects-screen' });
    container.append(this.root);
    void this.render();
  }

  private async render(): Promise<void> {
    this.root.innerHTML = '';
    const projects = await VirtualFS.listProjects();

    // ---- cabeçalho ----
    const header = el('div', { class: 'projects-header' },
      el('div', { class: 'projects-logo' }, Icons.cube()),
      el('div', { class: 'projects-title' },
        el('h1', {}, 'G.oni Llumni'),
        el('small', {}, `Engine 3D mobile-first · ${projects.length} projeto${projects.length === 1 ? '' : 's'}`)
      ),
      el('button', {
        class: 'icon-btn ghost',
        title: 'Importar .g.oni',
        onclick: () => void this.importProject(),
      }, Icons.import())
    );

    // ---- lista ----
    const list = el('div', { class: 'projects-list' });
    if (projects.length === 0) {
      list.append(el('div', { class: 'empty-state' },
        el('div', { class: 'big' }, '◈'),
        el('h2', { style: 'margin:0 0 6px;color:var(--text-2)' }, 'Nenhum projeto ainda'),
        el('p', { class: 'hint' }, 'Crie seu primeiro projeto 3D — um template com chão, luz, física e câmera será gerado automaticamente.')
      ));
    }

    for (const p of projects) {
      const card = el('div', { class: 'project-card', onclick: () => this.onOpenProject(p.id) },
        el('h3', {}, p.name),
        el('div', { class: 'meta' }, formatarData(p.updatedAt)),
        el('div', { class: 'actions' },
          el('button', {
            class: 'small ghost',
            onclick: (e) => { e.stopPropagation(); void this.rename(p); },
          }, 'Renomear'),
          el('button', {
            class: 'small ghost danger',
            onclick: (e) => { e.stopPropagation(); void this.remove(p); },
          }, 'Excluir'),
          el('button', {
            class: 'small ghost',
            onclick: (e) => { e.stopPropagation(); void this.exportProject(p); },
          }, 'Exportar')
        )
      );
      list.append(card);
    }

    // ---- rodapé ----
    const footer = el('div', { class: 'projects-footer' },
      el('button', {
        class: 'primary',
        onclick: () => void this.createNew(),
      }, '+ Novo Projeto')
    );

    this.root.append(header, list, footer);
  }

  private async createNew(): Promise<void> {
    const name = await promptModal('Nome do novo projeto', 'Meu Jogo 3D');
    if (!name) return;
    try {
      const meta = await VirtualFS.createProject(name, true);
      toast(`Projeto "${name}" criado`);
      this.onOpenProject(meta.id);
    } catch (err) {
      toast(`Erro ao criar: ${(err as Error).message}`, 'err');
    }
  }

  private async importProject(): Promise<void> {
    const input = el('input', { type: 'file', accept: '.oni,.g.oni,.json,.gz' }) as HTMLInputElement;
    input.style.display = 'none';
    document.body.append(input);
    input.addEventListener('change', async () => {
      const file = input.files?.[0];
      input.remove();
      if (!file) return;
      try {
        const project = await GOniFormat.importFile(file);
        const meta = await VirtualFS.importProject(project);
        toast(`Projeto "${project.name}" importado`);
        this.onOpenProject(meta.id);
      } catch (err) {
        toast(`Falha na importação: ${(err as Error).message}`, 'err');
      }
    });
    input.click();
  }

  private async exportProject(p: ProjectMeta): Promise<void> {
    const vfs = new VirtualFS(p.id);
    const data = await vfs.loadProjectData();
    if (!data) {
      toast('Projeto vazio ou corrompido', 'err');
      return;
    }
    await downloadProject(data);
  }

  private async rename(p: ProjectMeta): Promise<void> {
    const name = await promptModal('Renomear projeto', '', p.name);
    if (!name || name === p.name) return;
    await VirtualFS.renameProject(p.id, name);
    void this.render();
  }

  private async remove(p: ProjectMeta): Promise<void> {
    const ok = await confirmModal('Excluir projeto', `Excluir "${p.name}" permanentemente? Esta ação não pode ser desfeita.`, 'Excluir');
    if (!ok) return;
    await VirtualFS.deleteProject(p.id);
    toast('Projeto excluído');
    void this.render();
  }

  dispose(): void {
    this.root.remove();
  }
}

function formatarData(ts: number): string {
  const d = new Date(ts);
  const dias = Math.floor((Date.now() - ts) / 86400000);
  if (dias === 0) return `hoje às ${d.toLocaleTimeString('pt-BR', { hour: '2-digit', minute: '2-digit' })}`;
  if (dias === 1) return 'ontem';
  if (dias < 30) return `${dias} dias atrás`;
  return d.toLocaleDateString('pt-BR');
}

/** Gera e baixa o arquivo .g.oni (gzip quando suportado). */
export async function downloadProject(project: GOniProject): Promise<void> {
  const blob = await GOniFormat.exportGzip(project);
  const url = URL.createObjectURL(blob);
  const a = el('a', { href: url, download: GOniFormat.fileName(project.name) }) as HTMLAnchorElement;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 4000);
  toast(`Exportado ${GOniFormat.fileName(project.name)}`);
}

export { VirtualFS, db, GOniFormat };
export type { ProjectMeta, VFile };
