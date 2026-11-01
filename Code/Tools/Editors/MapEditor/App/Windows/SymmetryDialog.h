/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SymmetryDialog.h -- the choices for making the map symmetric (Core/Symmetry): the kind of symmetry,
// the source part and what is copied. The chosen kind is reported at once, so the views can show the
// mirror lines while the dialog is open.

#pragma once

#include "Core/Symmetry.h"

#include <QDialog>

class QButtonGroup;
class QCheckBox;

namespace MapEditorApp
{

class SymmetryDialog : public QDialog
{
	Q_OBJECT

public:
	SymmetryDialog(MapEditorCore::SymmetryMode mode, QWidget *parent);

	[[nodiscard]] MapEditorCore::SymmetryOptions options() const;

signals:
	void modeChanged(MapEditorCore::SymmetryMode mode);

private:
	QButtonGroup *m_modes;
	QCheckBox *m_other;
	QCheckBox *m_terrain;
	QCheckBox *m_objects;
};

} // namespace MapEditorApp
