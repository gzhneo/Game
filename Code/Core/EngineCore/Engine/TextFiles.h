/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextFiles.h -- readers for the game's string tables: the compiled .csf and the text form .str
// (also map.str, which comes with maps from other players). No engine header, so EngineTests and
// the fuzzers test them directly; GameTextManager (GameText.cpp) turns the entries into its tables.
//
// Every length and count comes from the file and is checked; a text, label or speech name longer
// than the limits below is an error (.csf) or cut (.str), never written past a buffer.

#pragma once

#ifndef ENGINE_TEXTFILES_H
#define ENGINE_TEXTFILES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine
{

const std::size_t TEXT_FILE_MAX_LABEL = 10239;		///< characters of a label or speech name
const std::size_t TEXT_FILE_MAX_TEXT = 10239;		///< characters of a text

// --- .csf ---------------------------------------------------------------------------------------

struct CsfEntry
{
	std::string label;
	std::u16string text;		///< the first string of the label, already decoded
	std::string speech;			///< speech file of the first string, if any
};

enum class CsfError
{
	None,
	NotCsf,
	TruncatedHeader,
	BadLabelCount,			///< negative, more than the file can hold, or more labels than announced
	UnknownBlock,			///< neither a label nor a string where one is expected
	Truncated,
	TooLong					///< a label, text or speech name beyond the limits
};

/// Reads a whole .csf file. version and language come from the header (language only from
/// version 2 on, else -1). On an error the file must not be used.
CsfError readCsf(const unsigned char *data, std::size_t size, int &version, int &language, std::vector<CsfEntry> &entries);

const char *csfErrorText(CsfError error);

// --- .str ---------------------------------------------------------------------------------------

struct StrEntry
{
	std::string label;
	std::string text;			///< as between the quotes, escapes still in; lines joined by spaces
	std::string speech;			///< speech name after the closing quote
	bool hasText = false;
	int extraTexts = 0;			///< further quoted strings of the label (ignored)
};

/// Reads a whole .str file: label lines, each followed by quoted strings and "END"; lines starting
/// with // are comments. Returns false if the file ends inside a label (entries then holds that
/// label as its last entry).
bool readStr(const char *data, std::size_t size, std::vector<StrEntry> &entries);

}	// namespace Engine

#endif	// ENGINE_TEXTFILES_H
