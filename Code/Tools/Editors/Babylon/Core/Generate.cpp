/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Generate.h"

#include "Model/CsfFile.h"
#include "Model/StrFile.h"
#include "Model/Text.h"

#include <filesystem>
#include <system_error>

namespace Babylon
{

namespace
{

std::string count(int n, const char *one, const char *many)
{
	return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

std::string gameFileName(const std::string &prefix, Lang lang, GameFileFormat format)
{
	const LangInfo *info = langInfo(lang);
	std::string name = prefix + "_" + (info ? info->initials : "xx") + (format == GameFileFormat::Str ? ".str" : ".csf");
	// lower case like the original, but only the file name, not the folders of the prefix
	const std::size_t slash = name.find_last_of("/\\");
	for (std::size_t i = slash == std::string::npos ? 0 : slash + 1; i < name.size(); ++i)
		if (name[i] >= 'A' && name[i] <= 'Z')
			name[i] = static_cast<char>(name[i] - 'A' + 'a');
	return name;
}

GeneratedFile generateGameFile(const Database &db, const std::string &prefix, Lang lang, const GenerateOptions &options, const DialogFiles &files)
{
	GeneratedFile out;
	out.lang = lang;
	out.fileName = gameFileName(prefix, lang, options.format);
	const LangInfo *info = langInfo(lang);
	if (!info)
	{
		out.error = "unknown language";
		return out;
	}

	std::vector<StrOutLabel> labels;
	labels.reserve(db.labels.size());
	for (const Label &label : db.labels)
	{
		StrOutLabel l;
		l.name = label.name;
		for (const Text &text : label.texts)
		{
			StrOutText t;
			t.wave = text.wave;
			if (const Translation *trans = text.translation(lang))
				t.text = trans->text;
			else if (lang == Lang::US || options.untranslated == Untranslated::UseOriginal)
				t.text = text.text;
			else if (!text.text.empty())
				t.text = toU16(text.id);
			if (text.text.empty())
				t.text.clear();
			l.texts.push_back(std::move(t));
		}
		labels.push_back(std::move(l));
	}

	if (options.format == GameFileFormat::Csf)
		out.ok = writeCsfFile(labels, *info, out.data, out.error);
	else
	{
		int lost = 0;
		const std::string s = writeStrFile(labels, options.generatedBy, options.generatedOn, &lost);
		out.data.assign(s.begin(), s.end());
		out.ok = true;
		if (lost)
			out.warnings.push_back(count(lost, "character", "characters") + " outside Latin-1 written as '?' (use the .csf format for this language)");
	}

	const TranslationReport tr = reportTranslations(db, lang);
	const DialogReport dr = reportDialog(db, lang, files);
	if (const int missing = tr.missing + tr.retranslate)
		out.warnings.push_back(count(missing, "translation is missing", "translations are missing"));
	if (tr.tooLong)
		out.warnings.push_back(count(tr.tooLong, "string is too long", "strings are too long"));
	if (tr.badFormat)
		out.warnings.push_back(count(tr.badFormat, "translation has other format conversions", "translations have other format conversions"));
	std::error_code ec;
	if (!files.root().empty() && std::filesystem::is_directory(files.root(), ec))
	{
		if (dr.missing)
			out.warnings.push_back(count(dr.missing, "speech file is missing", "speech files are missing"));
		if (dr.unresolved)
			out.warnings.push_back(count(dr.unresolved, "speech file is not verified", "speech files are not verified"));
	}
	return out;
}

} // namespace Babylon
