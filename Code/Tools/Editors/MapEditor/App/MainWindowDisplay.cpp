/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowDisplay.cpp -- what the views show ("View -> Show"), as the old WorldBuilder's view menu:
// models, shadows, clouds, macro texture (SelectMacrotexture), soft water, wireframe, three-way
// blends in black, ambient sounds with their ranges, contour lines with step and offset
// (ContourOptions) and the grid of the 2D overview. The choices are kept in the settings.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/GraphicsLevels.h"
#include "Core/Slope.h"

#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

namespace MapEditorApp
{

MeDisplayOptions MainWindow::engineDisplay() const
{
	const DisplayOptions &d = m_state.display;
	MeDisplayOptions o{};
	o.models = d.models ? 1 : 0;
	o.shadows = d.shadows ? 1 : 0;
	o.clouds = d.clouds ? 1 : 0;
	o.macroTexture = d.macroTexture ? 1 : 0;
	// Cut to fit; the rest of the field stays zero.
	const QByteArray name = d.macroTextureName.toUtf8();
	const std::size_t length = std::min(static_cast<std::size_t>(name.size()), sizeof(o.macroTextureName) - 1);
	std::memcpy(o.macroTextureName, name.constData(), length);
	o.softWater = d.softWater ? 1 : 0;
	o.wireframe = d.wireframe ? 1 : 0;
	o.extraBlends = d.extraBlends ? 1 : 0;
	o.letterbox = d.letterbox ? 1 : 0;
	o.garrisoned = d.garrisoned ? 1 : 0;
	// The detail level's look; effects switched off are left out of its list.
	for (const MapEditorCore::GraphicsLevel &level : graphicsLevels())
	{
		if (QString::fromStdString(level.name) != d.graphicsLevel)
			continue;
		std::vector<std::string> off;
		for (const QString &effect : d.effectsOff)
			off.push_back(effect.toStdString());
		const std::string effects = MapEditorCore::effectsWithout(level.effects, off);
		std::memcpy(o.effects, effects.data(), std::min(effects.size(), sizeof(o.effects) - 1));
		o.waterQuality = d.effectsOff.contains(QStringLiteral("water")) ? 0 : level.waterQuality;
		o.terrainAnisotropy = level.terrainAnisotropy;
	}
	return o;
}

const std::vector<MapEditorCore::GraphicsLevel> &MainWindow::graphicsLevels()
{
	static const std::vector<MapEditorCore::GraphicsLevel> levels = [] {
		QFile file(QStringLiteral(":/mapeditor/GameLODOverride.ini"));
		return file.open(QIODevice::ReadOnly) ? MapEditorCore::parseGraphicsLevels(file.readAll().toStdString()) : std::vector<MapEditorCore::GraphicsLevel>();
	}();
	return levels;
}

namespace
{

QString levelTitle(const QString &name)
{
	if (name == QLatin1String("Low"))
		return T("Low");
	if (name == QLatin1String("Medium"))
		return T("Medium");
	if (name == QLatin1String("High"))
		return T("High");
	if (name == QLatin1String("VeryHigh"))
		return T("Very high");
	return name;
}

QString effectTitle(const QString &name)
{
	if (name == QLatin1String("fxaa"))
		return T("Antialiasing (FXAA)");
	if (name == QLatin1String("sharpen") || name == QLatin1String("cas"))
		return T("Sharpening");
	if (name == QLatin1String("softparticles") || name == QLatin1String("soft"))
		return T("Soft particles");
	if (name == QLatin1String("ssao"))
		return T("Ambient occlusion (SSAO)");
	if (name == QLatin1String("colorgrade") || name == QLatin1String("grade"))
		return T("Colour grading");
	if (name == QLatin1String("shadowmap") || name == QLatin1String("schatten"))
		return T("Sun shadows (shadow map)");
	if (name == QLatin1String("terraindetail") || name == QLatin1String("detailboden"))
		return T("Ground detail");
	if (name == QLatin1String("terrainrock") || name == QLatin1String("berge"))
		return T("Rock on steep slopes");
	if (name == QLatin1String("particlelight") || name == QLatin1String("partikellicht"))
		return T("Particle lighting");
	if (name == QLatin1String("particleground") || name == QLatin1String("partikelboden"))
		return T("Particles above ground");
	if (name == QLatin1String("water"))
		return T("Realistic water");
	return name;
}

} // namespace

void MainWindow::fillGraphicsMenu(QMenu *menu)
{
	menu->clear();
	auto *levels = new QActionGroup(menu);
	const auto level = [this, menu, levels](const QString &text, const QString &name) {
		QAction *action = menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(m_state.display.graphicsLevel == name);
		levels->addAction(action);
		connect(action, &QAction::triggered, this, [this, name] { setGraphicsLevel(name); });
	};
	level(T("Classic (as the old editor)"), QString());
	for (const MapEditorCore::GraphicsLevel &l : graphicsLevels())
		level(T("As in the game: %1").arg(levelTitle(QString::fromStdString(l.name))), QString::fromStdString(l.name));
	// The effects of the chosen level, each to switch off on its own.
	for (const MapEditorCore::GraphicsLevel &l : graphicsLevels())
	{
		if (QString::fromStdString(l.name) != m_state.display.graphicsLevel)
			continue;
		menu->addSection(T("Effects of this level"));
		QStringList names;
		for (const std::string &name : MapEditorCore::effectNames(l.effects))
			names << QString::fromStdString(name);
		if (l.waterQuality > 0)
			names << QStringLiteral("water");
		for (const QString &name : names)
		{
			QAction *action = menu->addAction(effectTitle(name));
			action->setCheckable(true);
			action->setChecked(!m_state.display.effectsOff.contains(name));
			connect(action, &QAction::toggled, this, [this, name](bool on) { setEffectOff(name, !on); });
		}
	}
}

void MainWindow::setGraphicsLevel(const QString &level)
{
	if (m_state.display.graphicsLevel == level)
		return;
	m_state.display.graphicsLevel = level;
	m_state.save();
	applyDisplay();
}

void MainWindow::setEffectOff(const QString &effect, bool off)
{
	if (m_state.display.effectsOff.contains(effect) == off)
		return;
	if (off)
		m_state.display.effectsOff << effect;
	else
		m_state.display.effectsOff.removeAll(effect);
	m_state.save();
	applyDisplay();
}

void MainWindow::applyDisplay()
{
	EngineClient &engine = EngineClient::instance();
	// Models on or off: the view builds its objects again from the model.
	if (engine.isStarted() && engine.setDisplay(engineDisplay()) && m_state.map && m_viewport && m_viewport->hasDevice()
		&& m_viewMapSerial == m_mapSerial)
	{
		const MapModel::WriteResult bytes = engineMapBytes();
		if (!bytes.ok() || !engine.reloadObjects(bytes.bytes))
			m_viewMapSerial = -1;
	}
	if (m_overview)
	{
		m_overview->setContours(m_state.display.contours ? m_state.display.contourStep : 0, m_state.display.contourOffset);
		m_overview->setShowGrid(m_state.display.grid);
		m_overview->setShowLabels(m_state.display.labels);
		m_overview->setShowObjects(m_state.display.objectIcons);
	}
	m_overlay.sounds = m_state.display.sounds;
	m_overlay.boundingBoxes = m_state.display.boundingBoxes;
	m_overlay.sightRanges = m_state.display.sightRanges;
	m_overlay.weaponRanges = m_state.display.weaponRanges;
	m_overlay.testArt = m_state.display.testArt;
	updateImpassableView();
	if (m_viewport)
		applyOverlay();
}

void MainWindow::setDisplay(bool DisplayOptions::*flag, bool on)
{
	if (m_state.display.*flag == on)
		return;
	m_state.display.*flag = on;
	m_state.save();
	applyDisplay();
}

const MeTemplateInfo *MainWindow::templateInfo(const std::string &templateName)
{
	EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		return nullptr;
	// Index by name, built once per catalog (the catalog lives until the engine stops).
	static std::map<std::string, std::size_t> byName;
	static const MeTemplateInfo *indexed = nullptr;
	const std::vector<MeTemplateInfo> &catalog = engine.catalog();
	if (catalog.empty())
		return nullptr;
	if (indexed != catalog.data() || byName.size() > catalog.size())
	{
		byName.clear();
		for (std::size_t i = 0; i < catalog.size(); ++i)
			byName.emplace(catalog[i].name, i);
		indexed = catalog.data();
	}
	const auto it = byName.find(templateName);
	return it != byName.end() ? &catalog[it->second] : nullptr;
}

float MainWindow::snapCoord(float v) const
{
		return m_state.display.snapToGrid ? CELL * std::floor(v / CELL + 0.5f) : v;
}

void MainWindow::reloadTextures()
{
	EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		return;
	engine.reloadTextures();
	// The terrain builds its textures when the map loads.
	m_viewMapSerial = -1;
	if (m_show3D)
		rebuild();
	toast(T("Textures reloaded"));
}

bool MainWindow::objectSoundRange(const MapModel::MapObject &object, float &minRange, float &maxRange)
{
	EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		return false;
	const MapModel::PropertyMap &p = object.properties;
	// The sound that plays: the template's without the key, none for an empty name.
	std::string sound;
	if (const std::optional<std::string> chosen = p.getAscii("objectSoundAmbient"))
		sound = *chosen;
	else if (const MeTemplateInfo *info = templateInfo(object.templateName))
		sound = info->ambientSound;
	if (sound.empty())
		return false;
	if (p.getBool("objectSoundAmbientCustomized").value_or(false))
	{
		minRange = p.getReal("objectSoundAmbientMinRange").value_or(0.0f);
		maxRange = p.getReal("objectSoundAmbientMaxRange").value_or(0.0f);
		return true;
	}
	minRange = 0.0f;
	maxRange = 0.0f;
	for (const MeSoundInfo &s : engine.sounds())
	{
		if (sound == s.name)
		{
			minRange = s.minRange;
			maxRange = s.maxRange;
			break;
		}
	}
	return true;
}

void MainWindow::chooseMacroTexture()
{
	QStringList names;
	names << T("Game default");
	names << EngineClient::instance().macroTextures();
	const int current = m_state.display.macroTextureName.isEmpty() ? 0 : static_cast<int>(names.indexOf(m_state.display.macroTextureName));
	bool ok = false;
	const QString choice = QInputDialog::getItem(this, T("Macro texture"), T("Noise texture laid over the terrain against visible tiling:"),
		names, std::max(0, current), false, &ok);
	if (!ok)
		return;
	m_state.display.macroTextureName = choice == names.front() ? QString() : choice;
	m_state.display.macroTexture = true;
	m_state.save();
	applyDisplay();
}

void MainWindow::contourOptions()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("ContourOptions"));
	dialog.setWindowTitle(T("Contour lines"));
	auto *form = new QFormLayout(&dialog);
	auto *step = new QSpinBox(&dialog);
	step->setRange(1, 128);
	step->setValue(m_state.display.contourStep);
	auto *offset = new QSpinBox(&dialog);
	offset->setRange(0, 127);
	offset->setValue(m_state.display.contourOffset);
	form->addRow(T("Every (height steps)"), step);
	form->addRow(T("Offset"), offset);
	auto *hint = label(T("One height step is 0.625 world units; the lines show in the 2D overview."), "Hint", &dialog);
	hint->setWordWrap(true);
	form->addRow(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	m_state.display.contourStep = step->value();
	m_state.display.contourOffset = offset->value();
	m_state.display.contours = true;
	m_state.save();
	applyDisplay();
}

void MainWindow::slopeOptions()
{
	QDialog dialog(this);
	dialog.setObjectName(QStringLiteral("SlopeOptions"));
	dialog.setWindowTitle(T("Slopes"));
	auto *form = new QFormLayout(&dialog);
	auto *limit = new QSpinBox(&dialog);
	limit->setRange(1, 89);
	limit->setSuffix(QStringLiteral("\u00b0"));
	limit->setValue(m_state.display.slopeLimit);
	form->addRow(T("Mark steeper than"), limit);
	auto *hint = label(T("The 2D overview colours every cell by its slope, green on flat ground to red at the limit; the 3D view tints the cells from half the limit on, yellow to red, and the steeper ones dark red. Slopes are measured as the game finds visible cliffs. 45\u00b0 is the game's default."), "Hint", &dialog);
	hint->setWordWrap(true);
	form->addRow(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	m_state.display.slopeLimit = limit->value();
	m_state.display.slopes = true;
	m_state.save();
	applyDisplay();
}

} // namespace MapEditorApp
