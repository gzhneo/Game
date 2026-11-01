/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorProperties.cpp -- the properties panel of the GUI editor: the values of the selected window (or of
// the layout) with the ones that can change editable. A value is checked before it goes into the file;
// what the game's reader would not take back is refused.

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "EditorCommon/Language.h"

#include <QColorDialog>
#include <QHeaderView>
#include <QStatusBar>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <algorithm>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int KEY = Qt::UserRole;	// in column 1: what the row changes ("name", "status:HIDDEN", ...)

// A value inside quotes or a field: the reader ends it at a quote or a ';'.
QString clean(const QString &value)
{
	QString v = value;
	v.remove(QLatin1Char('"'));
	v.remove(QLatin1Char(';'));
	return v.trimmed();
}

bool parseColor(const QString &text, WndModel::Color *c)
{
	const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
	if (parts.size() != 4)
		return false;
	int v[4] = {};
	for (int i = 0; i < 4; ++i)
	{
		bool ok = false;
		v[i] = parts[i].toInt(&ok);
		if (!ok || v[i] < 0 || v[i] > 255)
			return false;
	}
	*c = {v[0], v[1], v[2], v[3]};
	return true;
}

bool validName(const QString &name)
{
	if (name.isEmpty())
		return false;
	for (const QChar c : name)
		if (c.isSpace() || c == QLatin1Char('"') || c == QLatin1Char(':') || c == QLatin1Char(';') || c.unicode() > 126)
			return false;
	return true;
}

} // namespace

QWidget *EditorWindow::buildPropertiesPage()
{
	m_properties = new QTreeWidget;
	m_properties->setObjectName(QStringLiteral("Properties"));
	m_properties->setColumnCount(2);
	m_properties->setHeaderLabels({T("Property"), T("Value")});
	m_properties->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	m_properties->setRootIsDecorated(true);
	m_properties->setSelectionMode(QAbstractItemView::SingleSelection);
	m_properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_properties->setToolTip(T("Double-click a value to change it; colours open a colour picker."));
	connect(m_properties, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
		const QString key = item->data(1, KEY).toString();
		if (key.isEmpty() || key.startsWith(QLatin1String("status:")) || key == QLatin1String("font:bold"))
			return;
		if (key.endsWith(QLatin1String(":color")) || key.endsWith(QLatin1String(":border")) || key.startsWith(QLatin1String("datacolor:")))
			pickColor(item);
		else if (pickValue(item))
			return;
		else
			m_properties->editItem(item, 1);
	});
	connect(m_properties, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
		if (!m_filling && (column == 1 || column == 0))
			applyProperty(item);
	});
	return m_properties;
}

void EditorWindow::fillProperties()
{
	if (!m_properties)
		return;
	m_filling = true;
	const QSignalBlocker block(m_properties);
	m_properties->clear();
	const auto row = [this](QTreeWidgetItem *parent, const QString &label, const QString &value, const QString &key = QString()) {
		auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_properties);
		item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
		item->setText(0, label);
		item->setText(1, value);
		item->setToolTip(1, value);
		if (!key.isEmpty())
		{
			item->setData(1, KEY, key);
			item->setFlags(item->flags() | Qt::ItemIsEditable);
		}
		return item;
	};
	const auto check = [&](QTreeWidgetItem *parent, const QString &label, bool on, const QString &key) {
		QTreeWidgetItem *item = row(parent, label, QString(), key);
		item->setFlags((item->flags() & ~Qt::ItemIsEditable) | Qt::ItemIsUserCheckable);
		item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
		return item;
	};
	const WndModel::Layout &layout = m_doc.layout();
	const QString primary = selectedWindow();
	const std::optional<WndModel::WindowPath> path = primary.isEmpty() ? std::nullopt : WndModel::findWindow(layout, toLatin1(primary));
	const WndModel::Window *window = path ? WndModel::windowAt(layout, *path) : nullptr;
	if (!window)
	{
		QTreeWidgetItem *file = row(nullptr, T("Layout"), m_fileName);
		row(file, T("File version"), QString::number(layout.version()));
		int count = 0;
		WndModel::forEachWindow(layout, [&](const WndModel::Window &, int) { ++count; });
		row(file, T("Windows"), QString::number(count));
		row(file, T("Init"), latin1(layout.layoutCallback("LAYOUTINIT")), QStringLiteral("layout:LAYOUTINIT"));
		row(file, T("Update"), latin1(layout.layoutCallback("LAYOUTUPDATE")), QStringLiteral("layout:LAYOUTUPDATE"));
		row(file, T("Shutdown"), latin1(layout.layoutCallback("LAYOUTSHUTDOWN")), QStringLiteral("layout:LAYOUTSHUTDOWN"));
		m_properties->expandAll();
		m_filling = false;
		return;
	}

	QTreeWidgetItem *general = row(nullptr, T("Window"), m_selection.size() > 1 ? T("%1 selected, showing the last").arg(m_selection.size()) : QString());
	row(general, T("Type"), latin1(window->type()));
	row(general, T("Name"), latin1(window->name()), QStringLiteral("name"));
	if (const std::optional<WndModel::Rect> r = window->rect())
	{
		row(general, T("Left"), QString::number(r->left), QStringLiteral("left"));
		row(general, T("Top"), QString::number(r->top), QStringLiteral("top"));
		row(general, T("Width"), QString::number(r->right - r->left), QStringLiteral("width"));
		row(general, T("Height"), QString::number(r->bottom - r->top), QStringLiteral("height"));
		row(general, T("Made for"), QStringLiteral("%1 \u00d7 %2").arg(r->creationWidth).arg(r->creationHeight));
	}
	// Text keys with what the game shows for them.
	const auto shown = [&](QTreeWidgetItem *parent, const QString &key) {
		if (key.isEmpty())
			return;
		bool exists = false;
		const QString text = gameText(key, &exists);
		row(parent, T("Shown"), exists ? text : T("(not in the string table: the game shows MISSING)"));
	};
	shown(row(general, T("Text"), latin1(window->text()), QStringLiteral("text")), latin1(window->text()));
	shown(row(general, T("Tooltip"), latin1(window->callback("TOOLTIPTEXT")), QStringLiteral("tooltip")), latin1(window->callback("TOOLTIPTEXT")));
	row(general, T("Tooltip delay"), latin1(window->value("TOOLTIPDELAY")), QStringLiteral("tooltipdelay"));
	row(general, T("Header template"), latin1(window->callback("HEADERTEMPLATE")), QStringLiteral("header"));
	row(general, T("Style"), oneLine(window->value("STYLE")));

	QStringList status;
	for (const std::string &word : window->status())
		status << latin1(word);
	QTreeWidgetItem *statusGroup = row(nullptr, T("Status"), status.join(QStringLiteral(", ")));
	for (const std::string &word : WndModel::statusWords())
		check(statusGroup, latin1(word), window->hasStatus(word), QStringLiteral("status:") + latin1(word));

	if (const std::optional<WndModel::Font> font = window->font())
	{
		QTreeWidgetItem *fontGroup = row(nullptr, T("Font"), QStringLiteral("%1, %2").arg(latin1(font->name)).arg(font->size));
		row(fontGroup, T("Name"), latin1(font->name), QStringLiteral("font:name"));
		row(fontGroup, T("Size"), QString::number(font->size), QStringLiteral("font:size"));
		check(fontGroup, T("bold"), font->bold != 0, QStringLiteral("font:bold"));
	}

	QTreeWidgetItem *callbacks = row(nullptr, T("Callbacks"), QString());
	const std::pair<const char *, QString> callbackKeys[] = {
		{"SYSTEMCALLBACK", T("System")}, {"INPUTCALLBACK", T("Input")}, {"TOOLTIPCALLBACK", T("Tooltip")}, {"DRAWCALLBACK", T("Draw")}};
	for (const auto &[k, text] : callbackKeys)
		row(callbacks, text, latin1(window->callback(k)), QStringLiteral("callback:") + QLatin1String(k));

	// Text colours and the data of the gadget, value by value.
	const auto dataGroup = [&](const std::string &field, const QString &title, bool colors) {
		const std::vector<std::pair<std::string, std::string>> pairs = window->dataFields(field);
		if (pairs.empty())
			return;
		QTreeWidgetItem *group = row(nullptr, title, QString());
		for (std::size_t i = 0; i < pairs.size(); ++i)
		{
			const QString k = QStringLiteral("%1:%2:%3").arg(colors ? QStringLiteral("datacolor") : QStringLiteral("data"), latin1(field)).arg(i);
			QTreeWidgetItem *item = row(group, latin1(pairs[i].first), latin1(pairs[i].second), k);
			WndModel::Color c;
			if (colors && parseColor(latin1(pairs[i].second), &c))
				item->setIcon(1, swatch(c));
		}
		m_properties->expandItem(group);
	};
	dataGroup("TEXTCOLOR", T("Text colours"), true);
	for (const WndModel::Entry &entry : window->entries)
		if (entry.kind == WndModel::Entry::Kind::Field && entry.key.size() > 4 && entry.key.compare(entry.key.size() - 4, 4, "DATA") == 0
			&& (entry.key.size() < 8 || entry.key.compare(entry.key.size() - 8, 8, "DRAWDATA") != 0))
			dataGroup(entry.key, T("Gadget: %1").arg(latin1(entry.key)), false);

	// Every draw data field: the three of the window first, then the parts of the gadget.
	std::vector<std::pair<std::string, QString>> drawKeys = {
		{"ENABLEDDRAWDATA", T("Enabled")}, {"DISABLEDDRAWDATA", T("Disabled")}, {"HILITEDRAWDATA", T("Highlighted")}};
	for (const WndModel::Entry &entry : window->entries)
		if (entry.kind == WndModel::Entry::Kind::Field && entry.key.size() > 8 && entry.key.compare(entry.key.size() - 8, 8, "DRAWDATA") == 0
			&& entry.key != "ENABLEDDRAWDATA" && entry.key != "DISABLEDDRAWDATA" && entry.key != "HILITEDRAWDATA")
			drawKeys.emplace_back(entry.key, latin1(entry.key));
	for (const auto &[k, text] : drawKeys)
	{
		const std::vector<WndModel::DrawEntry> entries = window->drawData(k);
		if (entries.empty())
			continue;
		QTreeWidgetItem *group = row(nullptr, T("Look: %1").arg(text), QString());
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			const WndModel::DrawEntry &e = entries[i];
			const QString base = QStringLiteral("draw:%1:%2").arg(latin1(k)).arg(i);
			QTreeWidgetItem *item = row(group, QString::number(i), latin1(e.image), base + QStringLiteral(":image"));
			const QIcon picture = e.image == "NoImage" ? QIcon() : imageIcon(latin1(e.image), 16);
			item->setIcon(1, picture.isNull() ? swatch(e.color) : picture);
			row(item, T("Colour"), colorText(e.color), base + QStringLiteral(":color"))->setIcon(1, swatch(e.color));
			row(item, T("Border colour"), colorText(e.border), base + QStringLiteral(":border"))->setIcon(1, swatch(e.border));
		}
	}

	QTreeWidgetItem *fields = row(nullptr, T("All fields"), QString());
	for (const WndModel::Entry &entry : window->entries)
		if (entry.kind == WndModel::Entry::Kind::Field && entry.key != "WINDOWTYPE" && entry.key != "NAME")
			row(fields, latin1(entry.key), oneLine(entry.value()), QStringLiteral("field:") + latin1(entry.key));

	m_properties->expandItem(general);
	m_properties->expandItem(callbacks);
	m_filling = false;
}

void EditorWindow::applyProperty(QTreeWidgetItem *item)
{
	const QString key = item->data(1, KEY).toString();
	if (key.isEmpty())
		return;
	const QString value = item->text(1);
	const bool checked = item->checkState(0) == Qt::Checked;
	const QString primary = selectedWindow();
	// The panel is filled again after the change: not while Qt is still inside this item's signal.
	QTimer::singleShot(0, this, [this, key, value, checked, primary] {
		const auto refused = [this](const QString &why) {
			statusBar()->showMessage(why, 5000);
			fillProperties();
		};
		if (key.startsWith(QLatin1String("layout:")))
		{
			const std::string k = toLatin1(key.mid(7));
			const QString v = clean(value).isEmpty() ? QStringLiteral("[None]") : clean(value);
			edit(T("Layout callback"), [&](WndModel::Layout &l) { WndModel::setLayoutValue(l, k, toLatin1(v)); });
			return;
		}
		const std::optional<WndModel::WindowPath> path = WndModel::findWindow(m_doc.layout(), toLatin1(primary));
		if (!path)
			return;
		const WndModel::WindowPath at = *path;
		// With several windows of one type selected, a value goes to all of them; name and place stay per
		// window (Arrange gives several windows one size or edge).
		std::vector<WndModel::WindowPath> targets{at};
		const bool perWindow = key == QLatin1String("name") || key == QLatin1String("left") || key == QLatin1String("top")
			|| key == QLatin1String("width") || key == QLatin1String("height");
		if (!perWindow)
			if (const WndModel::Window *p = WndModel::windowAt(m_doc.layout(), at))
				for (const QString &other : m_selection)
					if (other != primary)
						if (const std::optional<WndModel::WindowPath> o = WndModel::findWindow(m_doc.layout(), toLatin1(other)))
							if (const WndModel::Window *w = WndModel::windowAt(m_doc.layout(), *o); w && w->type() == p->type())
								targets.push_back(*o);
		const auto change = [&](const QString &description, const std::function<void(WndModel::Window &)> &f) {
			if (!edit(description, [&](WndModel::Layout &l) {
					for (const WndModel::WindowPath &target : targets)
						if (WndModel::Window *w = WndModel::windowAt(l, target))
							f(*w);
				}))
				fillProperties();
		};
		const WndModel::Window *now = WndModel::windowAt(m_doc.layout(), at);
		if (key == QLatin1String("name"))
		{
			const QString name = value.trimmed();
			const QString full = primary.left(primary.indexOf(QLatin1Char(':')) + 1) + name;
			const std::string file = toLatin1(primary.left(std::max<qsizetype>(0, primary.indexOf(QLatin1Char(':')))));
			if (!validName(name))
				return refused(T("A name needs letters, digits or _ and no spaces, quotes or colons."));
			if (full != primary && WndModel::findWindow(m_doc.layout(), toLatin1(full)))
				return refused(T("Another window is called %1.").arg(name));
			if (full.size() >= WndModel::MAX_NAME_LENGTH)
				return refused(T("The game keeps only %1 characters of a name with its file.").arg(WndModel::MAX_NAME_LENGTH - 1));
			change(T("Rename"), [&](WndModel::Window &w) { w.setName(file, toLatin1(name)); });
			m_selection.removeAll(primary);
			m_selection << full;
			changed();
			return;
		}
		if (key == QLatin1String("left") || key == QLatin1String("top") || key == QLatin1String("width") || key == QLatin1String("height"))
		{
			bool ok = false;
			const int v = value.trimmed().toInt(&ok);
			std::optional<WndModel::Rect> r = now ? now->rect() : std::nullopt;
			if (!ok || !r || ((key == QLatin1String("width") || key == QLatin1String("height")) && v < 1))
				return refused(T("Enter a whole number."));
			if (key == QLatin1String("left") || key == QLatin1String("top"))
			{
				// Moving keeps the children where they are inside the window.
				const int dx = key == QLatin1String("left") ? v - r->left : 0;
				const int dy = key == QLatin1String("top") ? v - r->top : 0;
				return change(T("Move"), [&](WndModel::Window &w) { WndModel::offsetWindow(w, dx, dy); });
			}
			if (key == QLatin1String("width"))
				r->right = r->left + v;
			else
				r->bottom = r->top + v;
			return change(T("Resize"), [&](WndModel::Window &w) { w.setRect(*r); });
		}
		if (key.startsWith(QLatin1String("status:")))
		{
			const std::string word = toLatin1(key.mid(7));
			return change(T("Status"), [&](WndModel::Window &w) {
				std::vector<std::string> words = w.status();
				words.erase(std::remove(words.begin(), words.end(), word), words.end());
				if (checked)
					words.push_back(word);
				w.setStatus(words);
			});
		}
		if (key == QLatin1String("text") || key == QLatin1String("tooltip"))
		{
			const char *field = key == QLatin1String("text") ? "TEXT" : "TOOLTIPTEXT";
			const QString v = clean(value);
			return change(key == QLatin1String("text") ? T("Text") : T("Tooltip"), [&](WndModel::Window &w) {
				if (v.isEmpty())
					w.removeField(field);
				else
					w.setValue(field, "\"" + toLatin1(v) + "\"");
			});
		}
		if (key == QLatin1String("header") || key.startsWith(QLatin1String("callback:")))
		{
			const std::string field = key == QLatin1String("header") ? std::string("HEADERTEMPLATE") : toLatin1(key.mid(9));
			const QString v = clean(value).isEmpty() ? QStringLiteral("[None]") : clean(value);
			return change(key == QLatin1String("header") ? T("Header template") : T("Callback"),
				[&](WndModel::Window &w) { w.setValue(field, "\"" + toLatin1(v) + "\""); });
		}
		if (key == QLatin1String("tooltipdelay"))
		{
			bool ok = false;
			const int v = value.trimmed().toInt(&ok);
			if (!ok)
				return refused(T("Enter a whole number."));
			return change(T("Tooltip delay"), [&](WndModel::Window &w) { w.setValue("TOOLTIPDELAY", std::to_string(v)); });
		}
		if (key.startsWith(QLatin1String("font:")))
		{
			std::optional<WndModel::Font> f = now ? now->font() : std::nullopt;
			if (!f)
				return;
			if (key == QLatin1String("font:name"))
			{
				if (clean(value).isEmpty())
					return refused(T("A font needs a name."));
				f->name = toLatin1(clean(value));
			}
			else if (key == QLatin1String("font:size"))
			{
				bool ok = false;
				f->size = value.trimmed().toInt(&ok);
				if (!ok || f->size < 1)
					return refused(T("Enter a whole number."));
			}
			else
				f->bold = checked ? 1 : 0;
			const WndModel::Font changedFont = *f;
			// Each window keeps the parts of its font that did not change.
			return change(T("Font"), [&](WndModel::Window &w) {
				WndModel::Font font = w.font().value_or(changedFont);
				if (key == QLatin1String("font:name"))
					font.name = changedFont.name;
				else if (key == QLatin1String("font:size"))
					font.size = changedFont.size;
				else
					font.bold = changedFont.bold;
				w.setValue("FONT", "NAME: \"" + font.name + "\", SIZE: " + std::to_string(font.size) + ", BOLD: " + std::to_string(font.bold));
			});
		}
		if (key.startsWith(QLatin1String("draw:")))
		{
			// draw:KEY:index:part
			const QStringList parts = key.split(QLatin1Char(':'));
			if (parts.size() != 4 || !now)
				return;
			const std::string field = toLatin1(parts[1]);
			const std::size_t index = static_cast<std::size_t>(parts[2].toInt());
			if (index >= now->drawData(field).size())
				return;
			const bool image = parts[3] == QLatin1String("image");
			const bool color = parts[3] == QLatin1String("color");
			std::string imageName;
			WndModel::Color c;
			if (image)
			{
				const QString v = clean(value).remove(QLatin1Char(' ')).remove(QLatin1Char(','));
				imageName = v.isEmpty() ? std::string("NoImage") : toLatin1(v);
			}
			else if (!parseColor(value, &c))
				return refused(T("A colour is four numbers from 0 to 255: red, green, blue, alpha."));
			// Each window keeps its other entries; only this one part changes.
			return change(T("Look"), [&](WndModel::Window &w) {
				std::vector<WndModel::DrawEntry> entries = w.drawData(field);
				if (index >= entries.size())
					return;
				if (image)
					entries[index].image = imageName;
				else
					(color ? entries[index].color : entries[index].border) = c;
				w.setDrawData(field, entries);
			});
		}
		if (key.startsWith(QLatin1String("data:")) || key.startsWith(QLatin1String("datacolor:")))
		{
			// data:FIELD:index or datacolor:FIELD:index
			const QStringList parts = key.split(QLatin1Char(':'));
			if (parts.size() != 3 || !now)
				return;
			const std::string field = toLatin1(parts[1]);
			const std::size_t index = static_cast<std::size_t>(parts[2].toInt());
			if (index >= now->dataFields(field).size())
				return;
			std::string newValue;
			if (parts[0] == QLatin1String("datacolor"))
			{
				WndModel::Color c;
				if (!parseColor(value, &c))
					return refused(T("A colour is four numbers from 0 to 255: red, green, blue, alpha."));
				newValue = toLatin1(colorText(c));
			}
			else
			{
				const QString v = value.simplified();
				if (v.isEmpty() || v.contains(QLatin1Char(';')) || v.contains(QLatin1Char(':')) || v.contains(QLatin1Char('"')))
					return refused(T("A value without ; : or quotes is needed."));
				newValue = toLatin1(v);
			}
			// Each window keeps its other values; only this one changes.
			return change(T("Field %1").arg(parts[1]), [&](WndModel::Window &w) {
				std::vector<std::pair<std::string, std::string>> pairs = w.dataFields(field);
				if (index >= pairs.size())
					return;
				pairs[index].second = newValue;
				w.setDataFields(field, pairs);
			});
		}
		if (key.startsWith(QLatin1String("field:")))
		{
			const std::string field = toLatin1(key.mid(6));
			QString v = value;
			v.remove(QLatin1Char(';'));
			if (v.trimmed().isEmpty())
				return refused(T("A field needs a value."));
			return change(T("Field %1").arg(key.mid(6)), [&](WndModel::Window &w) { w.setValue(field, toLatin1(v.trimmed())); });
		}
	});
}

void EditorWindow::pickColor(QTreeWidgetItem *item)
{
	WndModel::Color c;
	if (!parseColor(item->text(1), &c))
		c = {255, 255, 255, 255};
	const QColor chosen = QColorDialog::getColor(QColor(c.r, c.g, c.b, c.a), this, T("Colour"), QColorDialog::ShowAlphaChannel);
	if (!chosen.isValid())
		return;
	item->setText(1, QStringLiteral("%1 %2 %3 %4").arg(chosen.red()).arg(chosen.green()).arg(chosen.blue()).arg(chosen.alpha()));
}

bool EditorWindow::editProperty(const QString &key, const QString &value)
{
	if (!m_properties)
		return false;
	for (QTreeWidgetItemIterator it(m_properties); *it; ++it)
		if ((*it)->data(1, KEY).toString() == key)
		{
			if ((*it)->flags() & Qt::ItemIsUserCheckable)
				(*it)->setCheckState(0, value == QLatin1String("1") ? Qt::Checked : Qt::Unchecked);
			else
				(*it)->setText(1, value);
			return true;
		}
	return false;
}

int EditorWindow::propertyCount() const
{
	int n = 0;
	if (!m_properties)
		return n;
	for (QTreeWidgetItemIterator it(m_properties); *it; ++it)
		++n;
	return n;
}

} // namespace GuiEditorApp
