/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorSchemes.cpp -- the main window of the GUI editor: layout schemes (.ls) and the comparison of the
// layout with another one.

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/Widgets.h"

#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSaveFile>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int NAME = Qt::UserRole;	// the window a row of the comparison belongs to

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Schemes

bool EditorWindow::loadScheme(const QString &path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	const QByteArray text = file.readAll();
	std::optional<WndModel::Scheme> scheme = WndModel::readScheme(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
	if (!scheme)
		return false;
	m_scheme = std::move(scheme);
	m_schemePath = QFileInfo(path).absoluteFilePath();
	saveSettings();
	buildMenus();
	updateStatus();
	return true;
}

void EditorWindow::clearScheme()
{
	m_scheme.reset();
	m_schemePath.clear();
	saveSettings();
	buildMenus();
	updateStatus();
}

bool EditorWindow::saveLayoutScheme(const QString &path)
{
	const WndModel::Scheme scheme = WndModel::schemeOf(m_doc.layout(), m_scheme ? *m_scheme : WndModel::emptyScheme());
	const std::string text = WndModel::writeScheme(scheme);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(text.data(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())
		|| !file.commit())
		return false;
	m_scheme = scheme;
	m_schemePath = QFileInfo(path).absoluteFilePath();
	saveSettings();
	buildMenus();
	updateStatus();
	return true;
}

int EditorWindow::applyScheme(bool selectionOnly)
{
	if (!m_scheme)
		return 0;
	const std::vector<WndModel::WindowPath> paths = selectionOnly ? selectedPaths() : std::vector<WndModel::WindowPath>{};
	if (selectionOnly && paths.empty())
		return 0;
	int changedWindows = 0;
	edit(selectionOnly ? T("Apply scheme to the selection") : T("Apply scheme"), [&](WndModel::Layout &l) {
		if (selectionOnly)
		{
			for (const WndModel::WindowPath &p : paths)
				if (WndModel::Window *w = WndModel::windowAt(l, p))
					changedWindows += WndModel::applyScheme(*m_scheme, *w);
		}
		else
			for (WndModel::Window *w : l.windows())
				changedWindows += WndModel::applyScheme(*m_scheme, *w);
	});
	return changedWindows;
}

// ---------------------------------------------------------------------------------------------------------
// Comparing

int EditorWindow::compareWith(const QString &path)
{
	const LayoutSource source = path.isEmpty() ? m_source : sourceOf(path);
	const QByteArray text = readLayoutText(source);
	if (text.isNull())
	{
		m_error = T("%1 cannot be read.").arg(source.kind == LayoutSource::Kind::File ? QDir::toNativeSeparators(source.path) : source.path);
		updateStatus();
		return -1;
	}
	m_compare = WndModel::read(std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
	m_compareName = path.isEmpty() ? T("the saved version") : QFileInfo(source.path).fileName();
	fillCompare();
	m_compareDock->show();
	m_compareDock->raise();
	return static_cast<int>(m_differences.size());
}

void EditorWindow::stopCompare()
{
	m_compare.reset();
	m_differences.clear();
	fillCompare();
	m_compareDock->hide();
}

void EditorWindow::fillCompare()
{
	m_differences = m_compare ? WndModel::diffLayouts(*m_compare, m_doc.layout()) : std::vector<WndModel::WindowDiff>{};
	const QString title = m_compare ? T("Comparison (%1)").arg(m_differences.size()) : T("Comparison");
	setDockPage(m_compareDock, title, buildComparePage());
}

QWidget *EditorWindow::buildComparePage()
{
	auto *page = new QWidget;
	auto *box = new QVBoxLayout(page);
	box->setContentsMargins(0, 0, 0, 0);
	auto *top = new QHBoxLayout;
	top->setContentsMargins(6, 4, 6, 0);
	QString heading;
	if (!m_compare)
		heading = T("Compare the layout with another one or with its saved version (File menu).");
	else if (m_differences.empty())
		heading = T("No differences to %1.").arg(m_compareName);
	else
		heading = T("Compared with %1: left its value, right the value of this layout.").arg(m_compareName);
	auto *text = new QLabel(heading);
	text->setObjectName(QStringLiteral("CompareHeading"));
	text->setWordWrap(true);
	top->addWidget(text, 1);
	if (m_compare)
	{
		auto *stop = new QPushButton(T("Stop comparing"));
		stop->setObjectName(QStringLiteral("CompareStop"));
		connect(stop, &QPushButton::clicked, this, [this] { stopCompare(); });
		top->addWidget(stop);
	}
	box->addLayout(top);

	auto *tree = new QTreeWidget;
	tree->setObjectName(QStringLiteral("CompareTree"));
	tree->setColumnCount(3);
	tree->setHeaderLabels({T("Window / field"), m_compare ? m_compareName : T("Other"), T("This layout")});
	tree->header()->setSectionResizeMode(QHeaderView::Interactive);
	tree->setColumnWidth(0, 260);
	tree->setColumnWidth(1, 320);
	const Tokens t = tokens(m_theme, m_game);
	for (const WndModel::WindowDiff &d : m_differences)
	{
		const QString name = latin1(d.name);
		auto *item = new QTreeWidgetItem(tree);
		item->setData(0, NAME, name);
		switch (d.kind)
		{
		case WndModel::WindowDiff::Kind::Added:
			item->setText(0, T("%1 (only here)").arg(name));
			item->setForeground(0, t.accFg);
			break;
		case WndModel::WindowDiff::Kind::Removed:
			item->setText(0, T("%1 (only there)").arg(name));
			item->setForeground(0, t.error);
			break;
		case WndModel::WindowDiff::Kind::Changed:
			item->setText(0, name.isEmpty() ? T("Layout block") : name);
			break;
		}
		for (const WndModel::FieldDiff &f : d.fields)
		{
			auto *row = new QTreeWidgetItem(item);
			row->setData(0, NAME, name);
			row->setText(0, f.key == "parent" ? T("parent window") : latin1(f.key));
			const auto shown = [&f](const std::string &v) {
				if (!v.empty())
					return oneLine(v);
				return f.key == "parent" ? T("top level") : T("(none)");
			};
			row->setText(1, shown(f.before));
			row->setText(2, shown(f.after));
			row->setToolTip(1, latin1(f.before));
			row->setToolTip(2, latin1(f.after));
		}
	}
	tree->expandAll();
	connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
		const QString window = item->data(0, NAME).toString();
		if (window.isEmpty())
			return;
		for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
			if (it.key() == window || it.key().endsWith(QLatin1Char(':') + window))
			{
				selectWindow(it.key());
				return;
			}
	});
	box->addWidget(tree, 1);
	return page;
}

} // namespace GuiEditorApp
