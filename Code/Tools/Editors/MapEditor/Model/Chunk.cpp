/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Chunk.cpp -- the chunk container used by .map and .scb files.

#include "Model/Chunk.h"

#include <algorithm>
#include <type_traits>

namespace MapModel
{

namespace
{
// Chunk header: name id, version, data size.
constexpr std::size_t CHUNK_HEADER_SIZE = 4 + 2 + 4;
constexpr std::size_t MAX_STRING_LENGTH = 0xFFFF;
constexpr std::size_t MAX_NAME_LENGTH = 0xFF;
// Dictionary keys store the id in the upper 24 bits (DataChunkOutput::writeDict); the game's writer
// hands out ids from 1 upward.
constexpr std::uint32_t MAX_ID = 0xFFFFFF;
}

// ------------------------------------------------------------------------------------------------
// NameTable

std::optional<std::uint32_t> NameTable::find(std::string_view name) const
{
	const auto it = m_byName.find(std::string(name));
	if (it == m_byName.end())
		return std::nullopt;
	return it->second;
}

const std::string *NameTable::name(std::uint32_t id) const
{
	const auto it = m_byId.find(id);
	if (it == m_byId.end())
		return nullptr;
	return &m_entries[it->second].name;
}

void NameTable::index(const Entry &entry)
{
	// Duplicate names in a file: the first entry in file order wins, like the game's lookup.
	m_byName.emplace(entry.name, entry.id);
}

std::uint32_t NameTable::allocate(std::string_view name)
{
	if (const auto id = find(name))
		return *id;
	// Past MAX_ID the id no longer fits a dictionary key; ChunkWriter reports that as an error.
	Entry entry{std::string(name), m_nextId++};
	m_entries.insert(m_entries.begin(), entry);
	m_byName.clear();
	m_byId.clear();
	for (std::size_t i = 0; i < m_entries.size(); ++i)
	{
		index(m_entries[i]);
		m_byId.emplace(m_entries[i].id, i);
	}
	return entry.id;
}

bool NameTable::read(ByteReader &reader)
{
	const auto tag = reader.bytes(4);
	if (tag.size() != 4 || tag[0] != 'C' || tag[1] != 'k' || tag[2] != 'M' || tag[3] != 'p')
		return false;
	const std::size_t count = reader.count(reader.i32(), 1 + 4, "name table");
	std::uint32_t maxId = 0;
	for (std::size_t i = 0; i < count && reader.ok(); ++i)
	{
		const std::size_t len = reader.u8();
		const auto text = reader.bytes(len);
		Entry entry{std::string(text.begin(), text.end()), reader.u32()};
		if (!reader.ok())
			break;
		// The game's writer never repeats a name or an id; a table that does is damaged, and ids
		// in it could no longer be told apart.
		if (entry.id == 0 || entry.id > MAX_ID)
		{
			reader.fail("name table id out of range: " + std::to_string(entry.id));
			break;
		}
		if (m_byId.count(entry.id) != 0 || m_byName.count(entry.name) != 0)
		{
			reader.fail("name table repeats a name or an id");
			break;
		}
		maxId = std::max(maxId, entry.id);
		m_byId.emplace(entry.id, m_entries.size());
		index(entry);
		m_entries.push_back(std::move(entry));
	}
	m_nextId = std::max(m_nextId, maxId + 1);
	return reader.ok();
}

void NameTable::write(ByteWriter &writer) const
{
	writer.u8('C');
	writer.u8('k');
	writer.u8('M');
	writer.u8('p');
	writer.i32(static_cast<std::int32_t>(m_entries.size()));
	for (const Entry &entry : m_entries)
	{
		const std::size_t len = std::min(entry.name.size(), MAX_NAME_LENGTH);
		writer.u8(static_cast<std::uint8_t>(len));
		writer.bytes({reinterpret_cast<const std::uint8_t *>(entry.name.data()), len});
		writer.u32(entry.id);
	}
}

// ------------------------------------------------------------------------------------------------
// Chunks

bool splitChunks(std::span<const std::uint8_t> data, const NameTable &names, std::vector<ChunkView> &out, std::string &error)
{
	ByteReader reader(data);
	while (!reader.atEnd())
	{
		if (reader.remaining() < CHUNK_HEADER_SIZE)
		{
			error = "truncated chunk header at offset " + std::to_string(reader.position());
			return false;
		}
		const std::uint32_t id = reader.u32();
		const std::uint16_t version = reader.u16();
		const std::int32_t size = reader.i32();
		if (size < 0 || static_cast<std::size_t>(size) > reader.remaining())
		{
			error = "chunk size out of range at offset " + std::to_string(reader.position() - CHUNK_HEADER_SIZE);
			return false;
		}
		const std::string *name = names.name(id);
		if (name == nullptr)
		{
			error = "chunk with unknown name id " + std::to_string(id);
			return false;
		}
		out.push_back(ChunkView{*name, version, reader.bytes(static_cast<std::size_t>(size))});
	}
	return true;
}

// ------------------------------------------------------------------------------------------------
// PropertyMap

const PropertyValue *PropertyMap::find(std::string_view key) const
{
	for (const Pair &pair : m_pairs)
		if (pair.key == key)
			return &pair.value;
	return nullptr;
}

std::optional<std::int32_t> PropertyMap::getInt(std::string_view key) const
{
	const PropertyValue *v = find(key);
	if (v == nullptr || !std::holds_alternative<std::int32_t>(*v))
		return std::nullopt;
	return std::get<std::int32_t>(*v);
}

std::optional<float> PropertyMap::getReal(std::string_view key) const
{
	const PropertyValue *v = find(key);
	if (v == nullptr || !std::holds_alternative<float>(*v))
		return std::nullopt;
	return std::get<float>(*v);
}

std::optional<bool> PropertyMap::getBool(std::string_view key) const
{
	const PropertyValue *v = find(key);
	if (v == nullptr || !std::holds_alternative<bool>(*v))
		return std::nullopt;
	return std::get<bool>(*v);
}

std::optional<std::string> PropertyMap::getAscii(std::string_view key) const
{
	const PropertyValue *v = find(key);
	if (v == nullptr || !std::holds_alternative<std::string>(*v))
		return std::nullopt;
	return std::get<std::string>(*v);
}

void PropertyMap::set(std::string_view key, PropertyValue value)
{
	for (Pair &pair : m_pairs)
	{
		if (pair.key == key)
		{
			pair.value = std::move(value);
			return;
		}
	}
	m_pairs.push_back(Pair{std::string(key), std::move(value)});
}

bool PropertyMap::erase(std::string_view key)
{
	const auto it = std::find_if(m_pairs.begin(), m_pairs.end(), [&](const Pair &p) { return p.key == key; });
	if (it == m_pairs.end())
		return false;
	m_pairs.erase(it);
	return true;
}

ValueType valueType(const PropertyValue &value)
{
	return static_cast<ValueType>(value.index());
}

// ------------------------------------------------------------------------------------------------
// Field readers

std::string readAscii(ByteReader &reader)
{
	const std::size_t len = reader.u16();
	const auto text = reader.bytes(len);
	return std::string(text.begin(), text.end());
}

std::u16string readUnicode(ByteReader &reader)
{
	const std::size_t len = reader.u16();
	std::u16string text;
	if (reader.remaining() < len * 2)
	{
		reader.fail("unicode string exceeds the data left");
		return text;
	}
	text.reserve(len);
	for (std::size_t i = 0; i < len; ++i)
		text.push_back(static_cast<char16_t>(reader.u16()));
	return text;
}

namespace
{
std::string keyName(std::int32_t keyAndType, const NameTable &names, ByteReader &reader)
{
	const std::uint32_t id = static_cast<std::uint32_t>(keyAndType) >> 8;
	const std::string *name = names.name(id);
	if (name == nullptr)
	{
		reader.fail("dictionary key with unknown name id " + std::to_string(id));
		return {};
	}
	return *name;
}
}

PropertyMap readDict(ByteReader &reader, const NameTable &names)
{
	PropertyMap map;
	const std::size_t count = reader.count(reader.u16(), 4 + 1, "dictionary");
	for (std::size_t i = 0; i < count && reader.ok(); ++i)
	{
		const std::int32_t keyAndType = reader.i32();
		std::string key = keyName(keyAndType, names, reader);
		switch (keyAndType & 0xFF)
		{
			case 0: map.set(key, reader.boolean()); break;
			case 1: map.set(key, reader.i32()); break;
			case 2: map.set(key, reader.f32()); break;
			case 3: map.set(key, readAscii(reader)); break;
			case 4: map.set(key, readUnicode(reader)); break;
			default: reader.fail("unknown dictionary value type " + std::to_string(keyAndType & 0xFF)); break;
		}
	}
	return map;
}

std::string readNameKey(ByteReader &reader, const NameTable &names)
{
	return keyName(reader.i32(), names, reader);
}

// ------------------------------------------------------------------------------------------------
// ChunkWriter

void ChunkWriter::begin(std::string_view name, std::uint16_t version)
{
	if (name.size() > MAX_NAME_LENGTH && m_error.empty())
		m_error = "chunk name too long: " + std::string(name);
	const std::uint32_t id = m_names.allocate(name);
	if (id > MAX_ID && m_error.empty())
		m_error = "name table full";
	m_out.u32(id);
	m_out.u16(version);
	m_open.push_back(m_out.size());
	// Placeholder like the game's writer; end() patches the real size.
	m_out.i32(0xFFFF);
}

void ChunkWriter::end()
{
	if (m_open.empty())
	{
		if (m_error.empty())
			m_error = "end() without begin()";
		return;
	}
	const std::size_t pos = m_open.back();
	m_open.pop_back();
	m_out.patchU32(pos, static_cast<std::uint32_t>(m_out.size() - pos - 4));
}

void ChunkWriter::ascii(std::string_view text)
{
	if (text.size() > MAX_STRING_LENGTH)
	{
		if (m_error.empty())
			m_error = "string longer than 65535 characters";
		text = text.substr(0, MAX_STRING_LENGTH);
	}
	m_out.u16(static_cast<std::uint16_t>(text.size()));
	m_out.bytes({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

void ChunkWriter::unicode(std::u16string_view text)
{
	if (text.size() > MAX_STRING_LENGTH)
	{
		if (m_error.empty())
			m_error = "string longer than 65535 characters";
		text = text.substr(0, MAX_STRING_LENGTH);
	}
	m_out.u16(static_cast<std::uint16_t>(text.size()));
	for (char16_t c : text)
		m_out.u16(static_cast<std::uint16_t>(c));
}

void ChunkWriter::dict(const PropertyMap &map)
{
	if (map.size() > 0xFFFF && m_error.empty())
		m_error = "dictionary with more than 65535 entries";
	m_out.u16(static_cast<std::uint16_t>(map.size()));
	for (const PropertyMap::Pair &pair : map.pairs())
	{
		const std::uint32_t id = m_names.allocate(pair.key);
		if (id > MAX_ID && m_error.empty())
			m_error = "name table full";
		const auto type = static_cast<std::uint32_t>(valueType(pair.value));
		m_out.u32((id << 8) | type);
		std::visit([this](const auto &v) {
			using T = std::decay_t<decltype(v)>;
			if constexpr (std::is_same_v<T, bool>)
				m_out.boolean(v);
			else if constexpr (std::is_same_v<T, std::int32_t>)
				m_out.i32(v);
			else if constexpr (std::is_same_v<T, float>)
				m_out.f32(v);
			else if constexpr (std::is_same_v<T, std::string>)
				ascii(v);
			else
				unicode(v);
		}, pair.value);
	}
}

void ChunkWriter::nameKey(std::string_view name)
{
	// Encoded like a dictionary key of type ascii string (DataChunkOutput::writeNameKey).
	const std::uint32_t id = m_names.allocate(name);
	if (id > MAX_ID && m_error.empty())
		m_error = "name table full";
	m_out.u32((id << 8) | static_cast<std::uint32_t>(ValueType::Ascii));
}

void ChunkWriter::stored(const StoredChunk &chunk)
{
	begin(chunk.name, chunk.version);
	m_out.bytes(chunk.data);
	end();
}

} // namespace MapModel
