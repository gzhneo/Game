/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Update.h"

#include "Model/Text.h"

#include <unordered_map>

namespace Babylon
{

namespace
{

enum class LabelResult
{
	Ok,
	Skip,
	SkipAll
};

Text newTextFrom(const Text &source)
{
	Text t;
	t.text = source.text;
	t.wave = source.wave;
	t.revision = source.revision;
	t.line = source.line;
	return t;
}

LabelResult updateLabel(const Label &src, Label &dst, std::vector<Text> &obsolete, UpdateCounts &info, UpdateResolver *resolver, bool apply, bool skip,
	bool allowMultiText)
{
	std::vector<int> srcMatch(src.texts.size(), -1);
	std::vector<int> dstMatch(dst.texts.size(), -1);
	std::vector<bool> retranslate(dst.texts.size(), false);
	bool labelModified = false;

	// identical texts first
	for (std::size_t s = 0; s < src.texts.size(); ++s)
		for (std::size_t d = 0; d < dst.texts.size(); ++d)
			if (dstMatch[d] == -1 && dst.texts[d].text == src.texts[s].text)
			{
				srcMatch[s] = static_cast<int>(d);
				dstMatch[d] = static_cast<int>(s);
				break;
			}

	// the rest needs a decision
	for (std::size_t s = 0; s < src.texts.size(); ++s)
	{
		if (srcMatch[s] != -1)
			continue;
		std::vector<int> candidates;
		for (std::size_t d = 0; d < dst.texts.size(); ++d)
			if (dstMatch[d] == -1)
				candidates.push_back(static_cast<int>(d));
		if (candidates.empty())
			break;	// nothing left to match: the remaining texts are new
		if (!apply || skip || !resolver)
			return LabelResult::Skip;

		int match = -1;
		ChangeDecision decision;
		if (allowMultiText)
		{
			const MatchDecision m = resolver->matchText(dst, src.texts[s], candidates);
			decision = m.decision;
			match = m.match;
			bool valid = match == -1;
			for (const int c : candidates)
				valid = valid || c == match;
			if (!valid)
				match = -1;
		}
		else
		{
			match = candidates.front();
			decision = resolver->changedText(dst, src.texts[s], dst.texts[static_cast<std::size_t>(match)]);
		}
		if (decision == ChangeDecision::SkipLabel)
			return LabelResult::Skip;
		if (decision == ChangeDecision::SkipAll)
			return LabelResult::SkipAll;
		if (match != -1)
		{
			srcMatch[s] = match;
			dstMatch[static_cast<std::size_t>(match)] = static_cast<int>(s);
			retranslate[static_cast<std::size_t>(match)] = decision == ChangeDecision::Retranslate;
		}
	}

	// matched texts take over text and speech file of Generals.str
	for (std::size_t d = 0; d < dst.texts.size(); ++d)
	{
		if (dstMatch[d] == -1)
			continue;
		const Text &s = src.texts[static_cast<std::size_t>(dstMatch[d])];
		Text &t = dst.texts[d];
		if (t.text != s.text)
		{
			if (apply)
			{
				t.text = s.text;
				t.waveInfo.valid = false;
			}
			++info.modifiedStrings;
			++info.changes;
			labelModified = true;
		}
		if (!equalNoCase(t.wave, s.wave))
		{
			if (apply)
			{
				t.wave = s.wave;
				t.invalidateAllWaves();
			}
			++info.updatedWaves;
			++info.changes;
			labelModified = true;
		}
		if (retranslate[d])
		{
			if (apply)
				++t.revision;
			labelModified = true;
		}
		if (apply)
			t.line = s.line;
	}

	// unmatched old texts become obsolete, unmatched new ones are added
	std::vector<Text> kept;
	for (std::size_t d = 0; d < dst.texts.size(); ++d)
	{
		if (dstMatch[d] != -1)
		{
			if (apply)
				kept.push_back(std::move(dst.texts[d]));
			continue;
		}
		if (apply)
			obsolete.push_back(std::move(dst.texts[d]));
		++info.deletedStrings;
		++info.changes;
		labelModified = true;
	}
	for (std::size_t s = 0; s < src.texts.size(); ++s)
	{
		if (srcMatch[s] != -1)
			continue;
		if (apply)
			kept.push_back(newTextFrom(src.texts[s]));
		++info.newStrings;
		++info.changes;
		labelModified = true;
	}
	if (apply)
		dst.texts = std::move(kept);

	const auto field = [&](const std::u16string &from, std::u16string &to, int &count) {
		if (!equalNoCase(from, to))
		{
			if (apply)
				to = from;
			labelModified = true;
			++info.changes;
			++count;
		}
	};
	field(src.comment, dst.comment, info.updatedComments);
	field(src.context, dst.context, info.updatedContexts);
	field(src.speaker, dst.speaker, info.updatedSpeakers);
	field(src.listener, dst.listener, info.updatedListeners);
	if (src.maxLen != dst.maxLen)
	{
		if (apply)
			dst.maxLen = src.maxLen;
		labelModified = true;
		++info.updatedMaxLen;
		++info.changes;
	}
	if (apply)
		dst.line = src.line;
	if (labelModified)
		++info.modifiedLabels;
	return LabelResult::Ok;
}

std::string plural(int n, const char *what, const char *verb)
{
	return std::string(verb) + " " + std::to_string(n) + " " + what + (n == 1 ? "" : "s");
}

} // namespace

MatchDecision UpdateResolver::matchText(const Label &, const Text &, const std::vector<int> &)
{
	return {};
}

ChangeDecision FixedResolver::changedText(const Label &label, const Text &, const Text &)
{
	asked.push_back(toUtf8(label.name));
	return m_answer;
}

UpdateCounts updateDatabase(const Database &str, Database &db, UpdateResolver *resolver, bool apply)
{
	UpdateCounts info;
	std::unordered_map<std::u16string, std::size_t> index;
	for (std::size_t i = 0; i < db.labels.size(); ++i)
		index.emplace(db.labels[i].name, i);
	std::vector<bool> processed(db.labels.size(), false);
	bool skipAll = false;

	for (const Label &src : str.labels)
	{
		const auto it = index.find(src.name);
		if (it != index.end())
		{
			processed[it->second] = true;
			const LabelResult result = updateLabel(src, db.labels[it->second], db.obsolete, info, resolver, apply, skipAll, db.allowMultiText);
			if (result == LabelResult::SkipAll)
				skipAll = true;
			if (result != LabelResult::Ok)
			{
				++info.skippedLabels;
				++info.changes;
			}
			continue;
		}
		if (apply)
		{
			Label label;
			label.name = src.name;
			label.comment = src.comment;
			label.context = src.context;
			label.speaker = src.speaker;
			label.listener = src.listener;
			label.maxLen = src.maxLen;
			label.line = src.line;
			for (const Text &t : src.texts)
				label.texts.push_back(newTextFrom(t));
			index.emplace(label.name, db.labels.size());
			processed.push_back(true);
			db.labels.push_back(std::move(label));
		}
		info.newStrings += static_cast<int>(src.texts.size());
		info.changes += static_cast<int>(src.texts.size()) + 1;
		++info.newLabels;
	}

	std::vector<Label> kept;
	for (std::size_t i = 0; i < db.labels.size(); ++i)
	{
		if (processed[i])
		{
			if (apply)
				kept.push_back(std::move(db.labels[i]));
			continue;
		}
		for (Text &t : db.labels[i].texts)
		{
			if (apply)
				db.obsolete.push_back(std::move(t));
			++info.deletedStrings;
			++info.changes;
		}
		++info.deletedLabels;
	}
	if (apply)
	{
		db.labels = std::move(kept);
		db.assignIds();
		if (info.changes)
			db.modified = true;
	}
	info.aborted = skipAll;
	return info;
}

std::vector<std::string> updateSummary(const UpdateCounts &c)
{
	std::vector<std::string> lines;
	const auto add = [&lines](int n, const char *what, const char *verb) {
		if (n)
			lines.push_back(plural(n, what, verb));
	};
	add(c.newLabels, "new label", "Added");
	add(c.deletedLabels, "label", "Deleted");
	add(c.modifiedLabels, "label", "Modified");
	add(c.newStrings, "new string", "Added");
	add(c.deletedStrings, "string", "Deleted");
	add(c.modifiedStrings, "string", "Modified");
	add(c.skippedLabels, "label", "Skipped");
	add(c.updatedComments, "comment", "Updated");
	add(c.updatedContexts, "context", "Updated");
	add(c.updatedSpeakers, "speaker", "Updated");
	add(c.updatedListeners, "listener", "Updated");
	add(c.updatedMaxLen, "max length", "Updated");
	add(c.updatedWaves, "speech file", "Updated");
	if (lines.empty())
		lines.push_back("No differences found");
	if (c.aborted)
		lines.push_back("Update aborted by user: the skipped labels stay as they were");
	return lines;
}

} // namespace Babylon
