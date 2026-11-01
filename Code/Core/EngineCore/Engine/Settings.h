/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Settings.h -- user settings: the schema (every setting described once), the file format with
// sections, the settings object with its layers and the one-time import of the old files
// (Options.ini, Network.ini, Skirmish.ini). Needs no engine header, so EngineTests test it; reading
// and writing the files and copying values into GlobalData is the engine's part.
//
// Layers, lowest first: default (schema, or set by the engine from game data), file, command line.
// set() only changes the file layer, so a value forced on the command line never ends up in a file.
//
// Two files: Settings.ini holds what the user chooses, State.ini what the game remembers (last map,
// player name, hints shown). Both are UTF-8; comments and keys the schema does not know are kept.

#pragma once

#ifndef ENGINE_SETTINGS_H
#define ENGINE_SETTINGS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/PreferenceFile.h"

namespace Engine
{
namespace Settings
{

// ---------------------------------------------------------------------------------------------
// Schema

enum class File
{
	Settings,	///< Settings.ini
	State		///< State.ini
};

enum class Type
{
	Bool,		///< yes/no (reads also true/false, on/off, 1/0)
	Int,		///< with min and max
	Float,		///< with min and max
	Enum,		///< one of choices (case-insensitive, written as in the schema)
	Text,		///< free text (UTF-8)
	Resolution,	///< "width height"
	List		///< entries separated by ';'
};

enum class Apply
{
	Immediate,	///< observers apply the change at once
	Restart,	///< takes effect at the next start
	NextMatch	///< takes effect at the next match
};

enum class Legacy
{
	None,
	Options,	///< Options.ini
	Network,	///< Network.ini
	Skirmish	///< Skirmish.ini
};

/// How an old value is converted on import.
enum class LegacyCoding
{
	Plain,			///< as it is (CP1252 converted to UTF-8)
	QuotedAscii,	///< quoted-printable bytes, CP1252
	QuotedUnicode,	///< quoted-printable UTF-16 (player names)
	NumberedList	///< Key0, Key1, ... with the count in NumKeys (RemoteIP)
};

struct Definition
{
	const char *id;					///< "Section.Key", also the address in code
	File file;
	Type type;
	const char *defaultValue;		///< nullptr: the engine sets it from game data
	double minimum;
	double maximum;
	const char *choices;			///< Enum: "Low|Medium|High"
	Apply apply;
	const char *commandLine;		///< switch that overrides it, or nullptr
	Legacy legacyFile;
	const char *legacyKey;
	LegacyCoding legacyCoding;
	const char *description;		///< written as a comment above the key in a new file
};

/// All settings, in the order they are written. A static table without allocation.
std::span<const Definition> schema();

/// nullptr if the id is unknown (case-insensitive).
const Definition *findDefinition(std::string_view id);

/// Keys of the old files that have no reader any more and are not imported.
std::span<const char *const> droppedLegacyKeys();

// ---------------------------------------------------------------------------------------------
// Values

/// Checks text against the definition. Returns false for a value that cannot be read; clamps
/// numbers to the range and sets *clamped. normalized is the value as it will be written.
bool normalizeValue(const Definition &definition, std::string_view text, std::string &normalized,
	bool *clamped = nullptr);

bool parseBool(std::string_view text, bool &value);

// ---------------------------------------------------------------------------------------------
// File format

/// An INI-like file with sections that keeps its lines: comments, blank lines and unknown keys
/// survive a rewrite, changed keys are rewritten in place, new keys go to the end of their section.
class Document
{
public:
	struct Problem
	{
		int line = 0;			///< 1-based
		std::string text;
	};

	/// Parses text (UTF-8, an optional BOM is dropped). Lines that are neither a section, a key, a
	/// comment nor blank are kept unchanged and reported.
	void parse(std::string_view text, std::vector<Problem> *problems = nullptr);

	/// Value of a key (section and key case-insensitive); nullptr if missing.
	const std::string *find(std::string_view section, std::string_view key) const;

	/// Sets a key; adds it (and its section) if missing. comment is written above a new key.
	void set(std::string_view section, std::string_view key, std::string_view value, std::string_view comment = {});

	/// Removes a key (every line of it) and, if given, the comment line right above it when it is
	/// exactly that comment (the description written with set()); comments of the user stay.
	void remove(std::string_view section, std::string_view key, std::string_view comment = {});

	/// Appends a comment line ("; text").
	void addComment(std::string_view comment);

	/// All key lines in file order: (section, key, value).
	std::vector<std::vector<std::string>> entries() const;

	/// Text with the line ending the parsed text used (CRLF for a new document).
	std::string text() const;

private:
	enum class Kind { Blank, Comment, Section, Key, Other };
	struct Line
	{
		Kind kind = Kind::Blank;
		std::string raw;		///< as read, without line ending
		std::string section;	///< Section: its name; Key: the section it is in
		std::string key;
		std::string value;
	};

	std::vector<Line> m_lines;
	std::string m_newline = "\r\n";
};

// ---------------------------------------------------------------------------------------------
// Settings object

enum class Source
{
	None,			///< no value (an engine default that was never set)
	Default,
	File,
	CommandLine
};

struct Report
{
	std::vector<std::string> lines;		///< one finding per line, for the log
};

class Store
{
public:
	Store();

	/// Default from game data, for definitions without a fixed default.
	void setDefault(std::string_view id, std::string_view value);

	/// Reads the file layer of one file. Unknown keys stay in the document; invalid values are
	/// reported and ignored, numbers out of range are clamped.
	void loadFile(File file, std::string_view text, Report *report = nullptr);

	/// Command line layer; never written to a file.
	bool setCommandLine(std::string_view id, std::string_view value, Report *report = nullptr);

	/// Effective value as text ("" if none).
	std::string text(std::string_view id) const;
	bool getBool(std::string_view id) const;
	int getInt(std::string_view id) const;
	double getFloat(std::string_view id) const;
	Source sourceOf(std::string_view id) const;

	/// Changes the file layer; false if id or value are not valid. Observers run if the effective
	/// value changes.
	bool set(std::string_view id, std::string_view value);
	bool setBool(std::string_view id, bool value) { return set(id, value ? "yes" : "no"); }

	/// Value of the file layer only; nullptr if the file does not set it.
	const std::string *fileValue(std::string_view id) const;
	/// Removes the value from the file layer (and its line from the file).
	void unset(std::string_view id);
	bool setInt(std::string_view id, int value) { return set(id, std::to_string(value)); }

	/// Text of a file for saving; false from dirty() means nothing to save.
	std::string fileText(File file) const;
	bool dirty(File file) const;
	void markSaved(File file);

	/// Calls the callback when the effective value of id changes. Keep the returned object alive.
	using Observer = std::function<void()>;
	std::shared_ptr<void> observe(std::string_view id, Observer observer);

private:
	struct Value
	{
		std::string defaultValue;
		bool hasDefault = false;
		std::string fileValue;
		bool hasFile = false;
		std::string commandLineValue;
		bool hasCommandLine = false;
	};

	const Definition *definition(std::string_view id) const;
	void changed(const Definition &definition, const std::string &before);

	std::map<std::string, Value> m_values;			///< by id in lower case
	Document m_documents[2];
	bool m_dirty[2] = {};
	std::multimap<std::string, std::weak_ptr<Observer>> m_observers;
};

// ---------------------------------------------------------------------------------------------
// Import of the old files

struct LegacyFiles
{
	std::string options;	///< contents of Options.ini ("" if missing)
	std::string network;	///< Network.ini
	std::string skirmish;	///< Skirmish.ini
};

/// Takes the values of the old files into the file layer (Settings.ini and State.ini). Every key is
/// reported: imported, dropped (no reader), unknown or invalid. The old files are not changed.
void importLegacy(Store &store, const LegacyFiles &files, Report &report);

/// Text conversions used by the import and the game (player names, maps), exposed for the tests.
std::string cp1252ToUtf8(std::string_view text);
std::string decodeQuotedAscii(std::string_view text);		///< result in CP1252
std::string decodeQuotedUnicode(std::string_view text);		///< result in UTF-8
std::string utf8ToCp1252(std::string_view text);			///< '?' for what CP1252 cannot hold
std::string encodeQuotedAscii(std::string_view cp1252);		///< like AsciiStringToQuotedPrintable
std::string encodeQuotedUnicode(std::string_view utf8);		///< like UnicodeStringToQuotedPrintable

}	// namespace Settings
}	// namespace Engine

#endif	// ENGINE_SETTINGS_H
