/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/DbFile.h"

#include <optional>

namespace Babylon
{

namespace
{

constexpr std::uint32_t id4(char a, char b, char c, char d)
{
	return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
		(static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

const std::uint32_t ID_FORM = id4('F', 'O', 'R', 'M');
const std::uint32_t FORM_DB = id4('N', 'X', 'D', 'B');
const std::uint32_t FORM_LABEL = id4('N', 'L', 'B', 'L');
const std::uint32_t FORM_TEXT = id4('N', 'T', 'X', 'T');
const std::uint32_t FORM_TRANS = id4('N', 'T', 'R', 'N');
const std::uint32_t CHUNK_COMMENT = id4('C', 'M', 'N', 'T');
const std::uint32_t CHUNK_CONTEXT = id4('C', 'T', 'X', 'T');
const std::uint32_t CHUNK_SPEAKER = id4('S', 'P', 'K', 'R');
const std::uint32_t CHUNK_LISTENER = id4('L', 'T', 'N', 'R');
const std::uint32_t CHUNK_TEXT = id4('T', 'E', 'X', 'T');
const std::uint32_t CHUNK_WAVE = id4('W', 'A', 'V', 'E');
const std::uint32_t CHUNK_WAVE_INFO = id4('W', 'V', 'I', 'N');
const std::uint32_t CHUNK_INFO = id4('I', 'N', 'F', 'O');
const std::uint32_t CHUNK_NAME = id4('N', 'A', 'M', 'E');

std::uint32_t getBE(std::span<const std::uint8_t> d, std::size_t at)
{
	return (static_cast<std::uint32_t>(d[at]) << 24) | (static_cast<std::uint32_t>(d[at + 1]) << 16) | (static_cast<std::uint32_t>(d[at + 2]) << 8) |
		static_cast<std::uint32_t>(d[at + 3]);
}

std::int32_t getLE(std::span<const std::uint8_t> d, std::size_t at)
{
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(d[at]) | (static_cast<std::uint32_t>(d[at + 1]) << 8) |
		(static_cast<std::uint32_t>(d[at + 2]) << 16) | (static_cast<std::uint32_t>(d[at + 3]) << 24));
}

struct Chunk
{
	std::uint32_t id;
	std::span<const std::uint8_t> data;
};

struct Form
{
	std::uint32_t id;
	std::vector<Chunk> chunks;
};

// Splits the file into its forms; nullopt with a message if a size points outside the file.
std::optional<std::vector<Form>> splitForms(std::span<const std::uint8_t> d, std::string &error)
{
	std::vector<Form> forms;
	std::size_t pos = 0;
	while (pos + 12 <= d.size())
	{
		if (getBE(d, pos) != ID_FORM)
		{
			error = "no FORM at byte " + std::to_string(pos);
			return std::nullopt;
		}
		const std::uint32_t size = getBE(d, pos + 4);
		if (size < 4 || size > d.size() - pos - 8)
		{
			error = "form at byte " + std::to_string(pos) + " runs past the end of the file";
			return std::nullopt;
		}
		Form form;
		form.id = getBE(d, pos + 8);
		std::size_t c = pos + 12;
		const std::size_t end = pos + 8 + size;
		while (c + 8 <= end)
		{
			const std::uint32_t chunkId = getBE(d, c);
			const std::uint32_t chunkSize = getBE(d, c + 4);
			if (chunkSize > end - c - 8)
			{
				error = "chunk at byte " + std::to_string(c) + " runs past its form";
				return std::nullopt;
			}
			form.chunks.push_back({chunkId, d.subspan(c + 8, chunkSize)});
			c += 8 + chunkSize + (chunkSize & 1u);
		}
		forms.push_back(std::move(form));
		pos = end + (size & 1u);
	}
	// Fewer than 12 bytes left over were ignored by the original as well.
	return forms;
}

std::u16string readString(std::span<const std::uint8_t> d)
{
	std::u16string s;
	for (std::size_t i = 0; i + 1 < d.size(); i += 2)
	{
		const auto c = static_cast<char16_t>(d[i] | (d[i + 1] << 8));
		if (c == 0)
			break;
		s.push_back(c);
	}
	return s;
}

bool readWaveInfo(const Chunk &chunk, WaveInfo &wave)
{
	if (chunk.data.size() < 12)
		return false;
	wave.valid = getLE(chunk.data, 0) != 0;
	wave.lo = static_cast<std::uint32_t>(getLE(chunk.data, 4));
	wave.hi = static_cast<std::uint32_t>(getLE(chunk.data, 8));
	return true;
}

class Writer
{
public:
	std::vector<std::uint8_t> out;

	void beginForm(std::uint32_t id)
	{
		m_form = out.size();
		putBE(ID_FORM);
		putBE(0);
		putBE(id);
	}
	void endForm()
	{
		const std::size_t size = out.size() - m_form - 8;
		if (size & 1u)
			out.push_back(0);
		setBE(m_form + 4, static_cast<std::uint32_t>(size));
	}
	void chunk(std::uint32_t id, const std::vector<std::uint8_t> &data)
	{
		putBE(id);
		putBE(static_cast<std::uint32_t>(data.size()));
		out.insert(out.end(), data.begin(), data.end());
		if (data.size() & 1u)
			out.push_back(0);
	}
	void info(std::uint32_t id, std::initializer_list<std::int32_t> values)
	{
		std::vector<std::uint8_t> data;
		for (const std::int32_t v : values)
			for (int k = 0; k < 4; ++k)
				data.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * k)));
		chunk(id, data);
	}
	// Empty strings are left out, as in the original.
	void string(std::uint32_t id, const std::u16string &s)
	{
		if (s.empty())
			return;
		std::vector<std::uint8_t> data;
		for (const char16_t c : s)
		{
			data.push_back(static_cast<std::uint8_t>(c & 0xFF));
			data.push_back(static_cast<std::uint8_t>(c >> 8));
		}
		data.push_back(0);
		data.push_back(0);
		chunk(id, data);
	}
	void wave(const WaveInfo &w)
	{
		if (w.valid)
			info(CHUNK_WAVE_INFO, {1, static_cast<std::int32_t>(w.lo), static_cast<std::int32_t>(w.hi)});
	}

private:
	void putBE(std::uint32_t v)
	{
		for (int k = 3; k >= 0; --k)
			out.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
	}
	void setBE(std::size_t at, std::uint32_t v)
	{
		for (int k = 0; k < 4; ++k)
			out[at + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(v >> (8 * (3 - k)));
	}
	std::size_t m_form = 0;
};

void writeText(Writer &w, const Text &text)
{
	w.beginForm(FORM_TEXT);
	w.info(CHUNK_INFO, {text.id, text.revision});
	w.string(CHUNK_TEXT, text.text);
	w.string(CHUNK_WAVE, text.wave);
	w.wave(text.waveInfo);
	w.endForm();
	for (const Translation &t : text.translations)
	{
		w.beginForm(FORM_TRANS);
		w.info(CHUNK_INFO, {static_cast<std::int32_t>(t.lang), t.revision});
		w.string(CHUNK_TEXT, t.text);
		w.string(CHUNK_COMMENT, t.comment);
		w.wave(t.wave);
		w.endForm();
	}
}

} // namespace

bool readDatabase(std::span<const std::uint8_t> data, Database &db, std::string &error)
{
	db.clear();
	const auto forms = splitForms(data, error);
	if (!forms)
		return false;
	if (forms->empty() || forms->front().id != FORM_DB)
	{
		error = "not a Babylon database (no NXDB form at the start)";
		return false;
	}
	int nextId = -1;
	for (const Chunk &c : forms->front().chunks)
		if (c.id == CHUNK_INFO)
		{
			if (c.data.size() < 8)
			{
				error = "database header too short";
				return false;
			}
			nextId = getLE(c.data, 4);
		}

	// A text belongs to the label before it; texts before the first label are obsolete.
	Label *label = nullptr;
	Text *text = nullptr;
	bool ok = true;
	for (std::size_t f = 1; f < forms->size() && ok; ++f)
	{
		const Form &form = (*forms)[f];
		if (form.id == FORM_LABEL)
		{
			db.labels.emplace_back();
			label = &db.labels.back();
			text = nullptr;
			for (const Chunk &c : form.chunks)
			{
				if (c.id == CHUNK_INFO)
				{
					if (c.data.size() < 4)
						ok = false;
					else
						label->maxLen = getLE(c.data, 0);
				}
				else if (c.id == CHUNK_NAME)
					label->name = readString(c.data);
				else if (c.id == CHUNK_COMMENT)
					label->comment = readString(c.data);
				else if (c.id == CHUNK_CONTEXT)
					label->context = readString(c.data);
				else if (c.id == CHUNK_SPEAKER)
					label->speaker = readString(c.data);
				else if (c.id == CHUNK_LISTENER)
					label->listener = readString(c.data);
			}
		}
		else if (form.id == FORM_TEXT)
		{
			std::vector<Text> &list = label ? label->texts : db.obsolete;
			list.emplace_back();
			text = &list.back();
			for (const Chunk &c : form.chunks)
			{
				if (c.id == CHUNK_INFO)
				{
					if (c.data.size() < 8)
						ok = false;
					else
					{
						text->id = getLE(c.data, 0);
						text->revision = getLE(c.data, 4);
					}
				}
				else if (c.id == CHUNK_TEXT)
					text->text = readString(c.data);
				else if (c.id == CHUNK_WAVE)
					text->wave = readString(c.data);
				else if (c.id == CHUNK_WAVE_INFO)
					ok = readWaveInfo(c, text->waveInfo) && ok;
			}
		}
		else if (form.id == FORM_TRANS)
		{
			Translation trans;
			for (const Chunk &c : form.chunks)
			{
				if (c.id == CHUNK_INFO)
				{
					if (c.data.size() < 8)
						ok = false;
					else
					{
						trans.lang = static_cast<Lang>(getLE(c.data, 0));
						trans.revision = getLE(c.data, 4);
					}
				}
				else if (c.id == CHUNK_TEXT)
					trans.text = readString(c.data);
				else if (c.id == CHUNK_COMMENT)
					trans.comment = readString(c.data);
				else if (c.id == CHUNK_WAVE_INFO)
					ok = readWaveInfo(c, trans.wave) && ok;
			}
			// A translation without a text before it is dropped, as in the original.
			if (text)
				text->translations.push_back(std::move(trans));
		}
	}
	if (!ok)
	{
		db.clear();
		error = "an INFO chunk of the database is too short";
		return false;
	}
	db.nextId = nextId;
	// Texts without an ID got one when the original read them.
	db.assignIds();
	db.modified = false;
	return true;
}

std::vector<std::uint8_t> writeDatabase(const Database &db)
{
	Writer w;
	w.beginForm(FORM_DB);
	w.info(CHUNK_INFO, {static_cast<std::int32_t>(db.labels.size()), db.nextId});
	w.endForm();
	for (const Text &text : db.obsolete)
		writeText(w, text);
	for (const Label &label : db.labels)
	{
		w.beginForm(FORM_LABEL);
		w.info(CHUNK_INFO, {label.maxLen});
		w.string(CHUNK_NAME, label.name);
		w.string(CHUNK_COMMENT, label.comment);
		w.string(CHUNK_CONTEXT, label.context);
		w.string(CHUNK_SPEAKER, label.speaker);
		w.string(CHUNK_LISTENER, label.listener);
		w.endForm();
		for (const Text &text : label.texts)
			writeText(w, text);
	}
	return std::move(w.out);
}

} // namespace Babylon
