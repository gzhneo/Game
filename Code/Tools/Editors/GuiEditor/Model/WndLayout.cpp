/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndLayout.cpp -- reading and writing window layouts (.wnd) without the engine.

#include "Model/WndLayout.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace WndModel
{

namespace
{

bool isSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::string trim(std::string_view s)
{
	std::size_t b = 0;
	std::size_t e = s.size();
	while (b < e && isSpace(s[b]))
		++b;
	while (e > b && isSpace(s[e - 1]))
		--e;
	return std::string(s.substr(b, e - b));
}

class Reader
{
public:
	explicit Reader(std::string_view text) : m_text(text) {}

	[[nodiscard]] bool atEnd() const { return m_pos >= m_text.size(); }

	std::string trivia()
	{
		const std::size_t start = m_pos;
		while (m_pos < m_text.size() && isSpace(m_text[m_pos]))
			++m_pos;
		return std::string(m_text.substr(start, m_pos - start));
	}

	// The next word without moving on.
	[[nodiscard]] std::string_view peekWord() const
	{
		std::size_t end = m_pos;
		while (end < m_text.size() && !isSpace(m_text[end]))
			++end;
		return m_text.substr(m_pos, end - m_pos);
	}

	std::string word()
	{
		const std::string_view w = peekWord();
		m_pos += w.size();
		return std::string(w);
	}

	// The statement from the current word to the first ';' behind it (or to the end of the text).
	std::string statement()
	{
		const std::size_t start = m_pos;
		m_pos += peekWord().size();
		const std::size_t semicolon = m_text.find(';', m_pos);
		m_pos = semicolon == std::string_view::npos ? m_text.size() : semicolon + 1;
		return std::string(m_text.substr(start, m_pos - start));
	}

private:
	std::string_view m_text;
	std::size_t m_pos = 0;
};

Entry makeWord(std::string trivia, std::string word)
{
	Entry e;
	e.kind = Entry::Kind::Word;
	e.trivia = std::move(trivia);
	e.key = word;
	e.text = std::move(word);
	return e;
}

Entry makeField(std::string trivia, std::string text)
{
	Entry e;
	e.kind = Entry::Kind::Field;
	e.trivia = std::move(trivia);
	e.text = std::move(text);
	std::size_t end = 0;
	while (end < e.text.size() && !isSpace(e.text[end]))
		++end;
	e.key = e.text.substr(0, end);
	return e;
}

Window readWindow(Reader &r, std::string openWord);

Entry makeWindowEntry(std::string trivia, Window window)
{
	Entry e;
	e.kind = Entry::Kind::Window;
	e.trivia = std::move(trivia);
	e.key = "WINDOW";
	e.window.push_back(std::move(window));
	return e;
}

Window readWindow(Reader &r, std::string openWord)
{
	Window w;
	w.opening = std::move(openWord);
	for (;;)
	{
		std::string trivia = r.trivia();
		if (r.atEnd())
		{
			// The file ended inside the window; its whitespace stays with the window.
			w.endTrivia = std::move(trivia);
			w.ended = false;
			return w;
		}
		const std::string_view next = r.peekWord();
		if (next == "END")
		{
			w.endTrivia = std::move(trivia);
			r.word();
			return w;
		}
		if (next == "WINDOW")
		{
			std::string open = r.word();
			w.entries.push_back(makeWindowEntry(std::move(trivia), readWindow(r, std::move(open))));
		}
		else if (next == "CHILD" || next == "ENDALLCHILDREN")
			w.entries.push_back(makeWord(std::move(trivia), r.word()));
		else
			w.entries.push_back(makeField(std::move(trivia), r.statement()));
	}
}

void writeEntries(const std::vector<Entry> &entries, std::string &out);

void writeWindow(const Window &w, std::string &out)
{
	out += w.opening;
	writeEntries(w.entries, out);
	out += w.endTrivia;
	if (w.ended)
		out += "END";
}

void writeEntries(const std::vector<Entry> &entries, std::string &out)
{
	for (const Entry &e : entries)
	{
		out += e.trivia;
		if (e.kind == Entry::Kind::Window)
		{
			if (!e.window.empty())
				writeWindow(e.window.front(), out);
		}
		else
			out += e.text;
	}
}

// The newline the layout uses ("\r\n" for files from Windows).
std::string newlineOf(const std::vector<Entry> &entries)
{
	for (const Entry &e : entries)
		if (e.trivia.find('\n') != std::string::npos)
			return e.trivia.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	return "\r\n";
}

// The indent of the window's fields: the whitespace after the last newline in front of its first field.
std::string indentOf(const Window &w)
{
	for (const Entry &e : w.entries)
		if (e.kind == Entry::Kind::Field)
		{
			const std::size_t nl = e.trivia.rfind('\n');
			return nl == std::string::npos ? std::string("  ") : e.trivia.substr(nl + 1);
		}
	return "  ";
}

std::string withNewlines(std::string_view value, const std::string &newline)
{
	std::string out;
	for (const char c : value)
	{
		if (c == '\n')
			out += newline;
		else
			out += c;
	}
	return out;
}

std::string unquote(const std::string &value)
{
	const std::size_t open = value.find('"');
	if (open == std::string::npos)
		return value;
	const std::size_t close = value.find('"', open + 1);
	return value.substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1);
}

// The integers of a text in order (labels and punctuation in between are skipped).
std::vector<int> numbers(std::string_view text)
{
	std::vector<int> out;
	std::size_t i = 0;
	while (i < text.size())
	{
		const bool sign = (text[i] == '-' || text[i] == '+') && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1]));
		if (std::isdigit(static_cast<unsigned char>(text[i])) || sign)
		{
			std::size_t end = i + 1;
			while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end])))
				++end;
			out.push_back(std::atoi(std::string(text.substr(i, end - i)).c_str()));
			i = end;
		}
		else
			++i;
	}
	return out;
}

std::string colorText(const Color &c)
{
	return std::to_string(c.r) + ' ' + std::to_string(c.g) + ' ' + std::to_string(c.b) + ' ' + std::to_string(c.a);
}

const std::vector<std::string> GADGET_TYPES = {"PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER",
	"SCROLLLISTBOX", "ENTRYFIELD", "STATICTEXT", "PROGRESSBAR", "TABCONTROL", "COMBOBOX"};

} // namespace

std::string Entry::value() const
{
	if (kind != Kind::Field)
		return {};
	std::size_t pos = key.size();
	while (pos < text.size() && isSpace(text[pos]))
		++pos;
	if (pos < text.size() && text[pos] == '=')
		++pos;
	std::size_t end = text.size();
	if (end > pos && text[end - 1] == ';')
		--end;
	return trim(std::string_view(text).substr(pos, end - pos));
}

const Entry *Window::field(std::string_view key) const
{
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Field && e.key == key)
			return &e;
	return nullptr;
}

Entry *Window::field(std::string_view key)
{
	for (Entry &e : entries)
		if (e.kind == Entry::Kind::Field && e.key == key)
			return &e;
	return nullptr;
}

std::string Window::value(std::string_view key) const
{
	const Entry *e = field(key);
	return e ? e->value() : std::string();
}

void Window::setValue(std::string_view key, std::string_view value)
{
	const std::string newline = newlineOf(entries);
	const std::string text = std::string(key) + " = " + withNewlines(value, newline) + ";";
	if (Entry *e = field(key))
	{
		e->text = text;
		return;
	}
	// After the last field in front of the children (gadget data stays with the other fields).
	std::size_t at = 0;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		if (entries[i].kind != Entry::Kind::Field)
			break;
		at = i + 1;
	}
	entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(at), makeField(newline + indentOf(*this), text));
}

bool Window::removeField(std::string_view key)
{
	const auto it = std::find_if(entries.begin(), entries.end(),
		[key](const Entry &e) { return e.kind == Entry::Kind::Field && e.key == key; });
	if (it == entries.end())
		return false;
	entries.erase(it);
	return true;
}

std::vector<const Window *> Window::children() const
{
	std::vector<const Window *> out;
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Window && !e.window.empty())
			out.push_back(&e.window.front());
	return out;
}

std::vector<Window *> Window::children()
{
	std::vector<Window *> out;
	for (Entry &e : entries)
		if (e.kind == Entry::Kind::Window && !e.window.empty())
			out.push_back(&e.window.front());
	return out;
}

std::string Window::fullName() const
{
	return unquote(value("NAME"));
}

std::string Window::name() const
{
	const std::string full = fullName();
	const std::size_t colon = full.find(':');
	return colon == std::string::npos ? full : full.substr(colon + 1);
}

void Window::setName(std::string_view file, std::string_view newName)
{
	setValue("NAME", "\"" + std::string(file) + ":" + std::string(newName) + "\"");
}

std::optional<Rect> Window::rect() const
{
	const Entry *e = field("SCREENRECT");
	if (!e)
		return std::nullopt;
	const std::vector<int> n = numbers(e->value());
	if (n.size() < 4)
		return std::nullopt;
	Rect r;
	r.left = n[0];
	r.top = n[1];
	r.right = n[2];
	r.bottom = n[3];
	if (n.size() >= 6)
	{
		r.creationWidth = n[4];
		r.creationHeight = n[5];
	}
	return r;
}

void Window::setRect(const Rect &r)
{
	setValue("SCREENRECT", formatRect(r, static_cast<int>(indentOf(*this).size())));
}

std::vector<std::string> Window::status() const
{
	std::vector<std::string> out;
	const std::string v = value("STATUS");
	std::size_t start = 0;
	while (start <= v.size())
	{
		const std::size_t plus = v.find('+', start);
		const std::string word = trim(std::string_view(v).substr(start, plus == std::string::npos ? std::string::npos : plus - start));
		if (!word.empty() && word != "NONE" && word != "NULL")
			out.push_back(word);
		if (plus == std::string::npos)
			break;
		start = plus + 1;
	}
	return out;
}

void Window::setStatus(const std::vector<std::string> &words)
{
	std::string v;
	for (const std::string &w : words)
		v += (v.empty() ? "" : "+") + w;
	setValue("STATUS", v.empty() ? "NONE" : v);
}

bool Window::hasStatus(std::string_view word) const
{
	const std::vector<std::string> s = status();
	return std::find(s.begin(), s.end(), word) != s.end();
}

std::string Window::text() const
{
	return unquote(value("TEXT"));
}

std::string Window::callback(std::string_view key) const
{
	return unquote(value(key));
}

std::optional<Font> Window::font() const
{
	const Entry *e = field("FONT");
	if (!e)
		return std::nullopt;
	const std::string v = e->value();
	Font f;
	f.name = unquote(v);
	const std::size_t size = v.find("SIZE:");
	const std::size_t bold = v.find("BOLD:");
	if (size != std::string::npos)
		f.size = std::atoi(v.c_str() + size + 5);
	if (bold != std::string::npos)
		f.bold = std::atoi(v.c_str() + bold + 5);
	return f;
}

std::vector<DrawEntry> Window::drawData(std::string_view key) const
{
	std::vector<DrawEntry> out;
	const std::string v = value(key);
	std::size_t pos = 0;
	for (;;)
	{
		const std::size_t image = v.find("IMAGE:", pos);
		if (image == std::string::npos)
			break;
		DrawEntry d;
		std::size_t i = image + 6;
		while (i < v.size() && isSpace(v[i]))
			++i;
		std::size_t end = i;
		while (end < v.size() && v[end] != ',' && !isSpace(v[end]))
			++end;
		d.image = v.substr(i, end - i);
		const std::size_t next = v.find("IMAGE:", end);
		const std::string_view rest = std::string_view(v).substr(end, next == std::string::npos ? std::string::npos : next - end);
		const std::size_t border = rest.find("BORDERCOLOR:");
		const std::vector<int> color = numbers(rest.substr(0, border));
		const std::vector<int> borderColor = border == std::string_view::npos ? std::vector<int>{} : numbers(rest.substr(border));
		if (color.size() >= 4)
			d.color = {color[0], color[1], color[2], color[3]};
		if (borderColor.size() >= 4)
			d.border = {borderColor[0], borderColor[1], borderColor[2], borderColor[3]};
		out.push_back(d);
		pos = end;
	}
	return out;
}

void Window::setDrawData(std::string_view key, const std::vector<DrawEntry> &list)
{
	setValue(key, formatDrawData(key, list, static_cast<int>(indentOf(*this).size())));
}

std::vector<std::pair<std::string, std::string>> Window::dataFields(std::string_view key) const
{
	std::vector<std::pair<std::string, std::string>> out;
	const std::string v = value(key);
	std::size_t start = 0;
	while (start <= v.size())
	{
		const std::size_t comma = v.find(',', start);
		const std::string part = trim(std::string_view(v).substr(start, comma == std::string::npos ? std::string::npos : comma - start));
		const std::size_t colon = part.find(':');
		if (colon != std::string::npos)
			out.emplace_back(trim(std::string_view(part).substr(0, colon)), trim(std::string_view(part).substr(colon + 1)));
		else if (!out.empty())
			out.back().second += "," + part;
		if (comma == std::string::npos)
			break;
		start = comma + 1;
	}
	return out;
}

void Window::setDataFields(std::string_view key, const std::vector<std::pair<std::string, std::string>> &pairs)
{
	std::string v;
	for (const auto &[k, value] : pairs)
		v += (v.empty() ? "" : ", ") + k + ": " + value;
	setValue(key, v);
}

bool Window::isGadget() const
{
	return std::find(GADGET_TYPES.begin(), GADGET_TYPES.end(), type()) != GADGET_TYPES.end();
}

int Layout::version() const
{
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Field && e.key == "FILE_VERSION")
			return std::atoi(e.value().c_str());
	return 0;
}

std::string Layout::layoutCallback(std::string_view key) const
{
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Field && e.key == key)
			return e.value();
	return "[None]";
}

std::vector<const Window *> Layout::windows() const
{
	std::vector<const Window *> out;
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Window && !e.window.empty())
			out.push_back(&e.window.front());
	return out;
}

std::vector<Window *> Layout::windows()
{
	std::vector<Window *> out;
	for (Entry &e : entries)
		if (e.kind == Entry::Kind::Window && !e.window.empty())
			out.push_back(&e.window.front());
	return out;
}

Layout read(std::string_view text)
{
	Layout layout;
	Reader r(text);
	for (;;)
	{
		std::string trivia = r.trivia();
		if (r.atEnd())
		{
			layout.tail = std::move(trivia);
			return layout;
		}
		const std::string_view next = r.peekWord();
		if (next == "WINDOW")
		{
			std::string open = r.word();
			layout.entries.push_back(makeWindowEntry(std::move(trivia), readWindow(r, std::move(open))));
		}
		else if (next == "END" || next == "STARTLAYOUTBLOCK" || next == "ENDLAYOUTBLOCK" || next == "CHILD" || next == "ENDALLCHILDREN")
			layout.entries.push_back(makeWord(std::move(trivia), r.word()));
		else
			layout.entries.push_back(makeField(std::move(trivia), r.statement()));
	}
}

std::string write(const Layout &layout)
{
	std::string out;
	writeEntries(layout.entries, out);
	out += layout.tail;
	return out;
}

const std::vector<std::string> &statusWords()
{
	static const std::vector<std::string> words = {"ACTIVE", "TOGGLE", "DRAGABLE", "ENABLED", "HIDDEN", "ABOVE", "BELOW",
		"IMAGE", "TABSTOP", "NOINPUT", "NOFOCUS", "DESTROYED", "BORDER", "SMOOTH_TEXT", "ONE_LINE", "NO_FLUSH", "SEE_THRU",
		"RIGHT_CLICK", "WRAP_CENTERED", "CHECK_LIKE", "HOTKEY_TEXT", "USE_OVERLAY_STATES", "NOT_READY", "FLASHING",
		"ALWAYS_COLOR", "ON_MOUSE_DOWN"};
	return words;
}

const std::vector<std::string> &styleWords()
{
	static const std::vector<std::string> words = {"PUSHBUTTON", "RADIOBUTTON", "CHECKBOX", "VERTSLIDER", "HORZSLIDER",
		"SCROLLLISTBOX", "ENTRYFIELD", "STATICTEXT", "PROGRESSBAR", "USER", "MOUSETRACK", "ANIMATED", "TABSTOP",
		"TABCONTROL", "TABPANE", "COMBOBOX"};
	return words;
}

std::string formatRect(const Rect &r, int indent)
{
	// The labels of the continuation lines line up under UPPERLEFT, as in Save.cpp.
	const std::string pad(static_cast<std::size_t>(indent) + 13, ' ');
	return "UPPERLEFT: " + std::to_string(r.left) + ' ' + std::to_string(r.top) + ",\n" + pad + "BOTTOMRIGHT: "
		+ std::to_string(r.right) + ' ' + std::to_string(r.bottom) + ",\n" + pad + "CREATIONRESOLUTION: "
		+ std::to_string(r.creationWidth) + ' ' + std::to_string(r.creationHeight);
}

std::string formatDrawData(std::string_view key, const std::vector<DrawEntry> &list, int indent)
{
	const std::string pad(static_cast<std::size_t>(indent) + key.size() + 3, ' ');
	std::string out;
	for (std::size_t i = 0; i < list.size(); ++i)
	{
		if (i > 0)
			out += ",\n" + pad;
		out += "IMAGE: " + list[i].image + ", COLOR: " + colorText(list[i].color) + ", BORDERCOLOR: " + colorText(list[i].border);
	}
	return out;
}

} // namespace WndModel
