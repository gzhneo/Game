/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/SymmetryDialog.h"

#include "App/Language.h"
#include "App/Widgets.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QRadioButton>
#include <QVBoxLayout>

namespace MapEditorApp
{

SymmetryDialog::SymmetryDialog(MapEditorCore::SymmetryMode mode, QWidget *parent)
	: QDialog(parent), m_modes(new QButtonGroup(this)), m_other(new QCheckBox(T("Take the right or upper part as the source"), this)),
	  m_terrain(new QCheckBox(T("Terrain: heights, textures, cliff flags"), this)),
	  m_objects(new QCheckBox(T("Objects and waypoints (start waypoints get the next free Player_n_Start, other names a suffix)"), this))
{
	setObjectName(QStringLiteral("SymmetryDialog"));
	setWindowTitle(T("Make the map symmetric"));
	auto *col = new QVBoxLayout(this);
	col->addWidget(label(T("The source part is copied onto the rest of the map. The lines in the views show where it is mirrored."), "Hint", this));
	const QString modes[] = {T("Mirror left and right (two players side by side)"), T("Mirror bottom and top (two players one above the other)"),
		T("Turn by 180 degrees (two players in opposite corners)"), T("Four quarters (four players)")};
	for (int i = 0; i < 4; ++i)
	{
		auto *radio = new QRadioButton(modes[i], this);
		m_modes->addButton(radio, i);
		col->addWidget(radio);
	}
	m_modes->button(static_cast<int>(mode))->setChecked(true);
	connect(m_modes, &QButtonGroup::idClicked, this, [this](int id) { emit modeChanged(static_cast<MapEditorCore::SymmetryMode>(id)); });
	col->addWidget(m_other);
	m_terrain->setChecked(true);
	col->addWidget(m_terrain);
	m_objects->setChecked(true);
	col->addWidget(m_objects);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	col->addWidget(buttons);
}

MapEditorCore::SymmetryOptions SymmetryDialog::options() const
{
	MapEditorCore::SymmetryOptions options;
	options.mode = static_cast<MapEditorCore::SymmetryMode>(m_modes->checkedId());
	options.fromOtherSide = m_other->isChecked();
	options.terrain = m_terrain->isChecked();
	options.objects = m_objects->isChecked();
	return options;
}

} // namespace MapEditorApp
