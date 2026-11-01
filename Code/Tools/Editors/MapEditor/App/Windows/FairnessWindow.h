/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FairnessWindow.h -- the multiplayer fairness report (Core/Fairness): ground path lengths from every
// start to the supplies, the tech buildings and the other starts, the starts that are worse off marked,
// and on request the territory each start reaches first as colours for the views.

#pragma once

#include "Core/Fairness.h"
#include "Model/Map.h"

#include <QDialog>

#include <cstdint>
#include <vector>

class QCheckBox;
class QLabel;
class QTreeWidget;

namespace MapEditorApp
{

class FairnessWindow : public QDialog
{
	Q_OBJECT

public:
	explicit FairnessWindow(QWidget *parent);

	// The map measured; measure() measures it again (the window does so when it is shown).
	void setMap(const MapModel::Map *map);
	void measure();
	[[nodiscard]] const MapEditorCore::FairnessReport &report() const { return m_report; }
	void setTerritoriesShown(bool shown);
	[[nodiscard]] QTreeWidget *startsView() const { return m_starts; }
	[[nodiscard]] QTreeWidget *resourcesView() const { return m_resources; }
	[[nodiscard]] QString summary() const;

	// One 0xAARRGGBB per height map cell: each start's colour where it arrives first, white where two
	// arrive together.
	[[nodiscard]] static std::vector<std::uint32_t> territoryTint(const MapEditorCore::FairnessReport &report);

signals:
	void centerOn(float x, float y);
	// The territory colours to show over the views; empty when they are switched off.
	void territoriesChanged(const std::vector<std::uint32_t> &tint);

private:
	void emitTerritories();

	const MapModel::Map *m_map = nullptr;
	MapEditorCore::FairnessReport m_report;
	QLabel *m_summary;
	QTreeWidget *m_starts;
	QTreeWidget *m_resources;
	QCheckBox *m_territories;
};

} // namespace MapEditorApp
