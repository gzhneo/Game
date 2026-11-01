/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BabylonTests.cpp -- tests for the translation tool Babylon (Code/Tools/Editors/Babylon/Model and Core).
// No framework, like Code/Tests/EngineTests. Needs no game data and no office program: the reference
// database is assembled byte by byte the way the original tool wrote it, the generated .csf is read
// back with the game's own reader (Engine::readCsf).

#include "Core/Checks.h"
#include "Core/Exchange.h"
#include "Core/GameImport.h"
#include "Core/Generate.h"
#include "Core/Project.h"
#include "Core/Reports.h"
#include "Core/Update.h"
#include "Engine/TextFiles.h"
#include "Model/CsfFile.h"
#include "Model/Database.h"
#include "Model/DbFile.h"
#include "Model/Sheet.h"
#include "Model/StrFile.h"
#include "Model/Text.h"
#include "Model/Xml.h"
#include "Model/Zip.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Babylon;
namespace fs = std::filesystem;

static int s_failures = 0;
static int s_checks = 0;

static void check(bool condition, const char *what, int line)
{
	++s_checks;
	if (!condition)
	{
		++s_failures;
		std::printf("  FAILED (line %d): %s\n", line, what);
	}
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// ------------------------------------------------------------------ helpers

static std::u16string u(const char *utf8)
{
	return fromUtf8(utf8);
}

static Database parseStr(const std::string &text, std::vector<StrProblem> *problemsOut = nullptr)
{
	Database db;
	std::vector<StrProblem> problems;
	readStrFile(text, db, problems);
	if (problemsOut)
		*problemsOut = problems;
	return db;
}

// A database made from Generals.str the way the tool does it on its first update.
static Database freshDb(const Database &str)
{
	Database db;
	FixedResolver resolver(ChangeDecision::SkipLabel);
	updateDatabase(str, db, &resolver, true);
	return db;
}

static const char *SAMPLE_STR =
	"// Generals.str test\r\n"
	"// MAXLEN: 40\r\n"
	"\r\n"
	"GUI:Ok\r\n"
	"\"OK\"\r\n"
	"END\r\n"
	"\r\n"
	"// CONTEXT: Shown when a unit is built\r\n"
	"GUI:Built\r\n"
	"// COMMENT: %d is the number of units\r\n"
	"\"%d units built\"\r\n"
	"END\r\n"
	"\r\n"
	"DIALOG:Hello\r\n"
	"// SPEAKER: General\r\n"
	"\"Hello,   commander.\\nWelcome back!\" = HELLO01e\r\n"
	"END\r\n"
	"\r\n"
	"LONG:Text\r\n"
	"\"This text runs\r\n"
	"   // over two lines\"\r\n"
	"END\r\n";

// ------------------------------------------------------------------ text rules

static void testText()
{
	std::printf("text rules\n");
	const std::u16string japanese = u("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80");
	CHECK(fromUtf8(toUtf8(japanese)) == japanese);
	CHECK(japanese.size() == 6);	// three kanji, a space, a surrogate pair
	bool ok = true;
	fromUtf8("\xC3\x28", &ok);
	CHECK(!ok);
	CHECK(fromLatin1("\xE4\xF6\xFC") == u("\xC3\xA4\xC3\xB6\xC3\xBC"));
	int lost = 0;
	CHECK(toLatin1(u("a\xE6\x97\xA5" "b"), &lost) == "a?b" && lost == 1);
	CHECK(equalNoCase(u"GUI:Ok", u"gui:OK"));
	CHECK(!equalNoCase(u"GUI:Ok", u"GUI:Oka"));

	std::u16string s = u"a  b   c ";
	formatMetaString(s);
	CHECK(s == u"a b c");
	s = u"Line\\nNext";
	formatMetaString(s);
	CHECK(s == u"Line \\n Next");
	s = u"Line \\n  Next";
	formatMetaString(s);
	CHECK(s == u"Line \\n Next");
	// the original keeps its escape state after \n: a second \n right behind is plain text
	s = u"x\\n\\ny";
	formatMetaString(s);
	CHECK(s == u"x \\n \\ny");

	s = u"  a  b \n  c\t d ";
	stripSpaces(s);
	CHECK(s == u"a b\nc\td");
	s = u"one\\ntwo\\tthree\\\"q\\\\";
	convertMetaChars(s);
	CHECK(s == u"one\ntwo\tthree\"q\\");
	s = u"end\\";
	convertMetaChars(s);
	CHECK(s == u"end");

	CHECK(sameFormat(u"%d Einheiten", u"%d units"));
	CHECK(sameFormat(u"100%% fertig", u"100%% done"));
	CHECK(sameFormat(u"%s hat %d", u"%s has %d"));
	CHECK(!sameFormat(u"Einheiten", u"%d units"));
	CHECK(!sameFormat(u"%s %s", u"%s"));
	CHECK(!sameFormat(u"%d von %s", u"%s of %d"));
	CHECK(!sameFormat(u"%-5.2f", u"%f"));
	CHECK(formatSpecifiers(u"%ls and %S, 50%%").size() == 2);

	CHECK(jabber(u"Hello World") == u"Olleh Dlrow");
	CHECK(jabber(u"%d units\\n") == u"%d stinu\\n");
	CHECK(jabber(u"%") == u"%");
}

// ------------------------------------------------------------------ Generals.str

static void testStrFile()
{
	std::printf("Generals.str\n");
	std::vector<StrProblem> problems;
	const Database str = parseStr(SAMPLE_STR, &problems);
	CHECK(problems.empty());
	CHECK(str.labels.size() == 4);
	CHECK(str.nextId == -1);
	if (str.labels.size() != 4)
		return;
	const Label &ok = str.labels[0];
	CHECK(ok.name == u"GUI:Ok" && ok.line == 4 && ok.maxLen == 40);
	CHECK(ok.texts.size() == 1 && ok.texts[0].text == u"OK" && ok.texts[0].line == 5 && ok.texts[0].id == -1);
	const Label &built = str.labels[1];
	CHECK(built.context == u"Shown when a unit is built");
	CHECK(built.comment == u"%d is the number of units");
	CHECK(built.texts[0].text == u"%d units built");
	const Label &hello = str.labels[2];
	CHECK(hello.speaker == u"General");
	// the comment of the previous label does not carry over; the global context does
	CHECK(hello.comment.empty() && hello.context == u"Shown when a unit is built");
	CHECK(hello.texts[0].text == u"Hello, commander. \\n Welcome back!");
	CHECK(hello.texts[0].wave == u"HELLO01");	// a digit at the end stays
	const Label &longText = str.labels[3];
	CHECK(longText.texts[0].text == u"This text runs over two lines");
	CHECK(longText.line == 19 && longText.texts[0].line == 20);

	// a speech name ending in a letter loses it (the language letter)
	const Database w = parseStr("A\n\"x\" = abcE\nEND\nB\n// WAVE: local\n\"y\"\nEND\n");
	CHECK(w.labels.size() == 2 && w.labels[0].texts[0].wave == u"abc");
	CHECK(w.labels[1].texts[0].wave == u"local");

	problems.clear();
	parseStr("A\n\"open text\nEND\n", &problems);
	CHECK(problems.size() == 1 && problems[0].error && problems[0].message == "missing terminating quote");
	problems.clear();
	parseStr("A\n\"x\"\n", &problems);
	CHECK(problems.size() == 1 && problems[0].error);
	problems.clear();
	parseStr("A\nnot a text\n\"x\"\nEND\n", &problems);
	CHECK(problems.size() == 1 && !problems[0].error && problems[0].line == 2);

	// Latin-1 bytes are widened one to one
	const Database latin = parseStr("A\n\"Gr\xFC\xDF" "e\"\nEND\n");
	CHECK(latin.labels.size() == 1 && latin.labels[0].texts[0].text == u("Gr\xC3\xBC\xC3\x9F" "e"));

	// the game's reader sees the same labels
	std::vector<Engine::StrEntry> entries;
	CHECK(Engine::readStr(SAMPLE_STR, std::strlen(SAMPLE_STR), entries));
	CHECK(entries.size() == 4 && entries[2].label == "DIALOG:Hello");
}

static void testChecks()
{
	std::printf("checks\n");
	std::vector<StrProblem> problems;
	Database str = parseStr("A\n\"one\"\nEND\nA\n\"two\"\nEND\nB\n\"x\"\n\"y\"\nEND\nC\n\"z\"\n\"z\"\nEND\n"
							"// MAXLEN: 3\nD\n\"long\"\nEND\n// MAXLEN: 0\nE\n\"\"\nEND\ne\n\"w\"\nEND\n",
		&problems);
	const std::vector<Finding> errors = strErrors(str, problems);
	const std::vector<Finding> warnings = strWarnings(str, problems);
	// A twice, B and C with two texts, C with the same text twice, D too long
	CHECK(errors.size() == 5);
	// E empty, e differs from E only in case
	CHECK(warnings.size() == 2);
	bool sawDuplicate = false;
	for (const Finding &f : errors)
		sawDuplicate = sawDuplicate || (f.line == 4 && f.message.find("already defined on line 1") != std::string::npos);
	CHECK(sawDuplicate);
	CHECK(strErrors(parseStr(SAMPLE_STR), {}).empty());
}

// ------------------------------------------------------------------ main.db

// The bytes the original tool writes, built independently of DbFile.cpp.
struct IffBuilder
{
	std::vector<std::uint8_t> b;
	std::size_t form = 0;
	void be(std::uint32_t v)
	{
		for (int k = 3; k >= 0; --k)
			b.push_back(static_cast<std::uint8_t>(v >> (8 * k)));
	}
	void le(std::int32_t v)
	{
		for (int k = 0; k < 4; ++k)
			b.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * k)));
	}
	void id(const char *four)
	{
		for (int k = 0; k < 4; ++k)
			b.push_back(static_cast<std::uint8_t>(four[k]));
	}
	void beginForm(const char *four)
	{
		form = b.size();
		id("FORM");
		be(0);
		id(four);
	}
	void endForm()
	{
		const auto size = static_cast<std::uint32_t>(b.size() - form - 8);
		for (int k = 0; k < 4; ++k)
			b[form + 4 + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(size >> (8 * (3 - k)));
	}
	void info(const char *four, std::vector<std::int32_t> values)
	{
		id(four);
		be(static_cast<std::uint32_t>(values.size() * 4));
		for (const std::int32_t v : values)
			le(v);
	}
	void str(const char *four, const std::u16string &s)
	{
		id(four);
		be(static_cast<std::uint32_t>((s.size() + 1) * 2));
		for (const char16_t c : s)
		{
			b.push_back(static_cast<std::uint8_t>(c & 0xFF));
			b.push_back(static_cast<std::uint8_t>(c >> 8));
		}
		b.push_back(0);
		b.push_back(0);
	}
};

static std::vector<std::uint8_t> legacyDatabaseBytes()
{
	IffBuilder f;
	f.beginForm("NXDB");
	f.info("INFO", {2, 10003});
	f.endForm();
	// an obsolete text first
	f.beginForm("NTXT");
	f.info("INFO", {10000, 1});
	f.str("TEXT", u"Old text");
	f.endForm();
	f.beginForm("NLBL");
	f.info("INFO", {0});
	f.str("NAME", u"GUI:Ok");
	f.endForm();
	f.beginForm("NTXT");
	f.info("INFO", {10001, 2});
	f.str("TEXT", u"OK");
	f.endForm();
	f.beginForm("NTRN");
	f.info("INFO", {static_cast<std::int32_t>(Lang::German), 2});
	f.str("TEXT", u"OK");
	f.str("CMNT", u"same in German");
	f.endForm();
	f.beginForm("NTRN");
	f.info("INFO", {static_cast<std::int32_t>(Lang::Japanese), 1});
	f.str("TEXT", u("\xE4\xBA\x86\xE8\xA7\xA3"));
	f.endForm();
	f.beginForm("NLBL");
	f.info("INFO", {30});
	f.str("NAME", u"DIALOG:Hello");
	f.str("CMNT", u"Greeting");
	f.str("CTXT", u"Mission start");
	f.str("SPKR", u"General");
	f.str("LTNR", u"Player");
	f.endForm();
	f.beginForm("NTXT");
	f.info("INFO", {10002, 1});
	f.str("TEXT", u"Hello");
	f.str("WAVE", u"HELLO01");
	f.info("WVIN", {1, 12345, 0});
	f.endForm();
	return f.b;
}

static void testDatabaseFile()
{
	std::printf("main.db\n");
	const std::vector<std::uint8_t> legacy = legacyDatabaseBytes();
	Database db;
	std::string error;
	CHECK(readDatabase(legacy, db, error));
	CHECK(db.nextId == 10003);
	CHECK(db.obsolete.size() == 1 && db.obsolete[0].id == 10000 && db.obsolete[0].text == u"Old text");
	CHECK(db.labels.size() == 2);
	if (db.labels.size() == 2)
	{
		const Label &ok = db.labels[0];
		CHECK(ok.name == u"GUI:Ok" && ok.texts.size() == 1 && ok.texts[0].id == 10001 && ok.texts[0].revision == 2);
		CHECK(ok.texts[0].translations.size() == 2);
		const Translation *ge = ok.texts[0].translation(Lang::German);
		CHECK(ge && ge->revision == 2 && ge->comment == u"same in German");
		const Translation *ja = ok.texts[0].translation(Lang::Japanese);
		CHECK(ja && ja->text == u("\xE4\xBA\x86\xE8\xA7\xA3"));
		const Label &hello = db.labels[1];
		CHECK(hello.maxLen == 30 && hello.comment == u"Greeting" && hello.context == u"Mission start" && hello.speaker == u"General" &&
			hello.listener == u"Player");
		CHECK(hello.texts[0].wave == u"HELLO01" && hello.texts[0].waveInfo.valid && hello.texts[0].waveInfo.lo == 12345);
	}
	// written back byte for byte
	CHECK(writeDatabase(db) == legacy);

	// translations of obsolete texts survive (the original dropped them when saving)
	db.obsolete[0].addTranslation(Lang::French).text = u"Vieux";
	Database again;
	CHECK(readDatabase(writeDatabase(db), again, error));
	CHECK(again.obsolete.size() == 1 && again.obsolete[0].translation(Lang::French) && again.obsolete[0].translation(Lang::French)->text == u"Vieux");

	// broken files
	Database broken;
	std::vector<std::uint8_t> cut(legacy.begin(), legacy.begin() + 60);
	CHECK(!readDatabase(cut, broken, error));
	std::vector<std::uint8_t> notDb = legacy;
	notDb[8] = 'X';
	CHECK(!readDatabase(notDb, broken, error));
	CHECK(!readDatabase(std::vector<std::uint8_t>(), broken, error));
	std::vector<std::uint8_t> shortInfo = legacy;
	shortInfo[19] = 4;	// the INFO chunk of NXDB claims 4 bytes
	CHECK(!readDatabase(shortInfo, broken, error) && broken.labels.empty());
}

// ------------------------------------------------------------------ update

class CountingResolver : public UpdateResolver
{
public:
	explicit CountingResolver(ChangeDecision answer) : m_answer(answer) {}
	ChangeDecision changedText(const Label &, const Text &newText, const Text &oldText) override
	{
		++asked;
		lastNew = newText.text;
		lastOld = oldText.text;
		return m_answer;
	}
	int asked = 0;
	std::u16string lastNew;
	std::u16string lastOld;

private:
	ChangeDecision m_answer;
};

static void testUpdate()
{
	std::printf("update\n");
	const Database str = parseStr(SAMPLE_STR);
	Database db;
	const UpdateCounts dry = updateDatabase(str, db, nullptr, false);
	CHECK(dry.newLabels == 4 && dry.newStrings == 4 && db.labels.empty());
	CountingResolver never(ChangeDecision::SkipLabel);
	const UpdateCounts first = updateDatabase(str, db, &never, true);
	CHECK(first.newLabels == 4 && never.asked == 0);
	CHECK(db.labels.size() == 4 && db.labels[0].texts[0].id == 10000 && db.labels[3].texts[0].id == 10003 && db.nextId == 10004);
	CHECK(db.labels[1].comment == u"%d is the number of units");
	CHECK(db.modified);
	CHECK(updateDatabase(str, db, nullptr, false).changes == 0);

	// translate, then change the English text
	db.labels[1].texts[0].addTranslation(Lang::German) = Translation{Lang::German, u"%d Einheiten gebaut", u"", 1, {}, false};
	std::string changed = SAMPLE_STR;
	changed.replace(changed.find("%d units built"), 14, "%d units ready");
	const Database str2 = parseStr(changed);
	CHECK(updateDatabase(str2, db, nullptr, false).skippedLabels == 1);

	Database keep = db;
	CountingResolver keepIt(ChangeDecision::Keep);
	const UpdateCounts k = updateDatabase(str2, keep, &keepIt, true);
	CHECK(keepIt.asked == 1 && keepIt.lastNew == u"%d units ready" && keepIt.lastOld == u"%d units built");
	CHECK(k.modifiedStrings == 1 && k.skippedLabels == 0);
	CHECK(keep.labels[1].texts[0].text == u"%d units ready" && keep.labels[1].texts[0].revision == 1 && keep.labels[1].texts[0].id == 10001);
	CHECK(reportTranslations(keep, Lang::German).retranslate == 0);

	Database redo = db;
	CountingResolver redoIt(ChangeDecision::Retranslate);
	updateDatabase(str2, redo, &redoIt, true);
	CHECK(redo.labels[1].texts[0].revision == 2 && redo.labels[1].texts[0].id == 10001);
	CHECK(reportTranslations(redo, Lang::German).retranslate == 1);

	Database skip = db;
	CountingResolver skipIt(ChangeDecision::SkipLabel);
	const UpdateCounts s = updateDatabase(str2, skip, &skipIt, true);
	CHECK(s.skippedLabels == 1 && skip.labels[1].texts[0].text == u"%d units built");

	Database all = db;
	CountingResolver allIt(ChangeDecision::SkipAll);
	CHECK(updateDatabase(str2, all, &allIt, true).aborted);

	// a label leaves, another comes, a speech file and a comment change
	std::string moved = SAMPLE_STR;
	moved.replace(moved.find("GUI:Ok\r\n\"OK\"\r\nEND\r\n"), 18, "GUI:Cancel\r\n\"Cancel\"\r\nEND\r\n");
	moved.replace(moved.find("HELLO01e"), 8, "HELLO02e");
	moved.replace(moved.find("// SPEAKER: General"), 19, "// SPEAKER: Colonel");
	Database m = db;
	const UpdateCounts mc = updateDatabase(parseStr(moved), m, &never, true);
	CHECK(mc.newLabels == 1 && mc.deletedLabels == 1 && mc.deletedStrings == 1 && mc.updatedWaves == 1 && mc.updatedSpeakers == 1);
	CHECK(m.obsolete.size() == 1 && m.obsolete[0].text == u"OK" && m.obsolete[0].id == 10000);
	CHECK(m.labels.size() == 4 && m.labels.back().name == u"GUI:Cancel" && m.labels.back().texts[0].id == 10004);
	const std::vector<std::string> summary = updateSummary(mc);
	CHECK(!summary.empty() && summary[0] == "Added 1 new label");
}

// ------------------------------------------------------------------ sheets

static Sheet roundTrip(const Sheet &sheet, SheetFormat format)
{
	std::vector<std::uint8_t> data;
	writeSheet(sheet, format, data);
	Sheet back;
	std::string error;
	if (!readSheet(data, back, error))
		std::printf("  read %s: %s\n", sheetFormatName(format), error.c_str());
	return back;
}

static void testSheets()
{
	std::printf("sheet files\n");
	Sheet s;
	s.name = "German";
	s.headerRow = 1;
	s.protect = true;
	s.columns.resize(3);
	s.columns[1].editable = true;
	s.columns[2].hidden = true;
	const std::string tricky[] = {"plain", "  leading and trailing  ", "quote \" and , comma; semi", "a & b < c > d",
		"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xF0\x9F\x98\x80", "literal _x0041_ stays", "two\nlines\twith tab", "multiple    spaces", "=not a formula"};
	int row = 2;
	for (const std::string &t : tricky)
	{
		s.setText(row, 0, t);
		s.setNumber(row, 2, row * 1000 + 7);
		++row;
	}
	s.setFormula(row, 1, "LEN(A3)");
	s.setNumber(row + 1, 0, -1);

	for (const SheetFormat format : {SheetFormat::Xlsx, SheetFormat::Ods, SheetFormat::Csv})
	{
		const Sheet back = roundTrip(s, format);
		int r = 2;
		bool same = true;
		for (const std::string &t : tricky)
		{
			if (back.text(r, 0) != t || back.integer(r, 2, 0) != r * 1000 + 7)
			{
				std::printf("  %s row %d: \"%s\"\n", sheetFormatName(format), r, back.text(r, 0).c_str());
				same = false;
			}
			++r;
		}
		CHECK(same);
		CHECK(back.integer(row + 1, 0, 0) == -1);
		if (format != SheetFormat::Csv)
		{
			const Cell *f = back.get(row, 1);
			CHECK(f && f->kind == Cell::Kind::Formula);
			CHECK(back.name == "German");
		}
	}
	CHECK(sheetFormatFromData(writeOds(s)) == SheetFormat::Ods);
	CHECK(sheetFormatFromData(writeXlsx(s)) == SheetFormat::Xlsx);
	CHECK(sheetFormatFromName("x.XLSX") == SheetFormat::Xlsx && sheetFormatFromName("x.ods") == SheetFormat::Ods && sheetFormatFromName("x.xls") == SheetFormat::Unknown);
	CHECK(toOpenFormula("IF(K3>J3,\"Too long!\",\" \")") == "of:=IF([.K3]>[.J3];\"Too long!\";\" \")");
	CHECK(toOpenFormula("LEN(G12)") == "of:=LEN([.G12])");
	CHECK(columnName(0) == "A" && columnName(25) == "Z" && columnName(26) == "AA");

	// CSV as German Excel saves it: semicolons, Windows-1252
	Sheet csv;
	std::string error;
	const std::string ansi = "German;3\r\nx;\"a;b\";\x80 \xE4\r\n";
	CHECK(readCsv(std::vector<std::uint8_t>(ansi.begin(), ansi.end()), csv, error));
	CHECK(csv.text(0, 0) == "German" && csv.integer(0, 1, 0) == 3 && csv.text(1, 1) == "a;b" && csv.text(1, 2) == "\xE2\x82\xAC \xC3\xA4");

	// an .xlsx as other programs write it: inline and rich-text strings, prefixed namespace
	{
		ZipWriter zip;
		zip.add("[Content_Types].xml", "<Types/>");
		zip.add("xl/workbook.xml", "<x:workbook xmlns:x=\"m\" xmlns:r=\"r\"><x:sheets><x:sheet name=\"T\" sheetId=\"1\" r:id=\"rId1\"/></x:sheets></x:workbook>");
		zip.add("xl/_rels/workbook.xml.rels",
			"<Relationships><Relationship Id=\"rId1\" Type=\"x/worksheet\" Target=\"/xl/worksheets/s.xml\"/>"
			"<Relationship Id=\"rId2\" Type=\"x/sharedStrings\" Target=\"strings.xml\"/></Relationships>");
		zip.add("xl/strings.xml", "<sst><si><r><t>Rich </t></r><r><rPr/><t>text</t></r><rPh><t>ignored</t></rPh></si></sst>");
		zip.add("xl/worksheets/s.xml",
			"<x:worksheet xmlns:x=\"m\"><x:sheetData><x:row r=\"1\"><x:c r=\"A1\" t=\"s\"><x:v>0</x:v></x:c>"
			"<x:c r=\"C1\" t=\"inlineStr\"><x:is><x:t>inline &amp; &#x4E2D;</x:t></x:is></x:c><x:c r=\"D1\"><x:v>42</x:v></x:c></x:row>"
			"<x:row r=\"3\"><x:c r=\"B3\" t=\"str\"><x:f>A1</x:f><x:v>Rich text</x:v></x:c></x:row></x:sheetData></x:worksheet>");
		Sheet x;
		CHECK(readXlsx(zip.finish(), x, error));
		CHECK(x.name == "T" && x.text(0, 0) == "Rich text" && x.text(0, 2) == "inline & \xE4\xB8\xAD" && x.integer(0, 3, 0) == 42 && x.text(2, 1) == "Rich text");
	}
	// an .ods as LibreOffice writes it: repeated rows and cells, spaces as elements
	{
		ZipWriter zip;
		zip.add("mimetype", "application/vnd.oasis.opendocument.spreadsheet", true);
		zip.add("content.xml",
			"<office:document-content xmlns:office=\"o\" xmlns:table=\"t\" xmlns:text=\"x\"><office:body><office:spreadsheet>"
			"<table:table table:name=\"L\"><table:table-column table:number-columns-repeated=\"1024\"/>"
			"<table:table-row><table:table-cell office:value-type=\"string\"><text:p>a<text:s text:c=\"3\"/>b</text:p><text:p>c</text:p></table:table-cell>"
			"<table:table-cell table:number-columns-repeated=\"2\"/><table:table-cell office:value-type=\"float\" office:value=\"7\" calcext:value-type=\"float\"><text:p>7</text:p></table:table-cell>"
			"<table:table-cell table:number-columns-repeated=\"1020\"/></table:table-row>"
			"<table:table-row table:number-rows-repeated=\"3\"><table:table-cell table:number-columns-repeated=\"1024\"/></table:table-row>"
			"<table:table-row><table:table-cell office:value-type=\"string\" table:number-columns-repeated=\"2\"><text:p><text:span>sp</text:span>an<text:tab/>x</text:p></table:table-cell></table:table-row>"
			"<table:table-row table:number-rows-repeated=\"1048570\"><table:table-cell table:number-columns-repeated=\"1024\"/></table:table-row>"
			"</table:table></office:spreadsheet></office:body></office:document-content>");
		Sheet o;
		CHECK(readOds(zip.finish(), o, error));
		CHECK(o.name == "L" && o.text(0, 0) == "a   b\nc" && o.integer(0, 3, 0) == 7 && o.text(4, 0) == "span\tx" && o.text(4, 1) == "span\tx");
		CHECK(o.rowCount() == 5);
	}
	// damaged input is refused, never crashes
	CHECK(!readSheet(std::vector<std::uint8_t>{'P', 'K', 3, 4, 0, 0}, csv, error));
	const std::string xls = std::string("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
	CHECK(!readSheet(std::vector<std::uint8_t>(xls.begin(), xls.end()), csv, error));
	XmlNode node;
	CHECK(!parseXml("<a><b></a>", node, error));
	CHECK(!parseXml("<a x=1/>", node, error));
	std::string deep;
	for (int i = 0; i < 100000; ++i)
		deep += "<p>";
	CHECK(!parseXml(deep, node, error) && error == "elements nested too deeply");
	CHECK(parseXml("<?xml version=\"1.0\"?><!-- c --><a x='1'><![CDATA[<raw>]]></a>", node, error) && node.allText() == "<raw>" && *node.attribute("x") == "1");
}

// ------------------------------------------------------------------ export and import

static void testExchange()
{
	std::printf("export and import\n");
	Database db = freshDb(parseStr(SAMPLE_STR));
	const DialogFiles noFiles;
	ExportOptions options;
	CHECK(exportCount(db, Lang::German, options, noFiles) == 4);
	Sheet sheet = exportSheet(db, Lang::German, options, noFiles);
	CHECK(sheet.text(0, 0) == "German" && sheet.integer(0, 1, 0) == 4);
	CHECK(sheet.text(1, 6) == "German Translation" && sheet.columns[6].editable && sheet.columns[12].hidden && sheet.protect);
	CHECK(sheet.integer(2, 4, 0) == 10000 && sheet.text(2, 1) == "GUI:Ok" && sheet.text(2, 5) == "OK" && sheet.integer(2, 9, 0) == 40);
	CHECK(sheet.text(4, 0) == "HELLO01G.WAV" && sheet.text(4, 2) == "General");
	CHECK(sheet.get(2, 10) && sheet.get(2, 10)->formula == "LEN(G3)");
	CHECK(sheet.integer(6, 4, 0) == -1);

	// the translator fills in G; every format brings it back
	for (const SheetFormat format : {SheetFormat::Xlsx, SheetFormat::Ods, SheetFormat::Csv})
	{
		Database copy = db;
		Sheet filled = sheet;
		filled.setText(2, 6, "OK");
		filled.setText(3, 6, "%d Einheiten gebaut");
		filled.setText(4, 6, "Hallo, Kommandant.\nWillkommen!");
		const Sheet back = roundTrip(filled, format);
		FixedVerifier verifier(false);
		const ImportResult r = importSheet(copy, back, verifier);
		CHECK(r.ok && r.lang == Lang::German && r.found == 4 && r.added == 3 && r.updated == 3 && r.missing == 1);
		const Translation *t = copy.labels[2].texts[0].translation(Lang::German);
		CHECK(t && t->text == u"Hallo, Kommandant.\\nWillkommen!" && t->revision == 1);
		CHECK(copy.modified);
		// importing the same sheet again changes nothing
		copy.modified = false;
		const ImportResult again = importSheet(copy, back, verifier);
		CHECK(again.ok && again.updated == 0 && !copy.modified);
		// the changes filter now leaves out what is translated
		CHECK(exportCount(copy, Lang::German, options, noFiles) == 1);
	}

	// English corrected in the database without a new revision: the label confirms the row
	{
		Database copy = db;
		Sheet filled = sheet;
		filled.setText(3, 6, "%d gebaut");
		copy.labels[1].texts[0].text = u"%d units were built";
		FixedVerifier never(false);
		const ImportResult r = importSheet(copy, filled, never);
		CHECK(r.ok && r.updated == 1);
	}
	// a row whose label does not match: the verifier decides
	{
		Database copy = db;
		Sheet filled = sheet;
		filled.setText(3, 6, "%d gebaut");
		filled.setText(3, 1, "OTHER:Label");
		filled.setText(3, 5, "something else");
		copy.modified = false;
		FixedVerifier no(false);
		const ImportResult bad = importSheet(copy, filled, no);
		CHECK(!bad.ok && bad.badIds && !copy.modified);
		FixedVerifier yes(true);
		CHECK(importSheet(copy, filled, yes).updated == 1);
	}
	// revisions: older rows are left out, newer ones are errors
	{
		Database copy = db;
		Sheet filled = sheet;
		filled.setText(2, 6, "OK");
		filled.setNumber(2, 12, 0);
		filled.setText(3, 6, "x");
		filled.setNumber(3, 12, 5);
		FixedVerifier v(false);
		const ImportResult r = importSheet(copy, filled, v);
		CHECK(r.ok && r.unmatched == 1 && r.errors == 1 && r.updated == 0);
	}
	// not a translation sheet
	{
		Sheet other;
		other.setText(0, 0, "Klingon");
		FixedVerifier v(false);
		CHECK(!importSheet(db, other, v).ok);
	}
	// marking as sent
	{
		Database copy = db;
		FixedVerifier v(false);
		const ImportResult r = markSent(copy, sheet, v);
		CHECK(r.ok && r.matched == 4);
		ExportOptions unsent;
		unsent.filter = ExportFilter::Unsent;
		CHECK(exportCount(copy, Lang::German, unsent, noFiles) == 0);
	}
	// too long and format errors are flagged in the comment column
	{
		Database copy = db;
		copy.labels[1].texts[0].addTranslation(Lang::French) = Translation{Lang::French, u"%s unites", u"", 1, {}, false};
		copy.labels[0].texts[0].addTranslation(Lang::French) = Translation{Lang::French, std::u16string(50, u'x'), u"", 1, {}, false};
		const Sheet fr = exportSheet(copy, Lang::French, options, noFiles);
		CHECK(fr.text(2, 8) == "ERROR: too long" && fr.text(3, 8) == "ERROR: bad format" && fr.text(3, 6) == "%s unites");
		const TranslationReport rep = reportTranslations(copy, Lang::French);
		CHECK(rep.tooLong == 1 && rep.badFormat == 1 && rep.missing == 2);
	}
	// the test language is filled with reversed words
	{
		const Sheet jb = exportSheet(db, Lang::Jabber, options, noFiles);
		CHECK(jb.text(3, 6) == "%d stinu tliub");
	}
	ExportOptions sample;
	sample.filter = ExportFilter::Sample;
	CHECK(exportCount(db, Lang::German, sample, noFiles) == 4);
	ExportOptions dialog;
	dialog.filter = ExportFilter::Dialog;
	CHECK(exportCount(db, Lang::German, dialog, noFiles) == 1);
	dialog.filter = ExportFilter::NonDialog;
	CHECK(exportCount(db, Lang::German, dialog, noFiles) == 3);
}

// ------------------------------------------------------------------ game files

static void testGenerate()
{
	std::printf("game files\n");
	Database db = freshDb(parseStr(SAMPLE_STR));
	db.labels[2].texts[0].addTranslation(Lang::German) = Translation{Lang::German, u"Hallo,  Kommandant. \\n Willkommen!", u"", 1, {}, false};
	db.labels[0].texts[0].addTranslation(Lang::Japanese) = Translation{Lang::Japanese, u("\xE4\xBA\x86\xE8\xA7\xA3"), u"", 1, {}, false};
	const DialogFiles noFiles;
	GenerateOptions options;
	CHECK(gameFileName("Generals", Lang::German, GameFileFormat::Csf) == "generals_ge.csf");

	const GeneratedFile ge = generateGameFile(db, "Generals", Lang::German, options, noFiles);
	CHECK(ge.ok && ge.fileName == "generals_ge.csf");
	int version = 0;
	int language = 0;
	std::vector<Engine::CsfEntry> entries;
	CHECK(Engine::readCsf(ge.data.data(), ge.data.size(), version, language, entries) == Engine::CsfError::None);
	CHECK(version == 3 && language == static_cast<int>(Lang::German) && entries.size() == 4);
	if (entries.size() == 4)
	{
		CHECK(entries[0].label == "GUI:Ok" && entries[0].text == u"10000");	// untranslated: the string ID
		// as in the original: surplus spaces go, the ones around an escaped line break stay
		CHECK(entries[2].text == u"Hallo, Kommandant. \n Willkommen!" && entries[2].speech == "HELLO01g");
	}
	CHECK(!ge.warnings.empty());

	options.untranslated = Untranslated::UseOriginal;
	const GeneratedFile ja = generateGameFile(db, "Generals", Lang::Japanese, options, noFiles);
	CHECK(Engine::readCsf(ja.data.data(), ja.data.size(), version, language, entries) == Engine::CsfError::None);
	CHECK(entries.size() == 4 && entries[0].text == u("\xE4\xBA\x86\xE8\xA7\xA3") && entries[1].text == u"%d units built");

	const GeneratedFile us = generateGameFile(db, "Generals", Lang::US, options, noFiles);
	CHECK(Engine::readCsf(us.data.data(), us.data.size(), version, language, entries) == Engine::CsfError::None);
	CHECK(entries.size() == 4 && entries[2].text == u"Hello, commander. \n Welcome back!" && entries[2].speech == "HELLO01e");

	// the text form reads in the game's reader and again in Babylon
	options.format = GameFileFormat::Str;
	options.generatedOn = "test";
	const GeneratedFile str = generateGameFile(db, "Generals", Lang::German, options, noFiles);
	CHECK(str.ok && str.fileName == "generals_ge.str");
	std::vector<Engine::StrEntry> strEntries;
	CHECK(Engine::readStr(reinterpret_cast<const char *>(str.data.data()), str.data.size(), strEntries));
	CHECK(strEntries.size() == 4 && strEntries[2].text == "Hallo,  Kommandant. \\n Willkommen!");
	const Database reread = parseStr(std::string(str.data.begin(), str.data.end()));
	CHECK(reread.labels.size() == 4 && reread.labels[2].texts[0].wave == u"HELLO01");
	const GeneratedFile strJa = generateGameFile(db, "Generals", Lang::Japanese, options, noFiles);
	CHECK(!strJa.warnings.empty() && strJa.warnings[0].find("Latin-1") != std::string::npos);

	// a label without a name cannot go into a .csf
	Database nameless = db;
	nameless.labels[0].name.clear();
	options.format = GameFileFormat::Csf;
	CHECK(!generateGameFile(nameless, "Generals", Lang::US, options, noFiles).ok);
}

// ------------------------------------------------------------------ files of the game

static void testGameImport()
{
	std::printf("game files as source\n");
	Database db = freshDb(parseStr(SAMPLE_STR));
	db.labels[2].texts[0].addTranslation(Lang::French) = Translation{Lang::French, u"Bonjour, commandant. \\n Bienvenue !", u"", 1, {}, false};
	const DialogFiles noFiles;
	GenerateOptions options;
	const GeneratedFile fr = generateGameFile(db, "Generals", Lang::French, options, noFiles);

	// the English .csf becomes Generals.str again
	const GeneratedFile us = generateGameFile(db, "Generals", Lang::US, options, noFiles);
	std::vector<StrOutLabel> labels;
	std::string error;
	CHECK(readGameFile(us.data, labels, error) && labels.size() == 4);
	const std::string str = writeStrFile(labels, "test", "now");
	const Database again = parseStr(str);
	CHECK(again.labels.size() == 4 && again.labels[2].texts[0].text == db.labels[2].texts[0].text && again.labels[2].texts[0].wave == u"HELLO01");
	// a .csf has no comments, contexts or MAXLEN; labels, texts and speech files are the same
	const UpdateCounts diff = updateDatabase(again, db, nullptr, false);
	CHECK(diff.newLabels == 0 && diff.deletedLabels == 0 && diff.newStrings == 0 && diff.deletedStrings == 0 && diff.skippedLabels == 0 &&
		diff.updatedWaves == 0 && diff.modifiedStrings == 0 && diff.updatedContexts == 3 && diff.updatedMaxLen == 4);

	// a language file fills the translations, matched by label
	Database target = freshDb(parseStr(SAMPLE_STR));
	CHECK(readGameFile(fr.data, labels, error) && labels.size() == 4);
	GameImportResult r = importGameFile(target, Lang::French, labels);
	// three texts carry their string ID (untranslated), one is French
	CHECK(r.ok && r.labels == 4 && r.added == 1 && r.skipped == 3);
	const Translation *t = target.labels[2].texts[0].translation(Lang::French);
	CHECK(t && t->text == u"Bonjour, commandant. \\n Bienvenue !" && t->revision == 1);
	r = importGameFile(target, Lang::French, labels);
	CHECK(r.ok && r.unchanged == 1 && r.updated == 0);
	CHECK(!importGameFile(target, Lang::US, labels).ok);

	// a .str file works the same; unknown labels are counted
	CHECK(readGameFile(std::vector<std::uint8_t>{'X', '\n', '"', 'x', '"', '\n', 'E', 'N', 'D', '\n'}, labels, error) && labels.size() == 1);
	CHECK(importGameFile(target, Lang::French, labels).unknown == 1);
	std::vector<std::uint8_t> brokenCsf = fr.data;
	brokenCsf.resize(30);
	CHECK(!readGameFile(brokenCsf, labels, error));

	// Quotes, backslashes and a line break without a space before it survive .csf -> .str -> database -> .csf.
	const std::vector<StrOutLabel> special = {{u"GUI:HostWantsToStart", {{u"Click \\\"Accept\\\".", u"host"}}},
		{u"OBJECT:Aurora", {{u"Aurora-\\n Bomber", u""}}}, {u"GUI:Path", {{u"C:\\\\Games\\\\", u""}}}, {u"GUI:GSErrorTitle", {{u"Error", u""}}}};
	std::vector<std::uint8_t> csf;
	CHECK(writeCsfFile(special, *langInfo(Lang::German), csf, error));
	std::vector<Engine::CsfEntry> originalEntries;
	int version = 0;
	int language = 0;
	CHECK(Engine::readCsf(csf.data(), csf.size(), version, language, originalEntries) == Engine::CsfError::None);
	CHECK(originalEntries.size() == 4 && originalEntries[0].text == u"Click \"Accept\"." && originalEntries[1].text == u"Aurora-\n Bomber"
		&& originalEntries[2].text == u"C:\\Games\\");
	CHECK(readGameFile(csf, labels, error) && labels.size() == 4);
	std::vector<StrProblem> problems;
	const Database fromCsf = parseStr(writeStrFile(labels, "test", "now"), &problems);
	CHECK(problems.empty() && fromCsf.labels.size() == 4);
	CHECK(fromCsf.labels.size() == 4 && fromCsf.labels[0].texts[0].text == u"Click \\\"Accept\\\"." && fromCsf.labels[0].texts[0].wave == u"host");
	Database specialDb = freshDb(fromCsf);
	// the translation of the last text is empty: it stays empty, it does not become the string ID
	labels[3].texts[0].text.clear();
	originalEntries[3].text.clear();
	r = importGameFile(specialDb, Lang::German, labels);
	CHECK(r.ok && r.added == 4);
	const GeneratedFile back = generateGameFile(specialDb, "Generals", Lang::German, options, noFiles);
	std::vector<Engine::CsfEntry> backEntries;
	CHECK(back.ok && Engine::readCsf(back.data.data(), back.data.size(), version, language, backEntries) == Engine::CsfError::None);
	bool same = backEntries.size() == originalEntries.size();
	for (std::size_t i = 0; same && i < backEntries.size(); ++i)
		same = backEntries[i].label == originalEntries[i].label && backEntries[i].text == originalEntries[i].text && backEntries[i].speech == originalEntries[i].speech;
	CHECK(same);
	// written by hand without backslash: the game ends the text at the inner quote, the check says so
	problems.clear();
	parseStr("A\n\"Click \"Accept\".\"\nEND\n", &problems);
	CHECK(problems.size() == 1 && !problems[0].error);
}

// The project's own texts (Data/Text): english.str as master, german.str as translation; the generated
// German file holds the same texts.
static void testProjectTexts(const fs::path &root)
{
	std::printf("project texts (Data/Text)\n");
	std::vector<std::uint8_t> english;
	std::vector<std::uint8_t> german;
	std::string error;
	const bool readable = readFile(root / "Data" / "Text" / "english.str", english, error) && readFile(root / "Data" / "Text" / "german.str", german, error);
	CHECK(readable);
	if (!readable)
		return;
	std::vector<StrProblem> problems;
	const Database str = parseStr(std::string(english.begin(), english.end()), &problems);
	CHECK(!str.labels.empty() && strErrors(str, problems).empty() && strWarnings(str, problems).empty());
	Database db = freshDb(str);
	std::vector<StrOutLabel> labels;
	CHECK(readGameFile(german, labels, error));
	const GameImportResult r = importGameFile(db, Lang::German, labels);
	CHECK(r.ok && r.unknown == 0 && r.added == static_cast<int>(str.labels.size()));
	CHECK(reportTranslations(db, Lang::German).problems() == 0);

	GenerateOptions options;
	options.format = GameFileFormat::Str;
	const GeneratedFile ge = generateGameFile(db, "project", Lang::German, options, DialogFiles());
	std::vector<Engine::StrEntry> generated;
	std::vector<Engine::StrEntry> original;
	CHECK(Engine::readStr(reinterpret_cast<const char *>(ge.data.data()), ge.data.size(), generated));
	CHECK(Engine::readStr(reinterpret_cast<const char *>(german.data()), german.size(), original));
	CHECK(generated.size() == original.size());
	std::map<std::string, std::string> byLabel;
	for (const Engine::StrEntry &e : generated)
		byLabel[e.label] = e.text;
	int same = 0;
	for (const Engine::StrEntry &e : original)
	{
		std::u16string expected = fromLatin1(e.text);
		formatMetaString(expected);
		if (byLabel.count(e.label) && fromLatin1(byLabel[e.label]) == expected)
			++same;
		else
			std::printf("  differs: %s\n", e.label.c_str());
	}
	CHECK(same == static_cast<int>(original.size()));
	// and the sheet round trip keeps every text
	const Sheet sheet = exportSheet(db, Lang::German, ExportOptions{ExportFilter::All, true, true}, DialogFiles());
	Database copy = freshDb(str);
	FixedVerifier v(false);
	const ImportResult back = importSheet(copy, roundTrip(sheet, SheetFormat::Xlsx), v);
	CHECK(back.ok && back.added == static_cast<int>(str.labels.size()));
	bool equal = true;
	for (std::size_t l = 0; l < db.labels.size(); ++l)
		equal = equal && copy.labels[l].texts[0].translation(Lang::German) &&
			copy.labels[l].texts[0].translation(Lang::German)->text == db.labels[l].texts[0].translation(Lang::German)->text;
	CHECK(equal);
}

// ------------------------------------------------------------------ files on disk

static void writeBytes(const fs::path &p, const std::string &content)
{
	fs::create_directories(p.parent_path());
	std::ofstream out(p, std::ios::binary);
	out << content;
}

static void testProjectAndDialog()
{
	std::printf("project and speech files\n");
	const fs::path root = fs::temp_directory_path() / "babylon-tests";
	std::error_code ec;
	fs::remove_all(root, ec);
	writeBytes(root / "Data" / "Generals.str", SAMPLE_STR);
	writeBytes(root / "dialog" / "E" / "hello01E.WAV", std::string(100, 'w'));
	writeBytes(root / "dialog" / "g" / "HELLO01g.wav", std::string(80, 'w'));

	Project p;
	p.paths = ProjectPaths::inFolder(root);
	std::vector<std::string> log;
	p.log = [&log](const std::string &line) { log.push_back(line); };
	p.load();
	CHECK(p.strLoaded && !p.dbFound && !p.dbBroken && p.errors.empty() && p.strOutOfDate());
	FixedResolver resolver(ChangeDecision::SkipLabel);
	p.update(resolver);
	CHECK(!p.strOutOfDate() && p.db.labels.size() == 4);
	std::string error;
	CHECK(p.save(error));
	CHECK(fs::is_regular_file(p.paths.db));
	CHECK(!fs::exists(backupPath(p.paths.db)));
	CHECK(backupPath(p.paths.db).filename() == "main_back_up.db");

	// speech files are found in any letter case
	const DialogFiles files = p.dialogFiles();
	Text &hello = p.db.labels[2].texts[0];
	CHECK(files.size(hello, *langInfo(Lang::US)).value_or(0) == 100);
	CHECK(files.size(hello, *langInfo(Lang::German)).value_or(0) == 80);
	CHECK(!files.size(hello, *langInfo(Lang::French)));
	DialogReport dr = reportDialog(p.db, Lang::US, files);
	CHECK(dr.dialog == 1 && dr.unresolved == 1);
	CHECK(confirmDialog(hello, Lang::US, files));
	dr = reportDialog(p.db, Lang::US, files);
	CHECK(dr.resolved == 1 && dr.unresolved == 0);
	CHECK(!confirmDialog(hello, Lang::German, files));	// no German translation yet
	dr = reportDialog(p.db, Lang::French, files);
	CHECK(dr.missing == 1);
	// a changed recording loses its confirmation
	writeBytes(root / "dialog" / "E" / "hello01E.WAV", std::string(120, 'w'));
	DialogFiles fresh = p.dialogFiles();
	recheckDialog(p.db, Lang::US, fresh);
	CHECK(!hello.waveInfo.valid);
	CHECK(confirmDialog(hello, Lang::US, fresh));
	invalidateDialog(p.db, Lang::US);
	CHECK(!hello.waveInfo.valid);

	// saved and loaded again; a second save leaves a backup
	CHECK(p.save(error));
	CHECK(fs::exists(backupPath(p.paths.db)));
	Project q;
	q.paths = p.paths;
	q.load();
	CHECK(q.dbLoaded && q.db.labels.size() == 4 && q.db.nextId == 10004 && !q.strOutOfDate());

	ReportOptions ro;
	ro.dialog = true;
	const std::string report = reportText(q.db, {Lang::US, Lang::German}, ro, q.dialogFiles(), "now");
	CHECK(report.find("German Status:") != std::string::npos && report.find("Not translated: 4") != std::string::npos);
	CHECK(report.find("Total dialog: 1") != std::string::npos);

	// a broken database is not overwritten
	writeBytes(p.paths.db, "garbage");
	Project r;
	r.paths = p.paths;
	r.load();
	CHECK(r.dbBroken && !r.canSave() && !r.save(error));
	fs::remove_all(root, ec);
}

// With the repository folder as argument the project's own texts (Data/Text) are checked as well.
int main(int argc, char **argv)
{
	std::printf("BabylonTests\n\n");
	testText();
	testStrFile();
	testChecks();
	testDatabaseFile();
	testUpdate();
	testSheets();
	testExchange();
	testGenerate();
	testGameImport();
	testProjectAndDialog();
	if (argc > 1)
		testProjectTexts(pathFromText(argv[1]));
	std::printf("\n%d checks, %d failed\n", s_checks, s_failures);
	return s_failures ? 1 : 0;
}
