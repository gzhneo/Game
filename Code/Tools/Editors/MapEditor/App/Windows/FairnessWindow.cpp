/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/FairnessWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace MapEditorApp
{

namespace
{

constexpr int ROLE_RESOURCE = Qt::UserRole + 1;
// A start counts as worse off from this share above the mean of the nearest supply.
constexpr float UNEVEN = 0.15f;

// Player colours of the territories (as the game's first eight multiplayer colours, roughly).
constexpr std::array<std::uint32_t, 8> TERRITORY = {0xd02020u, 0x2050e0u, 0x30b030u, 0xe0c020u, 0xe07020u, 0x9040c0u, 0x20c0c0u, 0xe060a0u};

QString distanceText(float d)
{
	return d < 0.0f ? T("unreachable") : QString::number(static_cast<int>(std::lround(d)));
}

} // namespace

std::vector<std::uint32_t> FairnessWindow::territoryTint(const MapEditorCore::FairnessReport &report)
{
	std::vector<std::uint32_t> out(report.territory.size(), 0u);
	for (std::size_t i = 0; i < report.territory.size(); ++i)
	{
		const int owner = report.territory[i];
		if (owner == -2)
			out[i] = 0x70ffffffu;
		else if (owner >= 0 && owner < static_cast<int>(report.starts.size()))
			out[i] = 0x58000000u | TERRITORY[static_cast<std::size_t>(report.starts[static_cast<std::size_t>(owner)].number - 1) % TERRITORY.size()];
	}
	return out;
}

FairnessWindow::FairnessWindow(QWidget *parent)
	: QDialog(parent), m_summary(label(QString(), nullptr, this)), m_starts(new QTreeWidget(this)), m_resources(new QTreeWidget(this)),
	  m_territories(new QCheckBox(T("Show the territories in the views (each start's colour where it arrives first, white where two arrive together)"), this))
{
	setObjectName(QStringLiteral("FairnessWindow"));
	setWindowTitle(T("Multiplayer fairness"));
	resize(860, 600);
	auto *col = new QVBoxLayout(this);
	m_summary->setWordWrap(true);
	col->addWidget(m_summary);
	m_starts->setHeaderLabels({T("Start"), T("Nearest supply"), T("Supplies reached first"), T("Tech reached first"), T("Nearest other start"), T("Out of reach")});
	m_starts->setRootIsDecorated(false);
	m_starts->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	col->addWidget(m_starts, 1);
	m_resources->setHeaderLabels({T("Building"), T("Position"), T("Reached first by"), T("Path lengths")});
	m_resources->setRootIsDecorated(false);
	m_resources->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	col->addWidget(m_resources, 2);
	connect(m_resources, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
		const QPointF at = item->data(0, ROLE_RESOURCE).toPointF();
		emit centerOn(static_cast<float>(at.x()), static_cast<float>(at.y()));
	});
	connect(m_territories, &QCheckBox::toggled, this, [this] { emitTerritories(); });
	col->addWidget(m_territories);
	auto *hint = label(T("Path lengths in world units along the ground units can cross: no cliff flag, not under water, inside the border. Buildings on the way are not counted. A double-click shows a building."), "Hint", this);
	hint->setWordWrap(true);
	col->addWidget(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto *again = buttons->addButton(T("Measure again"), QDialogButtonBox::ActionRole);
	connect(again, &QPushButton::clicked, this, [this] { measure(); });
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
	col->addWidget(buttons);
	// The territories belong to the open report.
	connect(this, &QDialog::finished, this, [this] { m_territories->setChecked(false); });
}

void FairnessWindow::setMap(const MapModel::Map *map)
{
	m_map = map;
	if (!map)
		m_territories->setChecked(false);
}

void FairnessWindow::setTerritoriesShown(bool shown)
{
	m_territories->setChecked(shown);
}

QString FairnessWindow::summary() const
{
	return m_summary->text();
}

void FairnessWindow::emitTerritories()
{
	emit territoriesChanged(m_territories->isChecked() ? territoryTint(m_report) : std::vector<std::uint32_t>());
}

void FairnessWindow::measure()
{
	m_report = m_map ? MapEditorCore::fairnessReport(*m_map) : MapEditorCore::FairnessReport();
	const MapEditorCore::FairnessReport &report = m_report;
	m_starts->clear();
	m_resources->clear();
	int supplies = 0;
	int tech = 0;
	for (const MapEditorCore::ResourceFairness &r : report.resources)
		(r.kind == MapEditorCore::ResourceKind::Supply ? supplies : tech) += 1;
	float mean = 0.0f;
	int counted = 0;
	for (const MapEditorCore::StartFairness &s : report.starts)
		if (s.nearestSupply >= 0.0f)
		{
			mean += s.nearestSupply;
			++counted;
		}
	mean = counted ? mean / static_cast<float>(counted) : 0.0f;
	const QBrush warn(QColor(0xd0, 0x70, 0x10));
	int worse = 0;
	for (const MapEditorCore::StartFairness &s : report.starts)
	{
		auto *item = new QTreeWidgetItem(m_starts, {T("Player %1").arg(s.number), distanceText(s.nearestSupply), QString::number(s.suppliesFirst),
			QString::number(s.techFirst), distanceText(s.nearestEnemy), QString::number(s.unreachable)});
		// Without any supply on the map there is nothing to be farther from.
		const bool farther = supplies > 0 && (s.nearestSupply < 0.0f || (mean > 0.0f && s.nearestSupply > mean * (1.0f + UNEVEN)));
		if (farther)
			item->setForeground(1, warn);
		if (s.unreachable > 0)
			item->setForeground(5, warn);
		worse += farther || s.unreachable > 0 ? 1 : 0;
	}
	for (const MapEditorCore::ResourceFairness &r : report.resources)
	{
		QStringList lengths;
		for (std::size_t i = 0; i < r.distance.size() && i < report.starts.size(); ++i)
			lengths << QStringLiteral("%1: %2").arg(report.starts[i].number).arg(distanceText(r.distance[i]));
		const QString first = r.first < 0 ? T("no one") : r.contested ? T("contested") : T("Player %1").arg(report.starts[static_cast<std::size_t>(r.first)].number);
		auto *item = new QTreeWidgetItem(m_resources, {utf8(r.templateName), QStringLiteral("%1, %2").arg(r.x, 0, 'f', 0).arg(r.y, 0, 'f', 0), first, lengths.join(QStringLiteral("   "))});
		item->setData(0, ROLE_RESOURCE, QPointF(r.x, r.y));
	}
	QString text = T("%1 start positions, %2 supplies, %3 tech buildings.").arg(report.starts.size()).arg(supplies).arg(tech);
	if (report.starts.size() < 2)
		text += QLatin1Char(' ') + T("A fair comparison needs at least two waypoints Player_1_Start, Player_2_Start, ...");
	else
	{
		if (supplies > 0)
			text += QLatin1Char(' ') + T("The nearest supply lies %1 % farther for the worst-placed start than for the best.").arg(static_cast<int>(std::lround(report.supplySpread * 100.0f)));
		text += QLatin1Char(' ') + (worse ? T("%1 starts are worse off (marked).").arg(worse) : T("No start is worse off."));
	}
	m_summary->setText(text);
	if (m_territories->isChecked())
		emitTerritories();
}

} // namespace MapEditorApp
