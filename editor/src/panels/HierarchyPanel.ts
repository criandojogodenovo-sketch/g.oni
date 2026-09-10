/**
 * G.oni Llumni — Painel de Hierarquia
 * Árvore de objetos com seleção, visibilidade e tags de componentes.
 */

import { el, Icons } from '@editor/ui/dom';
import { GOniObject } from '@core/goni/Objetos';
import { EditorApp } from '@editor/EditorApp';

export class HierarchyPanel {
  constructor(private app: EditorApp) {}

  render(body: HTMLElement): void {
    body.innerHTML = '';
    const scene = this.app.engine.scene;

    const addBtn = el('button', {
      class: 'primary small',
      style: 'width:100%;margin-bottom:10px',
      onclick: () => this.app.openAddObjectMenu(),
    }, '+ Adicionar objeto');
    body.append(addBtn);

    if (scene.roots.length === 0) {
      body.append(el('p', { class: 'hint' }, 'Cena vazia. Adicione primitivas, luzes ou objetos vazios.'));
      return;
    }

    const tree = el('ul', { class: 'tree' });
    const build = (obj: GOniObject, parentUl: HTMLElement, depth: number) => {
      if (depth > 32) return;
      const li = el('li');
      const hasChildren = obj.children.length > 0;
      let expanded = true;

      const tags: string[] = [];
      if (obj.getComponent('mesh')) tags.push('malha');
      if (obj.getComponent('light')) tags.push('luz');
      if (obj.getComponent('camera')) tags.push('câm');
      if (obj.getComponent('collider')) tags.push('col');
      if (obj.getComponent('rigidbody')) tags.push('fís');
      if (obj.getComponent('script')) tags.push('scr');
      if (obj.getComponent('animation')) tags.push('ani');

      const row = el('div', {
        class: `tree-row${this.app.viewport.selected === obj ? ' selected' : ''}`,
        onclick: () => {
          this.app.selectObject(obj);
          this.render(body);
        },
      },
        el('span', {
          class: 'twist',
          style: hasChildren ? 'cursor:pointer' : '',
          onclick: (e) => {
            e.stopPropagation();
            const childUl = li.querySelector('.tree-children') as HTMLElement | null;
            if (childUl) childUl.style.display = childUl.style.display === 'none' ? '' : 'none';
          },
        }, hasChildren ? '▾' : '·'),
        el('span', { class: 'name' }, obj.name),
        ...tags.map((t) => el('span', { class: 'tag' }, t)),
        el('span', {
          class: 'twist',
          style: 'cursor:pointer',
          onclick: (e) => {
            e.stopPropagation();
            obj.visible = !obj.visible;
            obj.markDirty();
            this.render(body);
          },
        }, obj.visible ? '' : '')
      );

      // ícone de visibilidade
      const eyeWrap = row.children[row.children.length - 1] as HTMLElement;
      eyeWrap.innerHTML = '';
      eyeWrap.append(obj.visible ? Icons.eye() : Icons.eyeOff());
      eyeWrap.style.color = obj.visible ? 'var(--text-3)' : 'var(--red)';
      eyeWrap.style.display = 'grid';
      eyeWrap.style.placeItems = 'center';
      eyeWrap.querySelector('svg')?.setAttribute('width', '16');
      eyeWrap.querySelector('svg')?.setAttribute('height', '16');

      li.append(row);
      if (obj.children.length) {
        const childUl = el('ul', { class: 'tree-children' });
        for (const c of obj.children) build(c, childUl, depth + 1);
        li.append(childUl);
      }
      void expanded;
      parentUl.append(li);
    };

    for (const root of scene.roots) build(root, tree, 0);
    body.append(tree);
  }
}
