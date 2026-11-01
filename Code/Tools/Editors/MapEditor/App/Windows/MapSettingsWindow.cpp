/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/MapSettingsWindow.h"

#include "App/EditorContext.h"
#include "App/Common.h"
#include "App/EngineClient.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/SunDial.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/MapSettingsCommands.h"
#include "Core/WaterLook.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>

namespace MapEditorApp
{

namespace
{

// The lights of the map's own time of day, for terrain or objects.
std::array<MapModel::Light, 3> *lightsOf(MapModel::Map &map, bool objects)
{
	if (!map.lighting || map.lighting->timeOfDay < 1 || map.lighting->timeOfDay > 4)
		return nullptr;
	MapModel::TimeOfDayLighting &t = map.lighting->times[static_cast<std::size_t>(map.lighting->timeOfDay - 1)];
	return objects ? &t.objects : &t.terrain;
}

} // namespace

MapSettingsWindow::MapSettingsWindow(EditorContext &editor, MapSettingsContext &settings, QWidget *parent)
	: QDialog(parent), m_editor(editor), m_settings(settings)
{
	setObjectName(QStringLiteral("MapSettingsWindow"));
	setWindowTitle(T("Map settings"));
	resize(460, 420);
}

void MapSettingsWindow::refresh()
{
	if (!m_editor.openMap())
		return;
	QDialog *window = this;
	const int tab = window->findChild<QTabWidget *>() ? window->findChild<QTabWidget *>()->currentIndex() : 0;
	for (QWidget *child : window->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
	{
		child->hide();
		child->deleteLater();
	}
	delete window->layout();
	auto *col = new QVBoxLayout(window);
	auto *tabs = new QTabWidget(window);
	col->addWidget(tabs);
	MapModel::Map &map = m_editor.openMap()->map();

	// General: name, time of day, weather, compression.
	auto *general = new QWidget;
	auto *form = new QFormLayout(general);
	auto *name = new QLineEdit(QString::fromStdString(map.worldInfo ? map.worldInfo->getAscii("mapName").value_or("") : std::string()));
	name->setObjectName(QStringLiteral("SettingsName"));
	connect(name, &QLineEdit::editingFinished, this, [this, name] {
		const std::string text = name->text().toStdString();
		m_settings.editSettings("Map name", [text](MapModel::Map &m) {
			if (!m.worldInfo)
				m.worldInfo = MapModel::PropertyMap{};
			m.worldInfo->set("mapName", MapModel::PropertyValue(text));
		});
	});
	form->addRow(T("Name"), name);
	auto *time = new QComboBox;
	time->setObjectName(QStringLiteral("SettingsTime"));
	time->addItems({T("Morning"), T("Afternoon"), T("Evening"), T("Night")});
	time->setCurrentIndex(map.lighting ? std::clamp(map.lighting->timeOfDay - 1, 0, 3) : 1);
	time->setEnabled(map.lighting.has_value());
	connect(time, &QComboBox::currentIndexChanged, this, [this](int i) {
		m_settings.editLighting("Time of day", [i](MapModel::Map &m) {
			if (m.lighting)
				m.lighting->timeOfDay = i + 1;
		});
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	form->addRow(T("Time of day"), time);
	auto *weather = new QComboBox;
	weather->setObjectName(QStringLiteral("SettingsWeather"));
	weather->addItems({T("Normal"), T("Snowy")});
	weather->setCurrentIndex(std::clamp(map.worldInfo ? map.worldInfo->getInt("weather").value_or(0) : 0, 0, 1));
	connect(weather, &QComboBox::currentIndexChanged, this, [this](int i) {
		m_settings.editSettings("Weather", [i](MapModel::Map &m) {
			if (!m.worldInfo)
				m.worldInfo = MapModel::PropertyMap{};
			m.worldInfo->set("weather", MapModel::PropertyValue(static_cast<std::int32_t>(i)));
		});
	});
	form->addRow(T("Weather"), weather);
	auto *compression = new QComboBox;
	compression->setObjectName(QStringLiteral("SettingsCompression"));
	compression->addItem(T("None"), 0);
	compression->addItem(QStringLiteral("RefPack"), 1);
	const std::int32_t packed = map.worldInfo ? map.worldInfo->getInt("compression").value_or(1) : 1;
	if (compression->findData(packed) < 0)
		compression->addItem(T("Other (%1)").arg(packed), packed);
	compression->setCurrentIndex(compression->findData(packed));
	connect(compression, &QComboBox::currentIndexChanged, this, [this, compression](int) {
		const std::int32_t value = compression->currentData().toInt();
		m_settings.editSettings("Compression", [value](MapModel::Map &m) {
			if (!m.worldInfo)
				m.worldInfo = MapModel::PropertyMap{};
			m.worldInfo->set("compression", MapModel::PropertyValue(value));
		});
	});
	form->addRow(T("Compression"), compression);
	tabs->addTab(general, T("General"));

	// Lighting of the map's time of day: the sun and two accent lights, for terrain, objects or both.
	auto *lighting = new QWidget;
	auto *lcol = new QVBoxLayout(lighting);
	auto *target = new QComboBox;
	target->setObjectName(QStringLiteral("SettingsLightTarget"));
	target->addItems({T("Terrain"), T("Objects"), T("Terrain and objects")});
	target->setCurrentIndex(m_lightTarget);
	connect(target, &QComboBox::currentIndexChanged, this, [this](int i) {
		m_lightTarget = i;
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	lcol->addWidget(target);
	std::array<MapModel::Light, 3> *shown = lightsOf(map, m_lightTarget != 0);
	if (!shown)
		lcol->addWidget(label(T("The map has no lighting."), "Hint"));
	auto *grid = new QGridLayout;
	grid->addWidget(label(T("Light"), "Meta"), 0, 0);
	const QStringList columns = {T("Diffuse R"), T("G"), T("B"), T("Ambient R"), T("G"), T("B"), T("Azimuth"), T("Elevation")};
	for (int c = 0; c < columns.size(); ++c)
		grid->addWidget(label(columns[c], "Meta"), 0, c + 1);
	const QStringList lightNames = {T("Sun"), T("Accent 1"), T("Accent 2")};
	// Changes a value of one light in the lights the target names.
	const auto changeLight = [this](int light, const std::function<void(MapModel::Light &)> &edit) {
		const int t = m_lightTarget;
		m_settings.editLighting("Change light", [light, edit, t](MapModel::Map &m) {
			for (bool objects : {false, true})
			{
				if ((objects && t == 0) || (!objects && t == 1))
					continue;
				if (std::array<MapModel::Light, 3> *lights = lightsOf(m, objects))
					edit((*lights)[static_cast<std::size_t>(light)]);
			}
		});
	};
	for (int l = 0; l < 3 && shown; ++l)
	{
		const MapModel::Light &light = (*shown)[static_cast<std::size_t>(l)];
		grid->addWidget(label(lightNames[l], "Meta"), l + 1, 0);
		const auto colour = [&](int column, float value, const char *objectName, const std::function<void(MapModel::Light &, float)> &set) {
			auto *spin = new QSpinBox;
			spin->setObjectName(QString::fromLatin1(objectName).arg(l));
			spin->setRange(0, 255);
			spin->setValue(static_cast<int>(std::lround(value * 255.0f)));
			connect(spin, &QSpinBox::valueChanged, this, [changeLight, l, set](int v) {
				changeLight(l, [set, v](MapModel::Light &x) { set(x, static_cast<float>(v) / 255.0f); });
			});
			grid->addWidget(spin, l + 1, column);
		};
		for (int c = 0; c < 3; ++c)
			colour(1 + c, light.diffuse[static_cast<std::size_t>(c)], "LightDiffuse%1", [c](MapModel::Light &x, float v) { x.diffuse[static_cast<std::size_t>(c)] = v; });
		// Only the sun has an ambient part, as in the old dialog.
		for (int c = 0; c < 3 && l == 0; ++c)
			colour(4 + c, light.ambient[static_cast<std::size_t>(c)], "LightAmbient%1", [c](MapModel::Light &x, float v) { x.ambient[static_cast<std::size_t>(c)] = v; });
		float azimuth = 0.0f, elevation = 0.0f;
		MapEditorCore::lightAngles(light.position, azimuth, elevation);
		const auto angle = [&](int column, float value, const char *objectName, int minimum, int maximum, bool isAzimuth) {
			auto *spin = new QSpinBox;
			spin->setObjectName(QString::fromLatin1(objectName).arg(l));
			spin->setRange(minimum, maximum);
			spin->setSuffix(QStringLiteral("\u00b0"));
			spin->setValue(static_cast<int>(std::lround(value)));
			connect(spin, &QSpinBox::valueChanged, this, [changeLight, l, isAzimuth](int v) {
				changeLight(l, [isAzimuth, v](MapModel::Light &x) {
					float a = 0.0f, e = 0.0f;
					MapEditorCore::lightAngles(x.position, a, e);
					x.position = isAzimuth ? MapEditorCore::lightDirection(static_cast<float>(v), e) : MapEditorCore::lightDirection(a, static_cast<float>(v));
				});
			});
			grid->addWidget(spin, l + 1, column);
		};
		angle(7, azimuth, "LightAzimuth%1", -180, 180, true);
		angle(8, elevation, "LightElevation%1", -90, 90, false);
	}
	lcol->addLayout(grid);
	// The sun on a dial: drag it round for the direction and in or out for the height; the view shows
	// light and shadows while dragging, and letting go makes one undo step.
	if (shown)
	{
		auto *dial = new SunDial;
		dial->setObjectName(QStringLiteral("SunDial"));
		MapEditorCore::lightAngles((*shown)[0].position, dial->azimuth, dial->elevation);
		const int lightTarget = m_lightTarget;
		const auto applySun = [lightTarget](MapModel::Map &m, float azimuth, float elevation) {
			for (bool objects : {false, true})
			{
				if ((objects && lightTarget == 0) || (!objects && lightTarget == 1))
					continue;
				if (std::array<MapModel::Light, 3> *lights = lightsOf(m, objects))
					(*lights)[0].position = MapEditorCore::lightDirection(azimuth, elevation);
			}
		};
		dial->onMove = [this, applySun](float azimuth, float elevation) {
			if (!m_editor.openMap() || !m_editor.openMap()->map().lighting)
				return;
			// A copy for the view only; the map changes when the drag ends.
			MapModel::Map preview;
			preview.lighting = m_editor.openMap()->map().lighting;
			applySun(preview, azimuth, elevation);
			m_settings.previewLighting(*preview.lighting);
		};
		dial->onDone = [this, applySun](float azimuth, float elevation) {
			m_settings.editLighting("Move the sun", [applySun, azimuth, elevation](MapModel::Map &m) { applySun(m, azimuth, elevation); });
			QTimer::singleShot(0, this, [this] { refresh(); });
		};
		auto *sunRow = new QHBoxLayout;
		sunRow->addWidget(dial);
		auto *sunHint = label(T("Drag the sun: round the dial for the direction it comes from (north is up, as in the 2D view), towards the middle for a higher sun. Light and shadows follow in the 3D view at once."), "Hint");
		sunHint->setWordWrap(true);
		sunRow->addWidget(sunHint, 1);
		lcol->addLayout(sunRow);
	}
	auto *lightHint = label(T("The values of the map's time of day. Elevation 90 shines straight down; the view shows every change at once."), "Hint");
	lightHint->setWordWrap(true);
	lcol->addWidget(lightHint);
	lcol->addStretch(1);
	tabs->addTab(lighting, T("Lighting"));

	// Shadow colour: the map's own, or the game's default.
	auto *shadows = new QWidget;
	auto *scol = new QVBoxLayout(shadows);
	auto *own = new QCheckBox(T("Own shadow colour"));
	own->setObjectName(QStringLiteral("SettingsShadowOwn"));
	own->setChecked(map.lighting && map.lighting->shadowColor.has_value());
	own->setEnabled(map.lighting.has_value());
	scol->addWidget(own);
	const std::uint32_t argb = map.lighting && map.lighting->shadowColor ? *map.lighting->shadowColor : 0x7F000000u;
	auto *sgrid = new QGridLayout;
	const QStringList parts = {T("Red"), T("Green"), T("Blue"), T("Opacity")};
	const int shifts[] = {16, 8, 0, 24};
	std::vector<QSpinBox *> spins;
	for (int i = 0; i < 4; ++i)
	{
		auto *spin = new QSpinBox;
		spin->setObjectName(QStringLiteral("SettingsShadow%1").arg(i));
		spin->setRange(0, 255);
		spin->setValue(static_cast<int>((argb >> shifts[i]) & 0xFFu));
		spin->setEnabled(own->isChecked());
		sgrid->addWidget(label(parts[i], "Meta"), i, 0);
		sgrid->addWidget(spin, i, 1);
		spins.push_back(spin);
	}
	const auto writeShadow = [this, own, spins] {
		std::optional<std::uint32_t> value;
		if (own->isChecked())
			value = (static_cast<std::uint32_t>(spins[3]->value()) << 24) | (static_cast<std::uint32_t>(spins[0]->value()) << 16)
				| (static_cast<std::uint32_t>(spins[1]->value()) << 8) | static_cast<std::uint32_t>(spins[2]->value());
		m_settings.editLighting("Shadow colour", [value](MapModel::Map &m) {
			if (m.lighting)
				m.lighting->shadowColor = value;
		});
	};
	for (QSpinBox *spin : spins)
		connect(spin, &QSpinBox::valueChanged, this, writeShadow);
	connect(own, &QCheckBox::toggled, this, [spins, writeShadow](bool on) {
		for (QSpinBox *spin : spins)
			spin->setEnabled(on);
		writeShadow();
	});
	scol->addLayout(sgrid);
	scol->addStretch(1);
	tabs->addTab(shadows, T("Shadows"));
	tabs->addTab(waterTab(), T("Water"));
	tabs->setCurrentIndex(tab);
}

QWidget *MapSettingsWindow::waterTab()
{
	auto *page = new QWidget;
	auto *col = new QVBoxLayout(page);
	if (!m_settings.waterLook())
	{
		col->addWidget(label(T("The water values need the game data: open the map in the 3D view first."), "Hint", page));
		col->addStretch(1);
		return page;
	}
	MapEditorCore::WaterLook &look = *m_settings.waterLook();
	const auto changed = [this] { m_settings.showWaterLook(); };
	auto *form = new QFormLayout;
	const auto number = [&](const char *name, float value, double minimum, double maximum, double step, float MapEditorCore::WaterLook::*field) {
		auto *spin = new QDoubleSpinBox(page);
		spin->setObjectName(QString::fromLatin1(name));
		spin->setRange(minimum, maximum);
		spin->setSingleStep(step);
		spin->setDecimals(2);
		spin->setValue(static_cast<double>(value));
		connect(spin, &QDoubleSpinBox::valueChanged, this, [this, field, changed](double v) {
			if (m_settings.waterLook())
				(*m_settings.waterLook()).*field = static_cast<float>(v);
			changed();
		});
		return spin;
	};
	const auto colour = [&](const char *name, const std::array<int, 3> &value, std::array<int, 3> MapEditorCore::WaterLook::*field) {
		auto *button = new QPushButton(page);
		button->setObjectName(QString::fromLatin1(name));
		const auto paint = [button](const std::array<int, 3> &c) {
			button->setText(QStringLiteral("R %1  G %2  B %3").arg(c[0]).arg(c[1]).arg(c[2]));
			button->setStyleSheet(QStringLiteral("background: rgb(%1, %2, %3); color: %4;").arg(c[0]).arg(c[1]).arg(c[2])
				.arg(c[0] + c[1] + c[2] > 380 ? QStringLiteral("black") : QStringLiteral("white")));
		};
		paint(value);
		connect(button, &QPushButton::clicked, this, [this, button, field, changed, paint] {
			if (!m_settings.waterLook())
				return;
			const std::array<int, 3> c = (*m_settings.waterLook()).*field;
			const QColor chosen = QColorDialog::getColor(QColor(c[0], c[1], c[2]), button);
			if (!chosen.isValid())
				return;
			(*m_settings.waterLook()).*field = {chosen.red(), chosen.green(), chosen.blue()};
			paint((*m_settings.waterLook()).*field);
			changed();
		});
		return button;
	};
	col->addWidget(label(T("REALISTIC WATER (DETAIL LEVEL MEDIUM AND UP)"), "Section", page));
	form->addRow(T("Visibility depth"), number("WaterTurbidityDepth", look.turbidityDepth, 0.0, 500.0, 1.0, &MapEditorCore::WaterLook::turbidityDepth));
	form->addRow(T("Colour of the depth"), colour("WaterTurbidityColor", look.turbidityColor, &MapEditorCore::WaterLook::turbidityColor));
	form->addRow(T("Refraction"), number("WaterRefraction", look.refraction, 0.0, 3.0, 0.1, &MapEditorCore::WaterLook::refraction));
	form->addRow(T("Sky reflection"), number("WaterReflection", look.reflection, 0.0, 3.0, 0.1, &MapEditorCore::WaterLook::reflection));
	col->addLayout(form);
	auto *classicForm = new QFormLayout;
	col->addWidget(label(T("CLASSIC WATER"), "Section", page));
	classicForm->addRow(T("Transparent to depth"), number("WaterTransparentDepth", look.transparentDepth, 0.0, 100.0, 0.5, &MapEditorCore::WaterLook::transparentDepth));
	classicForm->addRow(T("Least opacity"), number("WaterMinOpacity", look.minOpacity, 0.0, 1.0, 0.05, &MapEditorCore::WaterLook::minOpacity));
	classicForm->addRow(T("Colour"), colour("WaterStandingColor", look.standingColor, &MapEditorCore::WaterLook::standingColor));
	auto *additive = new QCheckBox(T("Blend additively"), page);
	additive->setObjectName(QStringLiteral("WaterAdditive"));
	additive->setChecked(look.additive);
	connect(additive, &QCheckBox::toggled, this, [this, changed](bool on) {
		if (m_settings.waterLook())
			m_settings.waterLook()->additive = on;
		changed();
	});
	classicForm->addRow(QString(), additive);
	col->addLayout(classicForm);
	auto *hint = label(T("The 3D view shows every change at once (the realistic water with the graphics level Medium and up). Visibility depth, its colour and refraction act on lakes and ponds; rivers take the colour of the classic water and the sky reflection. A visibility depth of 0 keeps the classic water on this map. \"Write to map.ini\" saves the values that differ from the game data into the map.ini beside the map, which the game reads over Water.ini."), "Hint", page);
	hint->setWordWrap(true);
	col->addWidget(hint);
	auto *buttons = new QHBoxLayout;
	auto *write = new QPushButton(T("Write to map.ini"), page);
	write->setObjectName(QStringLiteral("WaterWrite"));
	connect(write, &QPushButton::clicked, this, [this] {
		QString error;
		m_editor.toast(m_settings.writeMapWaterLook(&error) ? T("The water values are in the map.ini.") : error);
	});
	auto *reset = new QPushButton(T("As the game data"), page);
	reset->setObjectName(QStringLiteral("WaterReset"));
	connect(reset, &QPushButton::clicked, this, [this] {
		m_settings.resetWaterLook();
		QTimer::singleShot(0, this, [this] { refresh(); });
	});
	buttons->addWidget(write);
	buttons->addWidget(reset);
	buttons->addStretch(1);
	col->addLayout(buttons);
	col->addStretch(1);
	return page;
}

} // namespace MapEditorApp
