/**
 * G.oni Llumni — Ponto de entrada do editor
 * Roteia entre a tela de projetos e o editor do projeto ativo.
 */

import './ui/style.css';
import { ProjectsScreen } from '@projects/ProjectsScreen';
import { EditorApp } from '@editor/EditorApp';

const ACTIVE_KEY = 'goni_active_project';

function boot(): void {
  const container = document.getElementById('app')!;
  const activeId = localStorage.getItem(ACTIVE_KEY);

  if (activeId) {
    // abre direto o editor do projeto ativo
    container.innerHTML = '';
    const app = new EditorApp(container, activeId);
    void app;
    return;
  }

  const screen = new ProjectsScreen(container, (projectId) => {
    localStorage.setItem(ACTIVE_KEY, projectId);
    location.reload();
  });
  void screen;
}

boot();
