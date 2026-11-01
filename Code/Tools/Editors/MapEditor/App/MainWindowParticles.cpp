/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowParticles.cpp -- the particle system editor (Windows/ParticlesWindow) in the main window: its
// emission volume goes into the overlay of the 3D view, "Test in game" into the session.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Viewport3D.h"
#include "App/Windows/ParticlesWindow.h"

namespace MapEditorApp
{

void MainWindow::showParticles()
{
	if (!EngineClient::instance().isStarted())
	{
		toast(T("The particle editor needs the game data: open a map in the 3D view first."));
		return;
	}
	if (!m_particlesWindow)
	{
		m_particlesWindow = new ParticlesWindow(*this, this);
		connect(m_particlesWindow, &ParticlesWindow::overlayChanged, this, [this] { applyOverlay(); });
		connect(m_particlesWindow, &ParticlesWindow::viewChanged, this, [this] {
			if (m_viewport)
				m_viewport->update();
		});
		connect(m_particlesWindow, &ParticlesWindow::testInGameRequested, this, [this] { testInGame(); });
	}
	m_particlesWindow->refreshParticleList();
	m_particlesWindow->show();
	m_particlesWindow->raise();
}

} // namespace MapEditorApp
