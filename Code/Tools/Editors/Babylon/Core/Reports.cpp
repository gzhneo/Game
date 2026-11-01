/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Reports.h"

#include "Model/Text.h"

#include <system_error>

namespace Babylon
{

namespace
{

namespace fs = std::filesystem;

std::string lower(std::string s)
{
	for (char &c : s)
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	return s;
}

std::string waveFileName(const Text &text, const LangInfo &lang)
{
	return toUtf8(text.wave) + lang.character + ".wav";
}

const WaveInfo *waveInfoOf(const Text &text, Lang lang)
{
	if (lang == Lang::US)
		return &text.waveInfo;
	const Translation *t = text.translation(lang);
	return t ? &t->wave : nullptr;
}

fs::path fromUtf8Path(const std::string &s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string nameOf(const fs::path &p)
{
	const std::u8string u = p.filename().u8string();
	return std::string(u.begin(), u.end());
}

std::string id(const Text &text)
{
	return std::to_string(text.id);
}

} // namespace

fs::path DialogFiles::path(const Text &text, const LangInfo &lang) const
{
	return m_root / lang.character / fromUtf8Path(waveFileName(text, lang));
}

std::optional<std::uint64_t> DialogFiles::size(const Text &text, const LangInfo &lang) const
{
	if (m_root.empty() || text.wave.empty())
		return std::nullopt;
	std::error_code ec;
	const fs::path exact = path(text, lang);
	if (fs::is_regular_file(exact, ec))
	{
		const auto s = fs::file_size(exact, ec);
		if (!ec)
			return s;
	}
	// The same name in another letter case: folder and file looked up in a listing.
	const auto listing = [this](const fs::path &dir) -> const std::map<std::string, std::string> & {
		const std::u8string k8 = dir.u8string();
		const std::string key(k8.begin(), k8.end());
		auto it = m_listings.find(key);
		if (it == m_listings.end())
		{
			std::map<std::string, std::string> names;
			std::error_code err;
			for (fs::directory_iterator d(dir, err), end; !err && d != end; d.increment(err))
				names.emplace(lower(nameOf(d->path())), nameOf(d->path()));
			it = m_listings.emplace(key, std::move(names)).first;
		}
		return it->second;
	};
	const auto &top = listing(m_root);
	const auto sub = top.find(lower(lang.character));
	if (sub == top.end())
		return std::nullopt;
	const fs::path dir = m_root / fromUtf8Path(sub->second);
	const auto &files = listing(dir);
	const auto file = files.find(lower(waveFileName(text, lang)));
	if (file == files.end())
		return std::nullopt;
	const fs::path found = dir / fromUtf8Path(file->second);
	if (!fs::is_regular_file(found, ec))
		return std::nullopt;
	const auto s = fs::file_size(found, ec);
	if (ec)
		return std::nullopt;
	return s;
}

WaveInfo *waveInfo(Text &text, Lang lang)
{
	if (lang == Lang::US)
		return &text.waveInfo;
	Translation *t = text.translation(lang);
	return t ? &t->wave : nullptr;
}

bool dialogConfirmed(Text &text, Lang lang, const DialogFiles &files, bool recheck)
{
	WaveInfo *w = waveInfo(text, lang);
	const LangInfo *info = langInfo(lang);
	if (!w || !info)
		return false;
	if (w->valid && recheck)
	{
		const auto s = files.size(text, *info);
		w->valid = s && static_cast<std::uint32_t>(*s & 0xFFFFFFFFu) == w->lo && static_cast<std::uint32_t>(*s >> 32) == w->hi;
	}
	return w->valid;
}

bool confirmDialog(Text &text, Lang lang, const DialogFiles &files)
{
	WaveInfo *w = waveInfo(text, lang);
	const LangInfo *info = langInfo(lang);
	if (!w || !info)
		return false;
	const auto s = files.size(text, *info);
	if (!s)
	{
		w->valid = false;
		return false;
	}
	w->lo = static_cast<std::uint32_t>(*s & 0xFFFFFFFFu);
	w->hi = static_cast<std::uint32_t>(*s >> 32);
	w->valid = true;
	return true;
}

void recheckDialog(Database &db, Lang lang, const DialogFiles &files)
{
	const LangInfo *info = langInfo(lang);
	if (!info)
		return;
	for (Label &label : db.labels)
		for (Text &text : label.texts)
			if (text.isDialog() && files.size(text, *info))
			{
				const WaveInfo *before = waveInfo(text, lang);
				const bool was = before && before->valid;
				if (was != dialogConfirmed(text, lang, files, true))
					db.modified = true;
			}
}

void invalidateDialog(Database &db, Lang lang)
{
	for (Label &label : db.labels)
		for (Text &text : label.texts)
			if (text.isDialog())
				if (WaveInfo *w = waveInfo(text, lang); w && w->valid)
				{
					w->valid = false;
					db.modified = true;
				}
}

TranslationReport reportTranslations(const Database &db, Lang lang, const LineSink &detail)
{
	TranslationReport r;
	for (const Label &label : db.labels)
	{
		const int maxLen = label.maxLen;
		for (const Text &text : label.texts)
		{
			if (text.text.empty())
				continue;
			++r.strings;
			bool tooBig = false;
			const int len = static_cast<int>(text.text.size());
			if (lang != Lang::US)
			{
				if (const Translation *t = text.translation(lang))
				{
					const int tlen = static_cast<int>(t->text.size());
					if (maxLen && tlen > maxLen)
					{
						if (detail)
							detail(id(text) + ": translation is too long by " + std::to_string(tlen - maxLen) + " characters");
						tooBig = true;
					}
					if (text.revision > t->revision)
					{
						if (detail)
							detail(id(text) + ": needs re-translation");
						++r.retranslate;
					}
					else
					{
						++r.translated;
						if (!sameFormat(t->text, text.text))
						{
							if (detail)
								detail(id(text) + ": translation has other format conversions than the original");
							++r.badFormat;
						}
					}
				}
				else
				{
					if (detail)
						detail(id(text) + ": not translated");
					++r.missing;
				}
			}
			else if (maxLen && len > maxLen)
			{
				if (detail)
					detail(id(text) + ": is too long by " + std::to_string(len - maxLen) + " characters");
				tooBig = true;
			}
			if (tooBig)
				++r.tooLong;
		}
		++r.labels;
	}
	r.errors = r.tooLong + r.badFormat;
	return r;
}

DialogReport reportDialog(const Database &db, Lang lang, const DialogFiles &files, const LineSink &detail)
{
	DialogReport r;
	const LangInfo *info = langInfo(lang);
	if (!info)
		return r;
	for (const Label &label : db.labels)
		for (const Text &text : label.texts)
		{
			if (!text.isDialog())
				continue;
			++r.dialog;
			const std::string file = waveFileName(text, *info);
			if (files.size(text, *info))
			{
				const WaveInfo *w = waveInfoOf(text, lang);
				if (w && w->valid)
					++r.resolved;
				else
				{
					if (detail)
						detail(id(text) + ": audio file \"" + file + "\" not verified");
					++r.unresolved;
				}
			}
			else
			{
				if (detail)
					detail(id(text) + ": audio file \"" + file + "\" missing");
				++r.missing;
			}
		}
	return r;
}

std::vector<std::string> translationSummary(const TranslationReport &r, Lang lang)
{
	std::vector<std::string> lines;
	if (!r.strings)
		return {"No text"};
	if (!r.problems())
		return {"OK: " + std::to_string(r.strings) + " texts"};
	if (r.missing)
		lines.push_back("Missing translations        : " + std::to_string(r.missing));
	if (r.tooLong)
		lines.push_back("Oversized strings           : " + std::to_string(r.tooLong));
	if (r.retranslate)
		lines.push_back("Retranslations              : " + std::to_string(r.retranslate));
	if (r.badFormat)
		lines.push_back("Badly formatted translations: " + std::to_string(r.badFormat));
	if (lang == Lang::US)
		lines.push_back("Recommend editing Generals.str to fix the problems and reloading");
	else
		lines.push_back("Recommend exporting the translations for an update and importing them again");
	return lines;
}

std::vector<std::string> dialogSummary(const DialogReport &r)
{
	if (!r.dialog)
		return {"No dialog found"};
	std::vector<std::string> lines;
	if (r.missing)
		lines.push_back("Missing dialog   : " + std::to_string(r.missing));
	if (r.unresolved)
		lines.push_back("Unverified dialog: " + std::to_string(r.unresolved));
	if (r.resolved)
		lines.push_back("Verified dialog  : " + std::to_string(r.resolved));
	lines.push_back("Total dialog     : " + std::to_string(r.dialog));
	return lines;
}

std::string reportText(const Database &db, const std::vector<Lang> &languages, const ReportOptions &options, const DialogFiles &files, const std::string &stamp)
{
	std::string out = "Babylon Report: " + stamp + "\n";
	const auto details = [&out](const std::string &line) { out += "\t\t\tString " + line + "\n"; };
	for (const Lang lang : languages)
	{
		const char *name = langName(lang);
		out += std::string("\n\n") + name + " Status:\n";
		if (options.translations)
		{
			const TranslationReport r = reportTranslations(db, lang);
			out += std::string("\n\tText Summary: ") + name + "\n\t-------------\n\n";
			out += "\t\tErrors: " + std::to_string(r.errors) + "\n";
			if (lang != Lang::US)
			{
				out += "\t\tNot translated: " + std::to_string(r.missing) + "\n";
				out += "\t\tRetranslation: " + std::to_string(r.retranslate) + "\n";
				out += "\t\tTranslated: " + std::to_string(r.translated) + "\n";
			}
			out += "\t\tTotal text: " + std::to_string(r.strings) + "\n";
			if (r.problems() && r.problems() < options.limit)
			{
				out += std::string("\n\tText Details: ") + name + "\n\t------------\n\n";
				reportTranslations(db, lang, details);
			}
		}
		if (options.dialog)
		{
			const DialogReport r = reportDialog(db, lang, files);
			out += std::string("\n\tDialog Summary: ") + name + "\n\t-------------\n\n";
			out += "\t\tMissing Audio: " + std::to_string(r.missing) + "\n";
			out += "\t\tNot verified: " + std::to_string(r.unresolved) + "\n";
			out += "\t\tVerified: " + std::to_string(r.resolved) + "\n";
			out += "\t\tTotal dialog: " + std::to_string(r.dialog) + "\n";
			if (r.problems() && r.problems() < options.limit)
			{
				out += std::string("\n\tDialog Details: ") + name + "\n\t------------\n\n";
				reportDialog(db, lang, files, details);
			}
		}
	}
	return out;
}

} // namespace Babylon
