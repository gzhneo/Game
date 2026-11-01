/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Chunk.h -- the chunk container used by .map and .scb files, independent of the engine.
//
// File layout: "CkMp", Int32 count, then count entries (UInt8 length, name, UInt32 id) -- the name
// table -- followed by chunks. A chunk is UInt32 name id, UInt16 version, Int32 size and size bytes
// of data; the data may itself hold chunks. Dictionary keys and script name keys share the name
// table with chunk names.
//
// The game's writer hands out ids in order of first use and puts each new name in front of the
// table. NameTable does the same, so writing the same content in the same order gives the same
// bytes, and writing with a table read from a file keeps every id of that file.

#pragma once

#include "Model/ByteIO.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace MapModel
{

class NameTable
{
public:
	struct Entry
	{
		std::string name;
		std::uint32_t id = 0;
	};

	std::optional<std::uint32_t> find(std::string_view name) const;
	const std::string *name(std::uint32_t id) const;
	std::uint32_t allocate(std::string_view name);

	// Entries in file order (newest first, like the game writes them).
	const std::vector<Entry> &entries() const { return m_entries; }
	bool empty() const { return m_entries.empty(); }

	// Reads the table at the start of a chunk file. Returns false if the tag is missing.
	bool read(ByteReader &reader);
	void write(ByteWriter &writer) const;

private:
	void index(const Entry &entry);

	std::vector<Entry> m_entries;
	std::unordered_map<std::string, std::uint32_t> m_byName;
	std::unordered_map<std::uint32_t, std::size_t> m_byId;
	std::uint32_t m_nextId = 1;
};

// A chunk kept as bytes, for chunks the model does not interpret. Its data may contain name ids,
// so it is only valid together with the name table it was read with.
struct StoredChunk
{
	std::string name;
	std::uint16_t version = 0;
	std::vector<std::uint8_t> data;

	bool operator==(const StoredChunk &) const = default;
};

// A chunk found while reading; the data points into the reader's buffer.
struct ChunkView
{
	std::string name;
	std::uint16_t version = 0;
	std::span<const std::uint8_t> data;
};

// Splits data into the chunks it contains. Fails on truncated headers or unknown name ids.
bool splitChunks(std::span<const std::uint8_t> data, const NameTable &names, std::vector<ChunkView> &out, std::string &error);

// Value types of the game's Dict (Common/Dict.h, DataType): the numbers are the file encoding.
enum class ValueType : std::uint8_t
{
	Bool = 0,
	Int = 1,
	Real = 2,
	Ascii = 3,
	Unicode = 4
};

using PropertyValue = std::variant<bool, std::int32_t, float, std::string, std::u16string>;

// Ordered key/value list: the file order is kept so an unchanged map writes back identically.
class PropertyMap
{
public:
	struct Pair
	{
		std::string key;
		PropertyValue value;

		bool operator==(const Pair &) const = default;
	};

	const PropertyValue *find(std::string_view key) const;
	std::optional<std::int32_t> getInt(std::string_view key) const;
	std::optional<float> getReal(std::string_view key) const;
	std::optional<bool> getBool(std::string_view key) const;
	std::optional<std::string> getAscii(std::string_view key) const;

	// Replaces the value of an existing key in place, otherwise appends.
	void set(std::string_view key, PropertyValue value);
	bool erase(std::string_view key);

	const std::vector<Pair> &pairs() const { return m_pairs; }
	std::size_t size() const { return m_pairs.size(); }
	bool empty() const { return m_pairs.empty(); }

	bool operator==(const PropertyMap &) const = default;

private:
	std::vector<Pair> m_pairs;
};

ValueType valueType(const PropertyValue &value);

// Field readers and writers of the chunk format.
std::string readAscii(ByteReader &reader);
std::u16string readUnicode(ByteReader &reader);
PropertyMap readDict(ByteReader &reader, const NameTable &names);
std::string readNameKey(ByteReader &reader, const NameTable &names);

class ChunkWriter
{
public:
	ChunkWriter(ByteWriter &out, NameTable &names) : m_out(out), m_names(names) {}

	void begin(std::string_view name, std::uint16_t version);
	void end();
	bool balanced() const { return m_open.empty(); }

	ByteWriter &out() { return m_out; }
	void ascii(std::string_view text);
	void unicode(std::u16string_view text);
	void dict(const PropertyMap &map);
	void nameKey(std::string_view name);
	void stored(const StoredChunk &chunk);

	// Set when a value does not fit the format (string over 65535 characters and similar).
	const std::string &error() const { return m_error; }

private:
	ByteWriter &m_out;
	NameTable &m_names;
	std::vector<std::size_t> m_open;
	std::string m_error;
};

} // namespace MapModel
