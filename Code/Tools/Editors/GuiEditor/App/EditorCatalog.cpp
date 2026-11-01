/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorCatalog.cpp -- the main window of the GUI editor: what the game data offer (images, header
// templates, callbacks, the string table), the pickers of the properties panel, and the problems of names
// the game does not know.

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "App/GuiEngine.h"
#include "App/Picker.h"
#include "EditorCommon/Language.h"

#include <QPixmap>
#include <QTreeWidget>

#include <algorithm>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int KEY = Qt::UserRole;	// as in EditorProperties.cpp: what the row changes
constexpr int ICON = 48;			// pictures in the image picker

// The callback fields of a window and the kind of the game's tables they name.
const std::pair<const char *, const char *> CALLBACK_KINDS[] = {
	{"SYSTEMCALLBACK", "system"}, {"INPUTCALLBACK", "input"}, {"TOOLTIPCALLBACK", "tooltip"}, {"DRAWCALLBACK", "draw"}};
const std::pair<const char *, const char *> LAYOUT_KINDS[] = {
	{"LAYOUTINIT", "init"}, {"LAYOUTUPDATE", "update"}, {"LAYOUTSHUTDOWN", "shutdown"}};

bool none(const QString &value)
{
	return value.isEmpty() || value.compare(QLatin1String("[None]"), Qt::CaseInsensitive) == 0;
}

} // namespace

void EditorWindow::loadCatalog()
{
	GuiEngine &engine = GuiEngine::instance();
	Catalog c;
	for (const GuiEngine::ImageInfo &i : engine.images())
	{
		c.images << i.name;
		c.imageSizes.insert(i.name, QStringLiteral("%1 \u00d7 %2").arg(i.width).arg(i.height));
		c.imageSet.insert(i.name);
	}
	c.headerTemplates = engine.headerTemplates();
	for (const char *kind : {"system", "input", "tooltip", "draw", "init", "update", "shutdown"})
		c.callbacks.insert(QString::fromLatin1(kind), engine.callbacks(kind));
	c.textKeys = engine.textKeys();
	c.textSet = QSet<QString>(c.textKeys.begin(), c.textKeys.end());
	c.loaded = true;
	m_catalog = std::move(c);
	m_imageIcons.clear();
	m_texts.clear();
}

QString EditorWindow::gameText(const QString &key, bool *exists)
{
	if (const auto it = m_texts.constFind(key); it != m_texts.cend())
	{
		*exists = !it->isNull();
		return *it;
	}
	const QString text = GuiEngine::instance().text(key, exists);
	m_texts.insert(key, *exists ? (text.isNull() ? QStringLiteral("") : text) : QString());
	return text;
}

QIcon EditorWindow::imageIcon(const QString &name, int size)
{
	const QString key = name + QLatin1Char('@') + QString::number(size);
	if (const auto it = m_imageIcons.constFind(key); it != m_imageIcons.cend())
		return *it;
	const QImage image = GuiEngine::instance().imagePreview(name, size);
	const QIcon icon = image.isNull() ? QIcon() : QIcon(QPixmap::fromImage(image));
	m_imageIcons.insert(key, icon);
	return icon;
}

std::vector<WndModel::Problem> EditorWindow::catalogProblems() const
{
	std::vector<WndModel::Problem> out;
	if (!m_catalog.loaded)
		return out;
	const auto warn = [&out](const std::string &window, const QString &text) {
		WndModel::Problem p;
		p.severity = WndModel::Problem::Severity::Warning;
		p.window = window;
		p.text = toLatin1(text);
		out.push_back(std::move(p));
	};
	const WndModel::Layout &layout = m_doc.layout();
	for (const auto &[key, kind] : LAYOUT_KINDS)
	{
		const QString v = latin1(layout.layoutCallback(key));
		if (!none(v) && !m_catalog.callbacks.value(QString::fromLatin1(kind)).contains(v))
			warn({}, T("%1 %2 is not a function of the game").arg(QLatin1String(key), v));
	}
	WndModel::forEachWindow(layout, [&](const WndModel::Window &w, int) {
		const std::string name = w.name();
		for (const WndModel::Entry &e : w.entries)
		{
			if (e.kind != WndModel::Entry::Kind::Field || e.key.size() < 8 || e.key.compare(e.key.size() - 8, 8, "DRAWDATA") != 0)
				continue;
			for (const WndModel::DrawEntry &d : w.drawData(e.key))
			{
				const QString image = latin1(d.image);
				if (!m_catalog.imageSet.isEmpty() && image != QLatin1String("NoImage") && !m_catalog.imageSet.contains(image))
					warn(name, T("image %1 (%2) is not in the game data").arg(image, latin1(e.key)));
			}
		}
		for (const char *field : {"TEXT", "TOOLTIPTEXT"})
		{
			const QString key = latin1(w.callback(field));
			if (!key.isEmpty() && !m_catalog.textSet.isEmpty() && !m_catalog.textSet.contains(key))
				warn(name, T("text %1 is not in the string table; the game shows MISSING").arg(key));
		}
		for (const auto &[field, kind] : CALLBACK_KINDS)
		{
			const QString v = latin1(w.callback(field));
			if (!none(v) && !m_catalog.callbacks.value(QString::fromLatin1(kind)).contains(v))
				warn(name, T("%1 %2 is not a function of the game").arg(QLatin1String(field), v));
		}
		const QString header = latin1(w.callback("HEADERTEMPLATE"));
		if (!none(header) && !m_catalog.headerTemplates.contains(header))
			warn(name, T("header template %1 does not exist").arg(header));
	});
	return out;
}

bool EditorWindow::pickValue(QTreeWidgetItem *item)
{
	const QString key = item->data(1, KEY).toString();
	const QString current = item->text(1);
	QString value;
	std::vector<Picker::Item> items;
	if (key.startsWith(QLatin1String("draw:")) && key.endsWith(QLatin1String(":image")))
	{
		items.push_back({QStringLiteral("NoImage"), T("no picture")});
		for (const QString &name : m_catalog.images)
			items.push_back({name, m_catalog.imageSizes.value(name)});
		if (!Picker::pick(this, T("Image"), std::move(items), current, &value,
				[this](const QString &name) { return name == QLatin1String("NoImage") ? QIcon() : imageIcon(name, ICON); }, ICON))
			return true;
	}
	else if (key == QLatin1String("header"))
	{
		items.push_back({QStringLiteral("[None]"), T("none")});
		for (const QString &name : m_catalog.headerTemplates)
			items.push_back({name, QString()});
		if (!Picker::pick(this, T("Header template"), std::move(items), current, &value))
			return true;
	}
	else if (key.startsWith(QLatin1String("callback:")) || key.startsWith(QLatin1String("layout:")))
	{
		const QString field = key.mid(key.indexOf(QLatin1Char(':')) + 1);
		QString kind;
		for (const auto &[f, k] : CALLBACK_KINDS)
			if (field == QLatin1String(f))
				kind = QString::fromLatin1(k);
		for (const auto &[f, k] : LAYOUT_KINDS)
			if (field == QLatin1String(f))
				kind = QString::fromLatin1(k);
		items.push_back({QStringLiteral("[None]"), T("none")});
		for (const QString &name : m_catalog.callbacks.value(kind))
			items.push_back({name, QString()});
		if (!Picker::pick(this, T("Callback"), std::move(items), current, &value, {}, 0, true))
			return true;
	}
	else if (key == QLatin1String("font:name"))
	{
		QStringList fonts = {QStringLiteral("Arial"), QStringLiteral("Times New Roman"), QStringLiteral("Courier New"),
			QStringLiteral("Tahoma"), QStringLiteral("Verdana")};
		WndModel::forEachWindow(m_doc.layout(), [&fonts](const WndModel::Window &w, int) {
			if (const std::optional<WndModel::Font> f = w.font(); f && !fonts.contains(latin1(f->name)))
				fonts << latin1(f->name);
		});
		fonts.sort(Qt::CaseInsensitive);
		for (const QString &f : fonts)
			items.push_back({f, QString()});
		if (!Picker::pick(this, T("Font"), std::move(items), current, &value, {}, 0, true))
			return true;
	}
	else if (key == QLatin1String("text") || key == QLatin1String("tooltip"))
	{
		for (const QString &k : m_catalog.textKeys)
		{
			bool exists = false;
			items.push_back({k, gameText(k, &exists).simplified()});
		}
		if (!Picker::pick(this, T("Text from the string table"), std::move(items), current, &value, {}, 0, true))
			return true;
	}
	else
		return false;
	item->setText(1, value);
	return true;
}

} // namespace GuiEditorApp
