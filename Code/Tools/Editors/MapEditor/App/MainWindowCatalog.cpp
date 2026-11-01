/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowCatalog.cpp -- the objects mode: the catalog of every object template of the game
// (grouped by side and editor sorting, with a search), the team for new objects, and placing the
// chosen template by a click in the 3D view or the overview (a drag sets its direction). Placing
// stays armed until Esc or another mode; every placed object is one step in the history. Below the
// list a picture of the chosen model, drawn by the engine; dragging it turns the model.

#include "App/Common.h"
#include "App/MainWindow.h"
#include "App/MainWindowParts.h"

#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapOverview.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/WaypointCommands.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace MapEditorApp
{

namespace
{

// The team of neutral objects, as the game and the old WorldBuilder name it.

constexpr int ROLE_NAME = Qt::UserRole;
constexpr int ROLE_ANGLE = Qt::UserRole + 1;
constexpr int ROLE_SEARCH = Qt::UserRole + 2;

// Editor sorting of the engine (EditorSortingType); roads and waypoints have their own tools.
QString sortingName(int sorting)
{
	switch (sorting)
	{
		case 1: return T("Structures");
		case 2: return T("Infantry");
		case 3: return T("Vehicles");
		case 4: return T("Shrubbery");
		case 5: return T("Man-made");
		case 6: return T("Natural");
		case 7: return T("Debris");
		case 8: return T("System");
		case 9: return T("Audio");
		case 10: return T("Test");
		case 11: return T("For review");
		default: return T("Unsorted");
	}
}

bool listedSorting(int sorting)
{
	return sorting != 12 && sorting != 13;
}

// Hides the templates that do not match; groups without a match go too. A search opens the groups.
int applyCatalogFilter(QTreeWidget *tree, const QString &filter)
{
	int shown = 0;
	for (int s = 0; s < tree->topLevelItemCount(); ++s)
	{
		QTreeWidgetItem *side = tree->topLevelItem(s);
		bool sideShown = false;
		for (int c = 0; c < side->childCount(); ++c)
		{
			QTreeWidgetItem *category = side->child(c);
			bool categoryShown = false;
			for (int i = 0; i < category->childCount(); ++i)
			{
				QTreeWidgetItem *item = category->child(i);
				const bool match = filter.isEmpty() || item->data(0, ROLE_SEARCH).toString().contains(filter, Qt::CaseInsensitive);
				item->setHidden(!match);
				if (match)
				{
					++shown;
					categoryShown = true;
				}
			}
			category->setHidden(!categoryShown);
			if (!filter.isEmpty())
				category->setExpanded(categoryShown);
			sideShown = sideShown || categoryShown;
		}
		side->setHidden(!sideShown);
		if (!filter.isEmpty())
			side->setExpanded(sideShown);
	}
	return shown;
}

// Picture of the model; dragging sideways turns it.
class PreviewLabel : public QLabel
{
public:
	std::function<void(double dx)> onTurn;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		m_dragging = event->button() == Qt::LeftButton;
		m_lastX = event->position().x();
	}
	void mouseMoveEvent(QMouseEvent *event) override
	{
		if (!m_dragging || !onTurn)
			return;
		const double x = event->position().x();
		onTurn(x - m_lastX);
		m_lastX = x;
	}
	void mouseReleaseEvent(QMouseEvent *) override { m_dragging = false; }

private:
	bool m_dragging = false;
	double m_lastX = 0.0;
};

constexpr int PREVIEW_SHOWN = 200;	// logical pixels

} // namespace

QTreeWidgetItem *findTemplate(QTreeWidget *tree, const QString &name)
{
	for (int s = 0; s < tree->topLevelItemCount(); ++s)
	{
		QTreeWidgetItem *side = tree->topLevelItem(s);
		for (int c = 0; c < side->childCount(); ++c)
		{
			QTreeWidgetItem *category = side->child(c);
			for (int i = 0; i < category->childCount(); ++i)
			{
				if (category->child(i)->data(0, ROLE_NAME).toString() == name)
					return category->child(i);
			}
		}
	}
	return nullptr;
}

QWidget *MainWindow::buildCatalogPanel(const Tokens &t)
{
	auto *panel = new QWidget;
	panel->setObjectName(QStringLiteral("Panel"));
	panel->setAttribute(Qt::WA_StyledBackground);
	panel->setFixedWidth(300);
	auto *col = new QVBoxLayout(panel);
	col->setContentsMargins(18, 18, 18, 18);
	col->setSpacing(8);
	col->addWidget(label(T("OBJECTS"), "Section"));

	QString error;
	bool chooseData = false;
	bool restart = false;
	// Without the engine only the editor's own entries (waypoints) are there.
	const bool engineReady = ensureEngine(&error, &chooseData, &restart);
	if (!engineReady)
	{
		auto *note = label(T("The object templates need the engine: %1").arg(error), "Hint");
		note->setWordWrap(true);
		col->addWidget(note);
		if (chooseData)
		{
			auto *choose = new QPushButton(T("Choose game data folder\u2026"));
			const Game game = m_state.game;
			connect(choose, &QPushButton::clicked, this, [this, game] { chooseDataFolder(game); });
			col->addWidget(choose);
		}
	}

	auto *search = new QLineEdit;
	search->setObjectName(QStringLiteral("CatalogSearch"));
	search->setPlaceholderText(T("Search templates"));
	search->setClearButtonEnabled(true);
	search->addAction(icon(QStringLiteral("search"), t.tx3), QLineEdit::LeadingPosition);
	col->addWidget(search);

	// Team of new objects: the neutral one or a team of the map.
	auto *teamRow = new QHBoxLayout;
	teamRow->addWidget(label(T("Team"), "Hint"));
	auto *team = new QComboBox;
	team->addItem(T("(neutral)"), QString::fromLatin1(NEUTRAL_TEAM));
	if (m_state.map && m_state.map->map().sides)
	{
		for (const MapModel::PropertyMap &entry : m_state.map->map().sides->teams)
		{
			const std::string teamName = entry.getAscii("teamName").value_or("");
			if (!teamName.empty() && teamName != NEUTRAL_TEAM)
				team->addItem(utf8(teamName), utf8(teamName));
		}
	}
	const int teamIndex = team->findData(m_placeTeam);
	if (teamIndex < 0)
		m_placeTeam = QString::fromLatin1(NEUTRAL_TEAM);
	team->setCurrentIndex(std::max(0, teamIndex));
	connect(team, &QComboBox::currentIndexChanged, this, [this, team](int) {
		m_placeTeam = team->currentData().toString();
		updateCatalogPreview();
	});
	teamRow->addWidget(team, 1);
	col->addLayout(teamRow);

	// Side, then editor sorting, then the templates by name.
	auto *tree = new QTreeWidget;
	tree->setHeaderHidden(true);
	tree->setUniformRowHeights(true);
	std::map<QString, std::map<int, std::vector<const MeTemplateInfo *>>> groups;
	static const std::vector<MeTemplateInfo> none;
	const std::vector<MeTemplateInfo> &catalog = engineReady ? EngineClient::instance().catalog() : none;
	for (const MeTemplateInfo &info : catalog)
	{
		if (listedSorting(info.sorting))
			groups[QString::fromUtf8(info.side)][info.sorting].push_back(&info);
	}
	// Waypoints first: they are no template of the game, the editor places them itself.
	int total = 1;
	{
		auto *markers = new QTreeWidgetItem(tree, {T("Map markers")});
		auto *category = new QTreeWidgetItem(markers, {T("Waypoints")});
		auto *item = new QTreeWidgetItem(category, {T("Waypoint")});
		item->setData(0, ROLE_NAME, QString::fromLatin1(MapEditorCore::WAYPOINT_TEMPLATE));
		item->setData(0, ROLE_ANGLE, 0.0);
		item->setData(0, ROLE_SEARCH, QStringLiteral("Waypoint\n") + T("Waypoint"));
		auto *roads = new QTreeWidgetItem(markers, {T("Roads")});
		for (const MeRoadInfo &road : engineReady ? EngineClient::instance().roads() : std::vector<MeRoadInfo>{})
		{
			if (road.bridge)
				continue;
			const QString roadName = QString::fromUtf8(road.name);
			auto *entry = new QTreeWidgetItem(roads, {roadName});
			entry->setData(0, ROLE_NAME, QString::fromLatin1(ROAD_TOOL_PREFIX) + roadName);
			entry->setData(0, ROLE_ANGLE, 0.0);
			entry->setData(0, ROLE_SEARCH, roadName + QLatin1Char('\n') + T("Road"));
			++total;
		}
		if (roads->childCount() == 0)
			delete roads;
		auto *bridges = new QTreeWidgetItem(markers, {T("Bridges")});
		for (const MeRoadInfo &road : engineReady ? EngineClient::instance().roads() : std::vector<MeRoadInfo>{})
		{
			if (!road.bridge)
				continue;
			const QString bridgeName = QString::fromUtf8(road.name);
			auto *entry = new QTreeWidgetItem(bridges, {bridgeName});
			entry->setData(0, ROLE_NAME, QString::fromLatin1(BRIDGE_TOOL_PREFIX) + bridgeName);
			entry->setData(0, ROLE_ANGLE, 0.0);
			entry->setData(0, ROLE_SEARCH, bridgeName + QLatin1Char('\n') + T("Bridge"));
			++total;
		}
		if (bridges->childCount() == 0)
			delete bridges;
		// Groves, fences and scorch marks: tools that place objects of the game.
		auto *nature = new QTreeWidgetItem(markers, {T("Grove")});
		nature->setData(0, ROLE_NAME, QString::fromLatin1(GROVE_TOOL));
		nature->setData(0, ROLE_ANGLE, 0.0);
		nature->setData(0, ROLE_SEARCH, QStringLiteral("Grove\n") + T("Grove") + QLatin1Char('\n') + T("Trees"));
		++total;
		auto *fences = new QTreeWidgetItem(markers, {T("Fences")});
		for (const MeTemplateInfo &info : catalog)
		{
			if (info.fenceWidth <= 0.0f)
				continue;
			const QString fenceName = QString::fromUtf8(info.name);
			auto *entry = new QTreeWidgetItem(fences, {fenceName});
			entry->setData(0, ROLE_NAME, QString::fromLatin1(FENCE_TOOL_PREFIX) + fenceName);
			entry->setData(0, ROLE_ANGLE, 0.0);
			entry->setData(0, ROLE_SEARCH, fenceName + QLatin1Char('\n') + T("Fence"));
			++total;
		}
		if (fences->childCount() == 0)
			delete fences;
		auto *scorch = new QTreeWidgetItem(markers, {T("Scorch marks")});
		scorch->setData(0, ROLE_NAME, QString::fromLatin1(SCORCH_TOOL));
		scorch->setData(0, ROLE_ANGLE, 0.0);
		scorch->setData(0, ROLE_SEARCH, QStringLiteral("Scorch\n") + T("Scorch marks"));
		++total;
		auto *ruler = new QTreeWidgetItem(markers, {T("Ruler")});
		ruler->setData(0, ROLE_NAME, QString::fromLatin1(RULER_TOOL));
		ruler->setData(0, ROLE_ANGLE, 0.0);
		ruler->setData(0, ROLE_SEARCH, QStringLiteral("Ruler\n") + T("Ruler"));
		++total;
		auto *buildList = new QTreeWidgetItem(markers, {T("Build list")});
		buildList->setData(0, ROLE_NAME, QString::fromLatin1(BUILD_LIST_TOOL));
		buildList->setData(0, ROLE_ANGLE, 0.0);
		buildList->setData(0, ROLE_SEARCH, QStringLiteral("Build list\n") + T("Build list"));
		++total;
		auto *border = new QTreeWidgetItem(markers, {T("Playable areas")});
		border->setData(0, ROLE_NAME, QString::fromLatin1(BORDER_TOOL));
		border->setData(0, ROLE_ANGLE, 0.0);
		border->setData(0, ROLE_SEARCH, QStringLiteral("Border\n") + T("Playable areas"));
		++total;
		// Prefabs: selections saved under a name (Edit -> Save selection as prefab).
		const QStringList prefabs = prefabNames();
		if (!prefabs.isEmpty())
		{
			auto *group = new QTreeWidgetItem(markers, {T("Prefabs")});
			for (const QString &prefab : prefabs)
			{
				auto *entry = new QTreeWidgetItem(group, {prefab});
				entry->setData(0, ROLE_NAME, QString::fromLatin1(PREFAB_TOOL_PREFIX) + prefab);
				entry->setData(0, ROLE_ANGLE, 0.0);
				entry->setData(0, ROLE_SEARCH, prefab);
				++total;
			}
		}
		auto *areas = new QTreeWidgetItem(markers, {T("Areas")});
		const std::pair<const char *, QString> tools[] = {{AREA_TRIGGER_TOOL, T("Trigger area")}, {AREA_WATER_TOOL, T("Water area")}};
		for (const auto &[tool, text] : tools)
		{
			auto *entry = new QTreeWidgetItem(areas, {text});
			entry->setData(0, ROLE_NAME, QString::fromLatin1(tool));
			entry->setData(0, ROLE_ANGLE, 0.0);
			entry->setData(0, ROLE_SEARCH, QString::fromLatin1(tool) + QLatin1Char('\n') + text);
			++total;
		}
	}
	for (auto &[side, categories] : groups)
	{
		auto *sideItem = new QTreeWidgetItem(tree, {side.isEmpty() ? T("(no side)") : side});
		for (auto &[sorting, templates] : categories)
		{
			auto *categoryItem = new QTreeWidgetItem(sideItem, {sortingName(sorting)});
			std::sort(templates.begin(), templates.end(), [](const MeTemplateInfo *a, const MeTemplateInfo *b) {
				return QString::compare(QString::fromUtf8(a->name), QString::fromUtf8(b->name), Qt::CaseInsensitive) < 0;
			});
			for (const MeTemplateInfo *info : templates)
			{
				const QString name = QString::fromUtf8(info->name);
				const QString shown = QString::fromUtf8(info->displayName);
				auto *item = new QTreeWidgetItem(categoryItem, {name});
				item->setData(0, ROLE_NAME, name);
				item->setData(0, ROLE_ANGLE, static_cast<double>(info->placementAngle));
				item->setData(0, ROLE_SEARCH, name + QLatin1Char('\n') + shown);
				if (!shown.isEmpty())
					item->setToolTip(0, shown);
				++total;
			}
		}
	}
	col->addWidget(tree, 1);
	auto *count = label(QString(), "Hint");
	col->addWidget(count);
	m_catalogTree = tree;

	auto *previewName = label(QString(), "Secondary");
	previewName->setWordWrap(true);
	col->addWidget(previewName);
	auto *preview = new PreviewLabel;
	preview->setFixedSize(PREVIEW_SHOWN, PREVIEW_SHOWN);
	preview->setAlignment(Qt::AlignCenter);
	preview->setWordWrap(true);
	preview->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2; border-radius: 6px; color: %3;")
		.arg(t.s2.name(), t.bd.name(), t.tx3.name()));
	preview->setToolTip(T("Drag sideways to turn the model"));
	preview->onTurn = [this](double dx) {
		m_previewYaw -= static_cast<float>(dx) * 0.02f;
		updateCatalogPreview();
	};
	col->addWidget(preview, 0, Qt::AlignHCenter);
	m_catalogPreview = preview;
	m_catalogPreviewName = previewName;
	m_previewBackground = t.s2;

	const auto filter = [tree, count, total](const QString &text) {
		const int shown = applyCatalogFilter(tree, text.trimmed());
		count->setText(shown == total ? T("%1 templates").arg(total) : T("%1 of %2 templates").arg(shown).arg(total));
	};
	search->setText(m_catalogFilter);
	filter(m_catalogFilter);
	connect(search, &QLineEdit::textChanged, this, [this, filter](const QString &text) {
		m_catalogFilter = text;
		filter(text);
	});

	// The template being placed stays chosen when the page is rebuilt.
	if (QTreeWidgetItem *current = findTemplate(tree, m_placeTemplate))
	{
		for (QTreeWidgetItem *p = current->parent(); p; p = p->parent())
			p->setExpanded(true);
		tree->setCurrentItem(current);
		tree->scrollToItem(current);
	}
	else
	{
		m_placeTemplate.clear();
	}
	connect(tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
		if (item && !item->data(0, ROLE_NAME).toString().isEmpty())
			setPlacingTemplate(item->data(0, ROLE_NAME).toString(), static_cast<float>(item->data(0, ROLE_ANGLE).toDouble()));
		else
			setPlacingTemplate(QString(), 0.0f);
	});
	setPlacingTemplate(m_placeTemplate, m_placeAngle);
	return panel;
}

void MainWindow::setPlacingTemplate(const QString &name, float placementAngle)
{
	if (name != m_placeTemplate)
	{
		// Another tool: an area being drawn is dropped, the area and road selections end.
		m_areaDraft.reset();
		m_areaDrag = AreaDrag{};
		m_area = -1;
		m_areaPoint = -1;
		m_roadDrag = RoadDrag{};
		m_road = -1;
		m_shapeDrag = ShapeDrag{};
		m_boundary = -1;
		m_boundaryDrag = BoundaryDrag{};
		// A fence starts with its template's spacing.
		if (name.startsWith(QLatin1String(FENCE_TOOL_PREFIX)))
		{
			const QByteArray fence = name.mid(static_cast<qsizetype>(std::strlen(FENCE_TOOL_PREFIX))).toUtf8();
			for (const MeTemplateInfo &info : EngineClient::instance().catalog())
			{
				if (fence == info.name)
				{
					m_fenceSpacing = info.fenceWidth;
					m_fenceOffset = info.fenceOffset;
				}
			}
		}
	}
	m_placeTemplate = name;
	m_placeAngle = placementAngle;
	const bool tool = areaTool() || roadTool() || terrainTool() || placementTool();
	const bool placing = !name.isEmpty() && !tool;
	if (m_viewport)
	{
		m_viewport->setPlacing(placing);
		m_viewport->setToolMode(tool);
	}
	if (m_overview)
	{
		m_overview->setPlacing(placing);
		m_overview->setToolMode(tool);
	}
	areasChanged();
	roadsChanged();
	fillSelectionPanel();
	if (!placing && m_catalogTree && m_catalogTree->currentItem())
	{
		const QSignalBlocker block(m_catalogTree.data());
		m_catalogTree->setCurrentItem(nullptr);
		m_catalogTree->clearSelection();
	}
	updateSelectionStatus();
	updateCatalogPreview();
}

void MainWindow::updateCatalogPreview()
{
	if (!m_catalogPreview)
		return;
	QLabel *preview = m_catalogPreview;
	if (m_placeTemplate.isEmpty())
	{
		preview->setPixmap(QPixmap());
		preview->setText(T("Choose a template"));
		if (m_catalogPreviewName)
			m_catalogPreviewName->clear();
		return;
	}
	if (roadTool())
	{
		preview->setPixmap(QPixmap());
		preview->setText(bridgeTool() ? T("Bridges span between two points; the terrain places the bridge model of the type there.")
			: T("Roads are drawn by the terrain between their points; segments join where their ends meet."));
		if (m_catalogPreviewName)
			m_catalogPreviewName->setText(QString::fromStdString(roadToolType()));
		return;
	}
	if (areaTool())
	{
		preview->setPixmap(QPixmap());
		preview->setText(T("Trigger areas start scripts when units enter them; water areas hold standing water or rivers."));
		if (m_catalogPreviewName)
			m_catalogPreviewName->setText(m_placeTemplate == QLatin1String(AREA_WATER_TOOL) ? T("Water area") : T("Trigger area"));
		return;
	}
	if (m_placeTemplate == QLatin1String(MapEditorCore::WAYPOINT_TEMPLATE))
	{
		preview->setPixmap(QPixmap());
		preview->setText(T("A waypoint marks a place for scripts and paths of the computer players."));
		if (m_catalogPreviewName)
			m_catalogPreviewName->setText(T("Waypoint"));
		return;
	}
	if (m_catalogPreviewName)
	{
		const QTreeWidgetItem *item = m_catalogTree ? m_catalogTree->currentItem() : nullptr;
		const QString shown = item ? item->toolTip(0) : QString();
		m_catalogPreviewName->setText(shown.isEmpty() ? m_placeTemplate : shown);
	}
	// The picture needs the 3D device, also while the overview is shown.
	QString error;
	QImage image;
	const qreal ratio = preview->devicePixelRatioF();
	const int size = PREVIEW_SHOWN * ratio > 256.0 ? 512 : 256;
	if (m_viewport && m_viewport->ensureDevice(&error))
		image = EngineClient::instance().preview(m_placeTemplate, size, m_previewYaw, m_placeTeam, m_previewBackground);
	if (image.isNull())
	{
		preview->setPixmap(QPixmap());
		preview->setText(error.isEmpty() ? T("No model") : error);
		return;
	}
	const int pixels = static_cast<int>(std::lround(PREVIEW_SHOWN * ratio)) - 2;
	QPixmap pixmap = QPixmap::fromImage(image.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	pixmap.setDevicePixelRatio(ratio);
	preview->setPixmap(pixmap);
}

void MainWindow::placeObject(float x, float y, float angle, bool aimed, int object, bool ctrl)
{
	if (!m_state.map || m_placeTemplate.isEmpty() || !m_dragStart.empty() || !m_turnStart.empty())
		return;
	if (m_placeTemplate.startsWith(QLatin1String(PREFAB_TOOL_PREFIX)))
	{
		placePrefab(m_placeTemplate.mid(static_cast<qsizetype>(std::strlen(PREFAB_TOOL_PREFIX))), x, y);
		return;
	}
	x = snapCoord(x);
	y = snapCoord(y);
	if (m_placeTemplate == QLatin1String(MapEditorCore::WAYPOINT_TEMPLATE))
	{
		placeWaypoint(x, y, object, ctrl);
		return;
	}
	MapModel::MapObject placed;
	placed.x = x;
	placed.y = y;
	placed.angle = aimed ? angle : m_placeAngle;
	placed.templateName = m_placeTemplate.toUtf8().toStdString();
	placed.properties.set("originalOwner", MapModel::PropertyValue(m_placeTeam.toUtf8().toStdString()));
	if (!m_activeLayer.empty())
		placed.properties.set("objectLayer", MapModel::PropertyValue(m_activeLayer));
	MapEditorCore::Clipboard clipboard;
	clipboard.objects.push_back(std::move(placed));
	// Paste gives the object a unique ID of the map, as for copies.
	auto command = MapEditorCore::pasteObjects(m_state.map->map(), clipboard, 0.0f, 0.0f, "Place object");
	MapEditorCore::InsertObjects *inserted = command.get();
	m_state.map->history.execute(std::move(command), m_state.map->map());
	applyChanges(inserted->changes());
}

void MainWindow::escape()
{
	if (m_mold.active)
		finishMold(false);
	else if (m_shapeDrag.active)
	{
		m_shapeDrag = ShapeDrag{};
		if (m_overview)
			m_overview->setToolLines(toolLines());
		applyOverlay();
	}
	else if (m_areaDraft)
	{
		m_areaDraft.reset();
		areasChanged();
		fillSelectionPanel();
	}
	else if (m_roadDrag.active)
	{
		// A cancelled Shift+drag puts the ends back.
		for (std::size_t n = 0; m_roadDrag.move && n < m_roadDrag.points.size(); ++n)
			(*m_state.map->map().objects)[static_cast<std::size_t>(m_roadDrag.points[n])] = m_roadDrag.before[n];
		m_roadDrag = RoadDrag{};
		roadsChanged();
	}
	else if (roadTool() && m_road >= 0)
	{
		m_road = -1;
		roadsChanged();
		fillSelectionPanel();
	}
	else if (areaTool() && m_area >= 0)
	{
		m_area = -1;
		m_areaPoint = -1;
		areasChanged();
		fillSelectionPanel();
	}
	else if (!m_placeTemplate.isEmpty())
		setPlacingTemplate(QString(), 0.0f);
	else
		selectNone();
}

// ------------------------------------------------------------------------------------------------
// Self test: the catalog, placing in the 3D view and in the overview, undo.

int MainWindow::freeFileStreams()
{
	const QString self = QCoreApplication::applicationFilePath();
	std::vector<std::FILE *> files;
	for (int i = 0; i < 8192; ++i)
	{
#ifdef _WIN32
		std::FILE *f = _wfopen(QDir::toNativeSeparators(self).toStdWString().c_str(), L"rb");
#else
		std::FILE *f = std::fopen(self.toLocal8Bit().constData(), "rb");
#endif
		if (!f)
			break;
		files.push_back(f);
	}
	for (std::FILE *f : files)
		std::fclose(f);
	return static_cast<int>(files.size());
}

} // namespace MapEditorApp
