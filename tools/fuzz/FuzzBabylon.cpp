/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// FuzzBabylon.cpp -- fuzz target for the readers of the translation tool Babylon
// (Code/Tools/Editors/Babylon): main.db, translation sheets (.xlsx, .ods, .csv, from translators), Generals.str
// and the game's .csf/.str as source.
//
// Every input goes through all readers. Two properties are checked besides crashes and undefined
// behaviour: a database the reader accepts writes and reads again to the same bytes, and a sheet it
// accepts imports into a small database without harm.
//
// Like the other targets it runs under libFuzzer (FUZZ_LIBFUZZER_MAIN) or as a plain program that
// checks the files given on the command line.

#include "Core/Exchange.h"
#include "Core/GameImport.h"
#include "Core/Update.h"
#include "Model/DbFile.h"
#include "Model/Sheet.h"
#include "Model/StrFile.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{

const Babylon::Database &sampleDb()
{
	static const Babylon::Database db = [] {
		Babylon::Database str;
		std::vector<Babylon::StrProblem> problems;
		Babylon::readStrFile("GUI:Ok\n\"OK\"\nEND\nGUI:Built\n\"%d units built\" = W01e\nEND\n", str, problems);
		Babylon::Database d;
		Babylon::FixedResolver resolver(Babylon::ChangeDecision::Keep);
		Babylon::updateDatabase(str, d, &resolver, true);
		return d;
	}();
	return db;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	const std::span<const std::uint8_t> bytes(data, size);
	std::string error;

	Babylon::Database db;
	if (Babylon::readDatabase(bytes, db, error))
	{
		const std::vector<std::uint8_t> written = Babylon::writeDatabase(db);
		Babylon::Database again;
		if (!Babylon::readDatabase(written, again, error) || Babylon::writeDatabase(again) != written)
		{
			std::fprintf(stderr, "written database not stable: %s\n", error.c_str());
			std::abort();
		}
	}

	Babylon::Sheet sheet;
	if (Babylon::readSheet(bytes, sheet, error))
	{
		Babylon::Database copy = sampleDb();
		Babylon::FixedVerifier verifier(true);
		Babylon::importSheet(copy, sheet, verifier);
		Babylon::markSent(copy, sheet, verifier);
	}

	Babylon::Database str;
	std::vector<Babylon::StrProblem> problems;
	Babylon::readStrFile(std::string_view(reinterpret_cast<const char *>(data), size), str, problems);

	std::vector<Babylon::StrOutLabel> labels;
	if (Babylon::readGameFile(bytes, labels, error))
	{
		Babylon::Database copy = sampleDb();
		Babylon::importGameFile(copy, Babylon::Lang::French, labels);
	}
	return 0;
}

#ifndef FUZZ_LIBFUZZER_MAIN
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
	{
		FILE *f = std::fopen(argv[i], "rb");
		if (!f)
		{
			std::printf("cannot read %s\n", argv[i]);
			return 2;
		}
		std::vector<uint8_t> buf;
		uint8_t tmp[65536];
		size_t n;
		while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
			buf.insert(buf.end(), tmp, tmp + n);
		std::fclose(f);
		LLVMFuzzerTestOneInput(buf.data(), buf.size());
	}
	return 0;
}
#endif
