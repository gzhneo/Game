/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SoundTests.cpp -- the self test of MainWindowSound.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/WaypointCommands.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MapEditorApp
{

bool MainWindow::selfTestSound(int object, QStringList &report)
{
	TestSteps step(report, "sound");
	MapEditorCore::History &history = m_state.map->history;
	auto &objects = *m_state.map->map().objects;
	const MapModel::MapObject before = objects[static_cast<std::size_t>(object)];
	const auto field = [this](const char *name) -> QWidget * {
		QApplication::processEvents();
		return m_selectionBox ? m_selectionBox->findChild<QWidget *>(QString::fromLatin1(name)) : nullptr;
	};
	m_state.map->selection.set(object);
	selectionChanged();
	auto *sound = qobject_cast<QComboBox *>(field("SoundAmbient"));
	step(sound && sound->count() >= 2, QStringLiteral("the panel lists %1 sounds, %2").arg(sound ? sound->count() - 2 : 0).arg(sound ? sound->itemText(0) : QString()));
	if (!sound || sound->count() < 3)
	{
		step(false, QStringLiteral("no sound effects in the data"));
		return false;
	}
	const QString name = sound->itemData(2).toString();
	sound->setCurrentIndex(2);
	emit sound->activated(2);
	const MapModel::MapObject &o = objects[static_cast<std::size_t>(object)];
	step(o.properties.getAscii("objectSoundAmbient") == name.toStdString() && history.undoName() == "Change sound", QStringLiteral("choosing %1 sets it").arg(name));
	if (auto *customize = qobject_cast<QCheckBox *>(field("SoundCustomize")))
		customize->click();
	auto *volume = qobject_cast<QSpinBox *>(field("SoundVolume"));
	const bool customizable = volume && volume->isEnabledTo(m_selectionBox);
	if (volume)
	{
		volume->setValue(42);
		emit volume->editingFinished();
	}
	step(o.properties.getBool("objectSoundAmbientCustomized") == true && customizable
			&& std::abs(o.properties.getReal("objectSoundAmbientVolume").value_or(0.0f) - 0.42f) < 0.001f,
		QStringLiteral("customise, then a volume of 42 %"));
	if (auto *customize = qobject_cast<QCheckBox *>(field("SoundCustomize")))
		customize->click();
	step(!o.properties.find("objectSoundAmbientCustomized") && !o.properties.find("objectSoundAmbientVolume"),
		QStringLiteral("customise off removes the customised values"));
	if ((sound = qobject_cast<QComboBox *>(field("SoundAmbient"))) != nullptr)
	{
		sound->setCurrentIndex(1);
		emit sound->activated(1);
	}
	auto *customize = qobject_cast<QCheckBox *>(field("SoundCustomize"));
	step(o.properties.getAscii("objectSoundAmbient") == std::string() && customize && !customize->isEnabled(), QStringLiteral("(none) leaves nothing to customise"));
	while (history.isModified() && history.canUndo())
		undo();
	step(objects[static_cast<std::size_t>(object)] == before && !history.isModified(), QStringLiteral("undo all"));
	return step.ok();
}

} // namespace MapEditorApp
