/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSound.cpp -- the ambient sound of the selected objects in the side panel, after the
// old WorldBuilder's object dialog (MapObjectProps): the sound (the template's default, none, or a
// sound effect), whether it plays from the start, and the customised values (looping, loop count,
// volumes, ranges, priority), which start from the sound's own. Without "customise" those keys go.

#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/WaypointCommands.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MapEditorApp
{

namespace
{

// The keys of the customised values; "customise" off removes them all.
const char *const CUSTOM_KEYS[] = {"objectSoundAmbientCustomized", "objectSoundAmbientLooping", "objectSoundAmbientLoopCount",
	"objectSoundAmbientMinVolume", "objectSoundAmbientVolume", "objectSoundAmbientMinRange", "objectSoundAmbientMaxRange",
	"objectSoundAmbientPriority"};

const MeSoundInfo *findSound(const std::string &name)
{
	const std::vector<MeSoundInfo> &sounds = EngineClient::instance().sounds();
	const auto it = std::lower_bound(sounds.begin(), sounds.end(), name, [](const MeSoundInfo &s, const std::string &n) { return std::strcmp(s.name, n.c_str()) < 0; });
	return it != sounds.end() && name == it->name ? &*it : nullptr;
}

std::string templateSound(const std::string &templateName)
{
	for (const MeTemplateInfo &info : EngineClient::instance().catalog())
	{
		if (templateName == info.name)
			return info.ambientSound;
	}
	return {};
}

} // namespace

bool MainWindow::editSelectedObjects(const std::function<void(MapModel::MapObject &)> &change, const std::string &commandName)
{
	if (!m_state.map || !m_state.map->map().objects)
		return false;
	auto &objects = *m_state.map->map().objects;
	std::vector<MapEditorCore::EditObjects::Edit> edits;
	for (std::int32_t index : m_state.map->selection.objects)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= objects.size() || MapEditorCore::isWaypoint(objects[static_cast<std::size_t>(index)]))
			continue;
		const MapModel::MapObject &o = objects[static_cast<std::size_t>(index)];
		MapModel::MapObject after = o;
		change(after);
		if (after != o)
			edits.push_back({index, o, std::move(after)});
	}
	return runCommand(edits.empty() ? nullptr : std::make_unique<MapEditorCore::EditObjects>(commandName, std::move(edits), true));
}

void MainWindow::addSoundEditors(QVBoxLayout *box, const MapModel::MapObject &shown)
{
	box->addWidget(label(T("SOUND"), "Section"));
	const MapModel::PropertyMap &p = shown.properties;
	const std::optional<std::string> chosen = p.getAscii("objectSoundAmbient");
	const std::string byDefault = templateSound(shown.templateName);
	// The sound that plays: the template's without the key, none for an empty name.
	const std::string effective = chosen ? *chosen : byDefault;
	const MeSoundInfo *info = effective.empty() ? nullptr : findSound(effective);

	auto *sound = new QComboBox;
	sound->setObjectName(QStringLiteral("SoundAmbient"));
	sound->addItem(byDefault.empty() ? T("Default (none)") : T("Default (%1)").arg(utf8(byDefault)), QVariant());
	sound->addItem(T("(none)"), QString());
	for (const MeSoundInfo &s : EngineClient::instance().sounds())
		sound->addItem(QString::fromUtf8(s.name), QString::fromUtf8(s.name));
	if (chosen && !chosen->empty() && sound->findData(utf8(*chosen)) < 0)
		sound->addItem(utf8(*chosen), utf8(*chosen));	// a sound the data does not list (kept as it is)
	sound->setCurrentIndex(!chosen ? 0 : (chosen->empty() ? 1 : sound->findData(utf8(*chosen))));
	sound->setEditable(true);
	sound->setInsertPolicy(QComboBox::NoInsert);
	sound->completer()->setFilterMode(Qt::MatchContains);
	sound->completer()->setCompletionMode(QCompleter::PopupCompletion);
	connect(sound, &QComboBox::activated, this, [this, sound](int index) {
		const QVariant choice = sound->itemData(index);
		editSelectedObjects([&choice](MapModel::MapObject &o) {
			if (!choice.isValid())
				o.properties.erase("objectSoundAmbient");
			else
				o.properties.set("objectSoundAmbient", MapModel::PropertyValue(choice.toString().toUtf8().toStdString()));
			// No sound, nothing to customise.
			if (choice.isValid() && choice.toString().isEmpty())
			{
				for (const char *key : CUSTOM_KEYS)
					o.properties.erase(key);
			}
		}, "Change sound");
	});
	box->addWidget(sound);

	const bool none = effective.empty();
	const bool customized = !none && p.getBool("objectSoundAmbientCustomized").value_or(false);
	// Defaults: the sound's own values; a looping-forever sound plays from the start.
	const bool looping = p.getBool("objectSoundAmbientLooping").value_or(info && info->looping);
	const int loopCount = p.getInt("objectSoundAmbientLoopCount").value_or(info ? info->loopCount : 0);
	auto *enabled = new QCheckBox(T("Plays from the start"));
	enabled->setObjectName(QStringLiteral("SoundEnabled"));
	enabled->setEnabled(!none);
	enabled->setChecked(!none && p.getBool("objectSoundAmbientEnabled").value_or(looping && loopCount == 0));
	connect(enabled, &QCheckBox::clicked, this, [this](bool on) {
		editSelectedObjects([on](MapModel::MapObject &o) { o.properties.set("objectSoundAmbientEnabled", MapModel::PropertyValue(on)); }, "Change sound");
	});
	box->addWidget(enabled);
	auto *customize = new QCheckBox(T("Customise"));
	customize->setObjectName(QStringLiteral("SoundCustomize"));
	customize->setEnabled(!none);
	customize->setChecked(customized);
	connect(customize, &QCheckBox::clicked, this, [this](bool on) {
		editSelectedObjects([on](MapModel::MapObject &o) {
			if (on)
				o.properties.set("objectSoundAmbientCustomized", MapModel::PropertyValue(true));
			else
			{
				for (const char *key : CUSTOM_KEYS)
					o.properties.erase(key);
			}
		}, "Change sound");
	});
	box->addWidget(customize);

	auto *grid = new QGridLayout;
	grid->setHorizontalSpacing(10);
	grid->setVerticalSpacing(6);
	int row = 0;
	auto *loop = new QCheckBox(T("Loops"));
	loop->setObjectName(QStringLiteral("SoundLooping"));
	loop->setChecked(looping);
	connect(loop, &QCheckBox::clicked, this, [this](bool on) {
		editSelectedObjects([on](MapModel::MapObject &o) { o.properties.set("objectSoundAmbientLooping", MapModel::PropertyValue(on)); }, "Change sound");
	});
	grid->addWidget(loop, row++, 0, 1, 2);
	// A spin box that writes its key when editing ends; scale turns the shown value into the stored one.
	const auto number = [&](const QString &name, const char *objectName, const char *key, int minimum, int maximum, int value, bool real, double scale) {
		auto *spin = new QSpinBox;
		spin->setObjectName(QString::fromLatin1(objectName));
		spin->setRange(minimum, maximum);
		spin->setValue(value);
		const std::string k = key;
		connect(spin, &QSpinBox::editingFinished, this, [this, spin, k, real, scale] {
			const int v = spin->value();
			editSelectedObjects([&](MapModel::MapObject &o) {
				if (real)
					o.properties.set(k, MapModel::PropertyValue(static_cast<float>(v * scale)));
				else
					o.properties.set(k, MapModel::PropertyValue(static_cast<std::int32_t>(v)));
			}, "Change sound");
		});
		grid->addWidget(label(name, "Meta"), row, 0);
		grid->addWidget(spin, row++, 1);
		return spin;
	};
	const auto percent = [](std::optional<float> v, float fallback) { return static_cast<int>(std::lround(v.value_or(fallback) * 100.0f)); };
	const auto units = [](std::optional<float> v, float fallback) { return static_cast<int>(std::lround(v.value_or(fallback))); };
	number(T("Loop count (0: forever)"), "SoundLoopCount", "objectSoundAmbientLoopCount", 0, 1000, loopCount, false, 1.0);
	number(T("Volume %"), "SoundVolume", "objectSoundAmbientVolume", 0, 100,
		percent(p.getReal("objectSoundAmbientVolume"), info ? info->volume : 1.0f), true, 0.01);
	number(T("Minimum volume %"), "SoundMinVolume", "objectSoundAmbientMinVolume", 0, 100,
		percent(p.getReal("objectSoundAmbientMinVolume"), info ? info->minVolume : 0.0f), true, 0.01);
	number(T("Full volume within"), "SoundMinRange", "objectSoundAmbientMinRange", 0, 100000,
		units(p.getReal("objectSoundAmbientMinRange"), info ? info->minRange : 0.0f), true, 1.0);
	number(T("Silent beyond"), "SoundMaxRange", "objectSoundAmbientMaxRange", 0, 100000,
		units(p.getReal("objectSoundAmbientMaxRange"), info ? info->maxRange : 0.0f), true, 1.0);
	auto *priority = new QComboBox;
	priority->setObjectName(QStringLiteral("SoundPriority"));
	priority->addItems({T("Lowest"), T("Low"), T("Normal"), T("High"), T("Critical")});
	priority->setCurrentIndex(std::clamp(p.getInt("objectSoundAmbientPriority").value_or(info ? info->priority : 2), 0, 4));
	connect(priority, &QComboBox::activated, this, [this](int i) {
		editSelectedObjects([i](MapModel::MapObject &o) { o.properties.set("objectSoundAmbientPriority", MapModel::PropertyValue(static_cast<std::int32_t>(i))); }, "Change sound");
	});
	grid->addWidget(label(T("Priority"), "Meta"), row, 0);
	grid->addWidget(priority, row++, 1);
	// The customised values only count with "customise" on.
	auto *custom = new QWidget;
	custom->setLayout(grid);
	custom->setEnabled(customized);
	box->addWidget(custom);
}

} // namespace MapEditorApp
