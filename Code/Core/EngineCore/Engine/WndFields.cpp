/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndFields.cpp -- see WndFields.h.

#include "Engine/WndFields.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Engine
{

bool parseWndFont(char *buffer, std::string &name, int &size, int &bold)
{
	const char *seps = " ,\n\r\t";
	const char *stringSeps = ":,\n\r\t\"";
	name.clear();
	size = 0;
	bold = 0;

	// strtok ends the label with a NUL; the quote lies behind it, so the search runs to the
	// original end of the text
	const char *bufferEnd = buffer + std::strlen(buffer);
	std::strtok(buffer, seps);	// label
	char *ptr = buffer;
	while (*ptr != '"' && ptr < bufferEnd)
		ptr++;
	if (*ptr != '"')
		return false;
	ptr++;
	const char *value = std::strtok(ptr, stringSeps);
	if (value != nullptr)
		name = value;

	std::strtok(nullptr, seps);	// SIZE label
	if (const char *v = std::strtok(nullptr, seps))
		size = std::atoi(v);
	std::strtok(nullptr, seps);	// BOLD label
	if (const char *v = std::strtok(nullptr, seps))
		bold = std::atoi(v);
	return true;
}

char *wndAfterFirstQuote(char *text)
{
	while (*text != '"' && *text != '\0')
		text++;
	if (*text == '"')
		text++;
	return text;
}

const char *wndQuotedValue(char *text, const char *stringSeps)
{
	const char *value = std::strtok(wndAfterFirstQuote(text), stringSeps);
	return value != nullptr ? value : "";
}

bool readWndChunk(WndByteSource source, void *context, char *buffer, int maxLen)
{
	int i = 0;
	bool start = true;

	while (i < maxLen)
	{
		char c = '\0';
		if (!source(context, c))
		{
			buffer[i] = '\0';
			return true;
		}
		buffer[i] = c;

		if (std::isspace(static_cast<unsigned char>(c)))
		{
			if (!start)
				buffer[i++] = ' ';
		}
		else
		{
			start = false;
			if (c == ';')
			{
				buffer[i] = '\0';
				return true;
			}
			i++;
		}
	}

	buffer[maxLen - 1] = '\0';
	return false;
}

int wndScanBool(const char *source, bool &value)
{
	if (source == nullptr)
	{
		value = false;
		return 0;
	}
	int temp = 0;
	const int ret = std::sscanf(source, "%d", &temp);
	value = (temp != 0);
	return ret;
}

int wndScanShort(const char *source, short &value)
{
	if (source == nullptr)
	{
		value = 0;
		return 0;
	}
	int temp = 0;
	const int ret = std::sscanf(source, "%d", &temp);
	value = static_cast<short>(temp);
	return ret;
}

int wndScanInt(const char *source, int &value)
{
	if (source == nullptr)
		return 0;
	return std::sscanf(source, "%d", &value);
}

int wndScanUnsigned(const char *source, unsigned int &value)
{
	if (source == nullptr)
		return 0;
	return std::sscanf(source, "%u", &value);
}

int wndToInt(const char *token)
{
	return token != nullptr ? std::atoi(token) : 0;
}

int wndClamp(int value, int lo, int hi)
{
	return value < lo ? lo : (value > hi ? hi : value);
}

bool wndFieldFitsType(const char *field, const char *type)
{
	if (field == nullptr || type == nullptr)
		return false;
	if (std::strcmp(field, "LISTBOXDATA") == 0)
		return std::strcmp(type, "SCROLLLISTBOX") == 0;
	if (std::strcmp(field, "COMBOBOXDATA") == 0)
		return std::strcmp(type, "COMBOBOX") == 0;
	if (std::strcmp(field, "SLIDERDATA") == 0)
		return std::strcmp(type, "VERTSLIDER") == 0 || std::strcmp(type, "HORZSLIDER") == 0;
	if (std::strcmp(field, "RADIOBUTTONDATA") == 0)
		return std::strcmp(type, "RADIOBUTTON") == 0;
	if (std::strcmp(field, "STATICTEXTDATA") == 0)
		return std::strcmp(type, "STATICTEXT") == 0;
	if (std::strcmp(field, "TEXTENTRYDATA") == 0)
		return std::strcmp(type, "ENTRYFIELD") == 0;
	if (std::strcmp(field, "TABCONTROLDATA") == 0)
		return std::strcmp(type, "TABCONTROL") == 0;
	return true;
}

int wndReadBoolList(int count, const char *seps, bool *out, int capacity)
{
	int read = 0;
	for (; read < count; ++read)
	{
		const char *token = std::strtok(nullptr, seps);
		if (token == nullptr)
			break;
		if (read < capacity)
			wndScanBool(token, out[read]);
	}
	return read;
}

}	// namespace Engine
