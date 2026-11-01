/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// BabylonTool.cpp -- the translation database on the command line: everything the Babylon
// application does, without questions, for scripts and tests. "BabylonTool --help" lists the commands.

#include "Core/Checks.h"
#include "Core/Exchange.h"
#include "Core/GameImport.h"
#include "Core/Generate.h"
#include "Core/Project.h"
#include "Core/Reports.h"
#include "Core/Update.h"
#include "Model/Sheet.h"
#include "Model/StrFile.h"
#include "Model/Text.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace Babylon;
namespace fs = std::filesystem;

namespace
{

const char *USAGE =
	"BabylonTool -- translation database of Generals / Zero Hour (Generals.str, main.db, .csf)\n"
	"\n"
	"  BabylonTool [files] <command> [arguments]\n"
	"\n"
	"Files (default: Data/Generals.str, Data/main.db and dialog/ below the current folder):\n"
	"  --project <folder>   the same below another folder\n"
	"  --str <file>         Generals.str\n"
	"  --db <file>          main.db\n"
	"  --dialog <folder>    recorded speech, <folder>/<letter>/<name><letter>.wav\n"
	"\n"
	"Commands:\n"
	"  status                        state of both files and what an update would change\n"
	"  check                         errors and warnings of Generals.str (exit 1 on errors)\n"
	"  update [--changed retranslate|keep|skip]\n"
	"                                bring main.db in line with Generals.str and save it; a changed text\n"
	"                                needs a new translation (retranslate), keeps it (keep) or waits (skip,\n"
	"                                default)\n"
	"  export <language> <file> [--filter <filter>] [--with-translations] [--no-protect]\n"
	"                                translation sheet .xlsx, .ods or .csv; filters: changes (default), all,\n"
	"                                dialog, nondialog, sample, missing-dialog, unverified, unsent\n"
	"  import <file> [--trust-ids]   take the translations of a sheet and save main.db; --trust-ids takes\n"
	"                                rows whose English differs from the database without asking\n"
	"  generate <prefix> [--format csf|str] [--untranslated ids|original] [--out <folder>] <language>...|all\n"
	"                                game files <prefix>_<xx>.csf (or .str)\n"
	"  report <file> [--translations] [--dialog] [--limit <n>] <language>...|all\n"
	"  verify <language>             state of the translations and the speech files of a language\n"
	"  reset-dialog <language>       forget which speech files were confirmed, save main.db\n"
	"  import-game <language> <file> translations from a game file (.csf or .str of that language),\n"
	"                                matched by label; saves main.db\n"
	"  csf-to-str <in.csf> <out.str> a .csf as text, e.g. the English one as Generals.str to start from\n"
	"  dump [<language>]             the database as text\n"
	"  convert <in> <out>            a sheet into another format (.xlsx, .ods, .csv)\n"
	"\n"
	"Languages: US, UK, German, French, Spanish, Italian, Japanese, Korean, Chinese, Jabberwockie\n"
	"(or us, uk, ge, fr, sp, it, ja, ko, ch, jb).\n";

int usage(const std::string &message)
{
	std::cerr << "BabylonTool: " << message << "\n\n" << USAGE;
	return 2;
}

std::string now()
{
	const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	return buf;
}

bool languageArg(const std::string &arg, std::vector<Lang> &out)
{
	if (arg == "all")
	{
		for (const LangInfo &info : languages())
			out.push_back(info.id);
		return true;
	}
	const LangInfo *info = findLanguage(arg);
	if (!info)
		return false;
	out.push_back(info->id);
	return true;
}

// Loads the project; false if the database cannot be used.
bool open(Project &p, bool needStr)
{
	p.log = [](const std::string &line) { std::cout << line << "\n"; };
	p.load();
	if (p.dbBroken)
		return false;
	if (needStr && !p.strLoaded)
		return false;
	return true;
}

bool save(Project &p)
{
	std::string error;
	if (!p.save(error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return false;
	}
	return true;
}

int cmdStatus(Project &p)
{
	open(p, false);
	if (p.strLoaded)
	{
		std::cout << "Generals.str: " << p.str.labels.size() << " labels, " << p.errors.size() << " errors, " << p.warnings.size() << " warnings\n";
		if (!p.errors.empty())
			std::cout << "The database cannot be updated until the errors are fixed (BabylonTool check).\n";
	}
	if (p.dbLoaded || !p.dbFound)
		std::cout << "main.db: " << p.db.labels.size() << " labels, " << p.db.textCount() << " texts, " << p.db.obsolete.size()
				  << " obsolete, next string ID " << p.db.nextId << (p.dbReadOnly ? ", read only" : "") << "\n";
	if (p.strOutOfDate())
	{
		std::cout << "main.db is not up to date with Generals.str:\n";
		for (const std::string &line : updateSummary(p.pending))
			std::cout << "  " << line << "\n";
	}
	else if (p.strLoaded && p.errors.empty())
		std::cout << "main.db is up to date with Generals.str.\n";
	for (const LangInfo &info : languages())
	{
		if (info.id == Lang::US || info.id == Lang::Jabber)
			continue;
		const TranslationReport r = reportTranslations(p.db, info.id);
		if (r.translated || r.retranslate)
			std::cout << info.name << ": " << r.translated << " of " << r.strings << " translated, " << r.retranslate << " to redo, " << r.missing << " missing\n";
	}
	return p.dbBroken ? 1 : 0;
}

int cmdCheck(Project &p)
{
	open(p, true);
	if (!p.strLoaded)
		return 1;
	for (const Finding &f : p.errors)
		std::cout << findingText(f) << "\n";
	for (const Finding &f : p.warnings)
		std::cout << findingText(f) << "\n";
	std::cout << "Total errors: " << p.errors.size() << ", warnings: " << p.warnings.size() << "\n";
	return p.errors.empty() ? 0 : 1;
}

int cmdUpdate(Project &p, const std::vector<std::string> &args)
{
	ChangeDecision answer = ChangeDecision::SkipLabel;
	for (std::size_t i = 0; i < args.size(); ++i)
	{
		if (args[i] == "--changed" && i + 1 < args.size())
		{
			const std::string &v = args[++i];
			if (v == "retranslate")
				answer = ChangeDecision::Retranslate;
			else if (v == "keep")
				answer = ChangeDecision::Keep;
			else if (v == "skip")
				answer = ChangeDecision::SkipLabel;
			else
				return usage("--changed takes retranslate, keep or skip");
		}
		else
			return usage("unknown argument " + args[i]);
	}
	if (!open(p, true))
		return 1;
	if (!p.errors.empty())
	{
		for (const Finding &f : p.errors)
			std::cout << findingText(f) << "\n";
		std::cerr << "BabylonTool: Generals.str has errors; fix them before updating.\n";
		return 1;
	}
	if (!p.canUpdate())
	{
		std::cerr << "BabylonTool: the database cannot be updated (read only?).\n";
		return 1;
	}
	FixedResolver resolver(answer);
	const UpdateCounts counts = p.update(resolver);
	if (answer == ChangeDecision::SkipLabel && !resolver.asked.empty())
	{
		std::cout << "Changed texts left as they were (decide with --changed retranslate or keep):\n";
		for (const std::string &label : resolver.asked)
			std::cout << "  " << label << "\n";
	}
	if (!save(p))
		return 1;
	return counts.skippedLabels ? 1 : 0;
}

int cmdExport(Project &p, const std::vector<std::string> &args)
{
	if (args.size() < 2)
		return usage("export needs a language and a file");
	std::vector<Lang> langs;
	if (!languageArg(args[0], langs) || langs.size() != 1)
		return usage("unknown language " + args[0]);
	const fs::path file = pathFromText(args[1]);
	const SheetFormat format = sheetFormatFromName(args[1]);
	if (format == SheetFormat::Unknown)
		return usage("the file must end in .xlsx, .ods or .csv");
	ExportOptions options;
	for (std::size_t i = 2; i < args.size(); ++i)
	{
		if (args[i] == "--filter" && i + 1 < args.size())
		{
			if (!parseExportFilter(args[++i], options.filter))
				return usage("unknown filter " + args[i]);
		}
		else if (args[i] == "--with-translations")
			options.includeTranslations = true;
		else if (args[i] == "--no-protect")
			options.protect = false;
		else
			return usage("unknown argument " + args[i]);
	}
	if (!open(p, false))
		return 1;
	if (p.strOutOfDate())
		std::cout << "Warning: main.db is not up to date with Generals.str (BabylonTool update).\n";
	const DialogFiles files = p.dialogFiles();
	const int count = exportCount(p.db, langs[0], options, files);
	if (!count)
	{
		std::cout << "Nothing to export.\n";
		return 0;
	}
	std::vector<std::uint8_t> data;
	writeSheet(exportSheet(p.db, langs[0], options, files), format, data);
	std::string error;
	if (!writeFile(file, data, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	std::cout << "Exported " << count << " " << langName(langs[0]) << " texts (" << exportFilterName(options.filter) << ") to " << args[1] << "\n";
	return 0;
}

int cmdImport(Project &p, const std::vector<std::string> &args)
{
	if (args.empty())
		return usage("import needs a file");
	bool trust = false;
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		if (args[i] == "--trust-ids")
			trust = true;
		else
			return usage("unknown argument " + args[i]);
	}
	if (!open(p, false))
		return 1;
	if (!p.canSave())
	{
		std::cerr << "BabylonTool: the database is read only.\n";
		return 1;
	}
	std::vector<std::uint8_t> data;
	std::string error;
	Sheet sheet;
	if (!readFile(pathFromText(args[0]), data, error) || !readSheet(data, sheet, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	FixedVerifier verifier(trust);
	const ImportResult r = importSheet(p.db, sheet, verifier);
	std::cout << "Importing \"" << args[0] << "\"... " << (r.ok ? langName(r.lang) : "FAILED") << "\n";
	for (const std::string &line : r.log)
		std::cout << "  " << line << "\n";
	if (!r.ok)
		return 1;
	return save(p) ? 0 : 1;
}

int cmdGenerate(Project &p, const std::vector<std::string> &args)
{
	if (args.empty())
		return usage("generate needs a prefix and languages");
	GenerateOptions options;
	options.generatedOn = now();
	fs::path outDir = ".";
	std::vector<Lang> langs;
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		if (args[i] == "--format" && i + 1 < args.size())
		{
			const std::string &v = args[++i];
			if (v == "csf")
				options.format = GameFileFormat::Csf;
			else if (v == "str")
				options.format = GameFileFormat::Str;
			else
				return usage("--format takes csf or str");
		}
		else if (args[i] == "--untranslated" && i + 1 < args.size())
		{
			const std::string &v = args[++i];
			if (v == "ids")
				options.untranslated = Untranslated::UseIds;
			else if (v == "original")
				options.untranslated = Untranslated::UseOriginal;
			else
				return usage("--untranslated takes ids or original");
		}
		else if (args[i] == "--out" && i + 1 < args.size())
			outDir = pathFromText(args[++i]);
		else if (!languageArg(args[i], langs))
			return usage("unknown language " + args[i]);
	}
	if (langs.empty())
		return usage("no language given");
	if (!open(p, false))
		return 1;
	if (p.strOutOfDate())
		std::cout << "Warning: main.db is not up to date with Generals.str (BabylonTool update).\n";
	const DialogFiles files = p.dialogFiles();
	int failed = 0;
	for (const Lang lang : langs)
	{
		const GeneratedFile f = generateGameFile(p.db, args[0], lang, options, files);
		std::string error = f.error;
		const fs::path target = outDir / pathFromText(f.fileName);
		const bool ok = f.ok && writeFile(target, f.data, error);
		std::cout << "Writing: " << pathText(target) << " - " << langName(lang) << "... " << (!ok ? "FAILED " + error : f.warnings.empty() ? "OK" : "WARNING")
				  << "\n";
		for (const std::string &w : f.warnings)
			std::cout << "  " << w << "\n";
		if (!ok)
			++failed;
	}
	return failed ? 1 : 0;
}

int cmdReport(Project &p, const std::vector<std::string> &args)
{
	if (args.empty())
		return usage("report needs a file");
	ReportOptions options;
	options.translations = false;
	std::vector<Lang> langs;
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		if (args[i] == "--translations")
			options.translations = true;
		else if (args[i] == "--dialog")
			options.dialog = true;
		else if (args[i] == "--limit" && i + 1 < args.size())
			options.limit = std::atoi(args[++i].c_str());
		else if (!languageArg(args[i], langs))
			return usage("unknown language " + args[i]);
	}
	if (!options.translations && !options.dialog)
		options.translations = true;
	if (langs.empty())
		return usage("no language given");
	if (!open(p, false))
		return 1;
	const std::string text = reportText(p.db, langs, options, p.dialogFiles(), now());
	std::string error;
	if (!writeFile(pathFromText(args[0]), text, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	std::cout << "Report written to " << args[0] << "\n";
	return 0;
}

int cmdVerify(Project &p, const std::vector<std::string> &args)
{
	std::vector<Lang> langs;
	if (args.size() != 1 || !languageArg(args[0], langs) || langs.size() != 1)
		return usage("verify needs one language");
	if (!open(p, false))
		return 1;
	const Lang lang = langs[0];
	std::cout << "Verifying " << langName(lang) << " text...\n";
	const TranslationReport tr = reportTranslations(p.db, lang);
	if (tr.problems() < 100)
		reportTranslations(p.db, lang, [](const std::string &line) { std::cout << "  String " << line << "\n"; });
	for (const std::string &line : translationSummary(tr, lang))
		std::cout << "  " << line << "\n";
	std::cout << "Verifying " << langName(lang) << " dialog...\n";
	const DialogFiles files = p.dialogFiles();
	recheckDialog(p.db, lang, files);
	const DialogReport dr = reportDialog(p.db, lang, files);
	if (dr.problems() < 100)
		reportDialog(p.db, lang, files, [](const std::string &line) { std::cout << "  String " << line << "\n"; });
	for (const std::string &line : dialogSummary(dr))
		std::cout << "  " << line << "\n";
	if (p.db.modified && p.canSave())
		save(p);
	return tr.problems() || dr.problems() ? 1 : 0;
}

int cmdResetDialog(Project &p, const std::vector<std::string> &args)
{
	std::vector<Lang> langs;
	if (args.size() != 1 || !languageArg(args[0], langs) || langs.size() != 1)
		return usage("reset-dialog needs one language");
	if (!open(p, false))
		return 1;
	invalidateDialog(p.db, langs[0]);
	return save(p) ? 0 : 1;
}

int cmdImportGame(Project &p, const std::vector<std::string> &args)
{
	std::vector<Lang> langs;
	if (args.size() != 2 || !languageArg(args[0], langs) || langs.size() != 1)
		return usage("import-game needs a language and a file");
	if (!open(p, false))
		return 1;
	if (!p.canSave())
	{
		std::cerr << "BabylonTool: the database is read only.\n";
		return 1;
	}
	std::vector<std::uint8_t> data;
	std::vector<StrOutLabel> labels;
	std::string error;
	if (!readFile(pathFromText(args[1]), data, error) || !readGameFile(data, labels, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	const GameImportResult r = importGameFile(p.db, langs[0], labels);
	std::cout << "Importing " << langName(langs[0]) << " texts of \"" << args[1] << "\"...\n";
	for (const std::string &line : r.log)
		std::cout << "  " << line << "\n";
	if (!r.ok)
		return 1;
	return save(p) ? 0 : 1;
}

int cmdCsfToStr(const std::vector<std::string> &args)
{
	if (args.size() != 2)
		return usage("csf-to-str needs an input and an output file");
	std::vector<std::uint8_t> data;
	std::vector<StrOutLabel> labels;
	std::string error;
	if (!readFile(pathFromText(args[0]), data, error) || !readGameFile(data, labels, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	int lost = 0;
	const std::string text = writeStrFile(labels, "BabylonTool csf-to-str " + args[0], now(), &lost);
	if (!writeFile(pathFromText(args[1]), text, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	std::cout << labels.size() << " labels written to " << args[1] << "\n";
	if (lost)
		std::cout << "Warning: " << lost << " characters outside Latin-1 written as '?'\n";
	return 0;
}

int cmdDump(Project &p, const std::vector<std::string> &args)
{
	std::vector<Lang> langs;
	if (!args.empty() && (!languageArg(args[0], langs) || langs.size() != 1))
		return usage("unknown language " + args[0]);
	const auto quiet = [](const std::string &) {};
	p.log = quiet;
	p.load();
	if (p.dbBroken)
		return 1;
	std::cout << "next string ID " << p.db.nextId << "\n";
	for (const Label &label : p.db.labels)
	{
		std::cout << toUtf8(label.name);
		if (label.maxLen)
			std::cout << " [MAXLEN " << label.maxLen << "]";
		std::cout << "\n";
		for (const Text &text : label.texts)
		{
			std::cout << "  " << text.id << " r" << text.revision << " \"" << toUtf8(text.text) << "\"";
			if (text.isDialog())
				std::cout << " = " << toUtf8(text.wave);
			std::cout << "\n";
			for (const Translation &t : text.translations)
				if (langs.empty() || langs[0] == t.lang)
					std::cout << "    " << langName(t.lang) << " r" << t.revision << " \"" << toUtf8(t.text) << "\"\n";
		}
	}
	for (const Text &text : p.db.obsolete)
		std::cout << "obsolete " << text.id << " \"" << toUtf8(text.text) << "\"\n";
	return 0;
}

int cmdConvert(const std::vector<std::string> &args)
{
	if (args.size() != 2)
		return usage("convert needs an input and an output file");
	const SheetFormat format = sheetFormatFromName(args[1]);
	if (format == SheetFormat::Unknown)
		return usage("the output must end in .xlsx, .ods or .csv");
	std::vector<std::uint8_t> data;
	std::string error;
	Sheet sheet;
	if (!readFile(pathFromText(args[0]), data, error) || !readSheet(data, sheet, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	writeSheet(sheet, format, data);
	if (!writeFile(pathFromText(args[1]), data, error))
	{
		std::cerr << "BabylonTool: " << error << "\n";
		return 1;
	}
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);
	Project project;
	project.paths = ProjectPaths::inFolder(fs::current_path());
	std::size_t i = 0;
	for (; i < args.size(); ++i)
	{
		const std::string &a = args[i];
		if (a == "--help" || a == "-h")
		{
			std::cout << USAGE;
			return 0;
		}
		if (a.rfind("--", 0) != 0)
			break;
		if (i + 1 >= args.size())
			return usage(a + " needs a value");
		const fs::path value = pathFromText(args[++i]);
		if (a == "--project")
		{
			const ProjectPaths in = ProjectPaths::inFolder(value);
			project.paths = in;
		}
		else if (a == "--str")
			project.paths.str = value;
		else if (a == "--db")
			project.paths.db = value;
		else if (a == "--dialog")
			project.paths.dialog = value;
		else
			return usage("unknown option " + a);
	}
	if (i >= args.size())
		return usage("no command");
	const std::string command = args[i];
	const std::vector<std::string> rest(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());

	if (command == "status")
		return cmdStatus(project);
	if (command == "check")
		return cmdCheck(project);
	if (command == "update")
		return cmdUpdate(project, rest);
	if (command == "export")
		return cmdExport(project, rest);
	if (command == "import")
		return cmdImport(project, rest);
	if (command == "generate")
		return cmdGenerate(project, rest);
	if (command == "report")
		return cmdReport(project, rest);
	if (command == "verify")
		return cmdVerify(project, rest);
	if (command == "reset-dialog")
		return cmdResetDialog(project, rest);
	if (command == "import-game")
		return cmdImportGame(project, rest);
	if (command == "csf-to-str")
		return cmdCsfToStr(rest);
	if (command == "dump")
		return cmdDump(project, rest);
	if (command == "convert")
		return cmdConvert(rest);
	return usage("unknown command " + command);
}
