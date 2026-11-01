/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowCompare.cpp -- comparing the open map with another map file (what changed between two
// versions), and the height map as a grey picture: exported to edit it elsewhere, imported back as
// one undo step.

#include "App/Common.h"
#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/HeightImage.h"
#include "Core/MapCompare.h"
#include "Model/MapFile.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QListWidget>
#include <QSpinBox>
#include <QVBoxLayout>

#include <filesystem>

namespace MapEditorApp
{

namespace
{

bool isObjectKind(MapEditorCore::MapDifference::Kind kind)
{
	using Kind = MapEditorCore::MapDifference::Kind;
	return kind == Kind::ObjectAdded || kind == Kind::ObjectMoved || kind == Kind::ObjectChanged;
}

bool isAreaKind(MapEditorCore::MapDifference::Kind kind)
{
	using Kind = MapEditorCore::MapDifference::Kind;
	return kind == Kind::AreaAdded || kind == Kind::AreaChanged;
}

} // namespace

QString MainWindow::differenceText(const MapEditorCore::MapDifference &d) const
{
	using Kind = MapEditorCore::MapDifference::Kind;
	const QString s = utf8(d.subject);
	const QString detail = utf8(d.detail);
	switch (d.kind)
	{
	case Kind::Size: return T("Different size: %1").arg(detail.isEmpty() ? s : detail);
	case Kind::Heights: return T("%1 height samples differ").arg(d.count);
	case Kind::Textures: return T("%1 cells have another texture").arg(d.count);
	case Kind::ObjectAdded: return T("New object %1").arg(s);
	case Kind::ObjectRemoved: return T("Object %1 is gone").arg(s);
	case Kind::ObjectMoved: return T("Object %1 moved (%2)").arg(s, detail);
	case Kind::ObjectChanged: return T("Object %1 changed: %2").arg(s, detail);
	case Kind::WaypointLinks: return T("%1 waypoint links differ").arg(d.count);
	case Kind::AreaAdded: return T("New area %1").arg(s);
	case Kind::AreaRemoved: return T("Area %1 is gone").arg(s);
	case Kind::AreaChanged: return T("Area %1 changed: %2").arg(s, detail);
	case Kind::PlayerAdded: return T("New player %1").arg(s);
	case Kind::PlayerRemoved: return T("Player %1 is gone").arg(s);
	case Kind::TeamAdded: return T("New team %1").arg(s);
	case Kind::TeamRemoved: return T("Team %1 is gone").arg(s);
	case Kind::TeamChanged: return T("Team %1 changed: %2").arg(s, detail);
	case Kind::ScriptAdded: return T("New script %1 (%2)").arg(s, detail);
	case Kind::ScriptRemoved: return T("Script %1 (%2) is gone").arg(s, detail);
	case Kind::ScriptChanged: return T("Script %1 (%2) changed").arg(s, detail);
	case Kind::Lighting: return T("The lighting differs");
	case Kind::WorldInfo: return T("Map settings differ: %1").arg(detail);
	}
	return s;
}

std::vector<MapEditorCore::MapDifference> MainWindow::compareWithFile(const QString &path, QString *error)
{
	if (!m_state.map)
		return {};
	const MapModel::LoadedMap other = MapModel::loadMap(toFsPath(path));
	if (!other.result.ok())
	{
		*error = T("The map cannot be read: %1").arg(utf8(other.result.error));
		return {};
	}
	return MapEditorCore::compareMaps(m_state.map->map(), other.result.map);
}

void MainWindow::compareDialog()
{
	if (!m_state.map)
		return;
	const QString path = QFileDialog::getOpenFileName(this, T("Compare with map"), QFileInfo(m_state.map->path).dir().path(), T("Maps (*.map)"));
	if (path.isEmpty())
		return;
	QString error;
	const std::vector<MapEditorCore::MapDifference> differences = compareWithFile(path, &error);
	if (!error.isEmpty())
	{
		toast(error);
		return;
	}
	auto *dialog = new QDialog(this);
	dialog->setObjectName(QStringLiteral("CompareWindow"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(T("Compared with %1").arg(QFileInfo(path).fileName()));
	dialog->resize(760, 480);
	auto *col = new QVBoxLayout(dialog);
	col->addWidget(label(differences.empty() ? T("The maps are the same.")
		: T("%1 differences; a double-click shows an object or area of the open map.").arg(differences.size()), "Hint", dialog));
	auto *list = new QListWidget(dialog);
	list->setObjectName(QStringLiteral("DifferenceList"));
	for (const MapEditorCore::MapDifference &d : differences)
	{
		auto *item = new QListWidgetItem(differenceText(d), list);
		item->setData(Qt::UserRole, static_cast<int>(d.kind));
		item->setData(Qt::UserRole + 1, static_cast<int>(d.index));
	}
	connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		const auto kind = static_cast<MapEditorCore::MapDifference::Kind>(item->data(Qt::UserRole).toInt());
		const int index = item->data(Qt::UserRole + 1).toInt();
		if (index < 0 || !m_state.map)
			return;
		if (isObjectKind(kind))
		{
			m_state.map->selection.set(index);
			selectionChanged();
			centerOnSelection();
		}
		else if (isAreaKind(kind))
			goToArea(index);
	});
	col->addWidget(list, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	col->addWidget(buttons);
	dialog->show();
}

bool MainWindow::exportHeightmap(const QString &path, QString *error)
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return false;
	const MapEditorCore::GreyImage grey = MapEditorCore::heightsToImage(*m_state.map->map().heightMap);
	QImage image(grey.width, grey.height, QImage::Format_Grayscale8);
	for (int y = 0; y < grey.height; ++y)
	{
		uchar *line = image.scanLine(y);
		for (int x = 0; x < grey.width; ++x)
			line[x] = static_cast<uchar>(grey.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(grey.width) + static_cast<std::size_t>(x)]);
	}
	if (!image.save(path))
	{
		*error = T("The file cannot be written: %1").arg(path);
		return false;
	}
	return true;
}

bool MainWindow::importHeightmap(const QString &path, int low, int high, QString *error)
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return false;
	QImage image(path);
	if (image.isNull())
	{
		*error = T("The picture cannot be read: %1").arg(path);
		return false;
	}
	// 16-bit pictures keep their finer steps until they are mapped to the height range.
	MapEditorCore::GreyImage grey;
	grey.width = image.width();
	grey.height = image.height();
	const bool deep = image.depth() >= 16 && image.format() != QImage::Format_RGB16 && image.format() != QImage::Format_ARGB8565_Premultiplied;
	grey.bits = deep ? 16 : 8;
	image = image.convertToFormat(deep ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8);
	grey.pixels.resize(static_cast<std::size_t>(grey.width) * static_cast<std::size_t>(grey.height));
	for (int y = 0; y < grey.height; ++y)
	{
		for (int x = 0; x < grey.width; ++x)
		{
			const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(grey.width) + static_cast<std::size_t>(x);
			grey.pixels[i] = deep ? reinterpret_cast<const quint16 *>(image.constScanLine(y))[x] : image.constScanLine(y)[x];
		}
	}
	auto command = MapEditorCore::importHeights(m_state.map->map(), grey, low, high);
	if (!command)
	{
		*error = T("The picture gives the same heights.");
		return false;
	}
	// The heights are already in the map; the step only records them.
	const MapEditorCore::Changes changes = command->changes();
	m_state.map->history.record(std::move(command));
	applyChanges(changes);
	return true;
}

void MainWindow::importHeightmapDialog()
{
	if (!m_state.map || !m_state.map->map().heightMap)
		return;
	const QString path = QFileDialog::getOpenFileName(this, T("Import height map"), QString(), T("Pictures (*.png *.bmp *.tga *.tif *.tiff *.jpg)"));
	if (path.isEmpty())
		return;
	QDialog ask(this);
	ask.setWindowTitle(T("Import height map"));
	auto *form = new QFormLayout(&ask);
	auto *low = new QSpinBox(&ask);
	low->setRange(0, 255);
	low->setValue(0);
	auto *high = new QSpinBox(&ask);
	high->setRange(0, 255);
	high->setValue(255);
	form->addRow(T("Black becomes height"), low);
	form->addRow(T("White becomes height"), high);
	auto *hint = label(T("The picture is scaled to the map (north up, as the 2D overview shows it); one height step is 0.625 world units."), "Hint", &ask);
	hint->setWordWrap(true);
	form->addRow(hint);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
	connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
	form->addRow(buttons);
	if (ask.exec() != QDialog::Accepted)
		return;
	QString error;
	if (!importHeightmap(path, low->value(), high->value(), &error))
		toast(error);
}

} // namespace MapEditorApp
