/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Language.cpp -- interface texts in English (source) and German.

#include "EditorCommon/Language.h"

#include <QFile>
#include <QHash>
#include <QLocale>

namespace EditorCommon
{

namespace
{

Language s_language = Language::English;
QString s_translationFile;

const QHash<QString, QString> &german()
{
	static const QHash<QString, QString> table = [] {
		QHash<QString, QString> t;
		QFile file(s_translationFile);
		if (!file.open(QIODevice::ReadOnly))
			return t;
		const QString text = QString::fromUtf8(file.readAll());
		for (const QString &line : text.split(QLatin1Char('\n')))
		{
			const QString trimmed = line.trimmed();
			if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
				continue;
			const qsizetype tab = line.indexOf(QLatin1Char('\t'));
			if (tab <= 0)
				continue;
			QString target = line.mid(tab + 1);
			target.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
			while (target.endsWith(QLatin1Char('\r')))
				target.chop(1);
			t.insert(line.left(tab), target);
		}
		return t;
	}();
	return table;
}

} // namespace

void setTranslationFile(const QString &path)
{
	s_translationFile = path;
}

void setLanguage(Language language)
{
	s_language = language;
}

Language currentLanguage()
{
	return s_language;
}

Language systemLanguage()
{
	return QLocale::system().language() == QLocale::German ? Language::German : Language::English;
}

QString T(const char *english)
{
	const QString source = QString::fromUtf8(english);
	if (s_language != Language::German)
		return source;
	return german().value(source, source);
}

QString sourceText(const QString &shown)
{
	if (s_language != Language::German)
		return shown;
	static const QHash<QString, QString> reverse = [] {
		QHash<QString, QString> r;
		const QHash<QString, QString> &table = german();
		for (auto it = table.begin(); it != table.end(); ++it)
			r.insert(it.value(), it.key());
		return r;
	}();
	return reverse.value(shown, shown);
}

int germanEntryCount()
{
	return static_cast<int>(german().size());
}

} // namespace EditorCommon
