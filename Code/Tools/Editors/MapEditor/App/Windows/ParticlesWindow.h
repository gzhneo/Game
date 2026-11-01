/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ParticlesWindow.h -- the particle system editor: every system of the game data with a form built from
// its INI block, the INI text, colour and alpha as curves, a preview in the 3D view with the emission
// volume, reset to the game data, an undo list of its own, and saving all systems.

#pragma once

#include "Core/ParticleIni.h"
#include "Engine/EngineApi.h"

#include <QDialog>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <array>
#include <functional>
#include <optional>
#include <vector>

class QTimer;

namespace MapEditorApp
{

class EditorContext;

class ParticlesWindow : public QDialog
{
	Q_OBJECT

public:
	ParticlesWindow(EditorContext &editor, QWidget *parent);

	void refreshParticleList();
	void selectParticle(const QString &name);
	void loadParticle(const QString &name);
	void fillParticleFields();
	void previewSelectedParticle();
	void stopParticlePreview();
	// Puts the selected system (or, with all, every system) back as the game data have it.
	void resetParticles(bool all, bool ask);
	// The emission volume of the previewed system for the 3D overlay (x, y, z per point).
	void appendParticleOutline(std::vector<float> &points, std::vector<MeOverlayShape> &shapes) const;
	// Runs change (apply, reset) on the named systems as one step of the particle window's undo list.
	void particleStep(const QStringList &names, const std::function<bool()> &change);
	void undoParticles();
	void redoParticles();
	// Shows the selected system again after an undo step; empty if the step removed it.
	void showParticleAgain();
	// The fields that differ from the game data, in the "Changes" tab.
	void fillParticleChanges();
	// A texture of the game data chosen in a picker with pictures; empty if cancelled.
	QString pickTexture(const QString &current);

	// For the self test: the system being edited and the state of the preview.
	[[nodiscard]] MapEditorCore::ParticleBlock &block() { return m_particleBlock; }
	[[nodiscard]] QString &blockName() { return m_particleBlockName; }
	[[nodiscard]] bool &outlineShown() { return m_particleOutline; }
	[[nodiscard]] const std::optional<std::array<float, 3>> &previewAt() const { return m_particlePreviewAt; }
	[[nodiscard]] bool previewRunning() const;

signals:
	// The emission volume in the 3D view changed.
	void overlayChanged();
	// The 3D view should draw a new frame (a preview started or stopped).
	void viewChanged();
	void testInGameRequested();

private:
	EditorContext &m_editor;
	QPointer<QTimer> m_particleTimer;		// renders the view while a particle preview runs
	MapEditorCore::ParticleBlock m_particleBlock;	// the system being edited
	QString m_particleBlockName;
	std::optional<std::array<float, 3>> m_particlePreviewAt;	// where the preview runs
	bool m_particleOutline = true;					// emission volume in the 3D view
	struct ParticleStep
	{
		QString name;
		QString before;		// INI block before the step; empty if the system did not exist
		QString after;
	};
	std::vector<std::vector<ParticleStep>> m_particleUndo;
	std::vector<std::vector<ParticleStep>> m_particleRedo;
};

} // namespace MapEditorApp
