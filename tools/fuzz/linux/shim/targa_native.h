/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Force-included for the native TGA fuzzer only: the file factory interface without the Windows
// parts of WWLib/ffactory.h (mutex, WWLib containers; sets the include guard of the real header) and
// a silent WWDEBUG_SAY.
#pragma once

#define FFACTORY_H

class FileClass;

class FileFactoryClass
{
public:
	virtual ~FileFactoryClass(void) {}
	virtual FileClass *Get_File(char const *filename) = 0;
	virtual void Return_File(FileClass *file) = 0;
};

class RawFileFactoryClass : public FileFactoryClass {};

extern FileFactoryClass *_TheFileFactory;
extern RawFileFactoryClass *_TheWritingFileFactory;

#define WWDEBUG_SAY(x)
