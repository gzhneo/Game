/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: QuotedPrintable.cpp /////////////////////////////////////////////////////////
// Author: Matt Campbell, February 2002
// Description: Quoted-printable encode/decode
////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/QuotedPrintable.h"

#define MAGIC_CHAR '_'

// takes an integer and returns an ASCII representation
static char intToHexDigit(int num)
{
	if (num<0 || num >15) return '\0';
	if (num<10)
	{
		return static_cast<char>('0' + num);
	}
	return static_cast<char>('A' + (num-10));
}

// convert an ASCII representation of a hex digit into the digit itself
static int hexDigitToInt(char c)
{
	if (c <= '9' && c >= '0') return (c - '0');
	if (c <= 'f' && c >= 'a') return (c - 'a' + 10);
	if (c <= 'F' && c >= 'A') return (c - 'A' + 10);
	return 0;
}

// The texts come from files and the network (map names, player names). Output is cut at the same
// lengths as before (1023 characters, 1023 wide characters); bytes are handled as unsigned so that
// characters from 0x80 on get two hex digits.
namespace
{
const int QP_MAX_OUT = 1023;

// Appends one byte, encoded unless it is an ASCII letter or digit.
void appendEncoded(char *dest, int &i, unsigned char ch)
{
	if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'))
	{
		dest[i++] = static_cast<char>(ch);
	}
	else
	{
		dest[i++] = MAGIC_CHAR;
		dest[i++] = intToHexDigit(ch >> 4);
		dest[i++] = intToHexDigit(ch & 0xf);
	}
}

// Decodes into dest (at most maxBytes bytes); returns the number of bytes written.
int decode(unsigned char *dest, int maxBytes, const unsigned char *src)
{
	int n = 0;
	while (*src && n < maxBytes)
	{
		unsigned char value;
		if (*src == MAGIC_CHAR)
		{
			if (src[1] == '\0')
			{
				// string ends with MAGIC_CHAR
				break;
			}
			value = static_cast<unsigned char>(hexDigitToInt(src[1]));
			src++;
			if (src[1] != '\0')
			{
				value = static_cast<unsigned char>((value << 4) | hexDigitToInt(src[1]));
				src++;
			}
		}
		else
		{
			value = *src;
		}
		dest[n++] = value;
		src++;
	}
	return n;
}
}	// namespace

// Convert unicode strings into ascii quoted-printable strings (each byte of the UTF-16 text)
AsciiString UnicodeStringToQuotedPrintable(UnicodeString original)
{
	char dest[QP_MAX_OUT + 1];
	const unsigned char *src = reinterpret_cast<const unsigned char *>(original.str());
	int i = 0;
	// a character takes up to six output characters
	while (!(src[0] == '\0' && src[1] == '\0') && i + 6 <= QP_MAX_OUT)
	{
		appendEncoded(dest, i, src[0]);
		appendEncoded(dest, i, src[1]);
		src += 2;
	}
	dest[i] = '\0';

	return dest;
}

// Convert ascii strings into ascii quoted-printable strings
AsciiString AsciiStringToQuotedPrintable(AsciiString original)
{
	char dest[QP_MAX_OUT + 1];
	const unsigned char *src = reinterpret_cast<const unsigned char *>(original.str());
	int i = 0;
	while (src[0] != '\0' && i + 3 <= QP_MAX_OUT)
	{
		appendEncoded(dest, i, src[0]);
		src++;
	}
	dest[i] = '\0';

	return dest;
}

// Convert ascii quoted-printable strings into unicode strings
UnicodeString QuotedPrintableToUnicodeString(AsciiString original)
{
	WideChar dest[QP_MAX_OUT + 1];
	unsigned char *bytes = reinterpret_cast<unsigned char *>(dest);
	const int n = decode(bytes, QP_MAX_OUT * static_cast<int>(sizeof(WideChar)), reinterpret_cast<const unsigned char *>(original.str()));
	// an odd byte count leaves a half character; complete it and terminate
	std::memset(bytes + n, 0, sizeof(dest) - n);

	UnicodeString out(dest);
	return out;
}

// Convert ascii quoted-printable strings into ascii strings
AsciiString QuotedPrintableToAsciiString(AsciiString original)
{
	unsigned char dest[QP_MAX_OUT + 1];
	const int n = decode(dest, QP_MAX_OUT, reinterpret_cast<const unsigned char *>(original.str()));
	dest[n] = 0;

	return AsciiString(reinterpret_cast<const char *>(dest));
}
