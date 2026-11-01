/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Dialogs.h -- the questions Babylon asks: what a changed English text means, whether a row of an
// imported sheet still belongs to its text, whether a recording matches its text, and the options
// of export, generating and reports.
//
// The main window asks through Prompts; the self test replaces it with fixed answers, so every
// path runs without anyone clicking. The dialog classes can also be built on their own (pictures).

#pragma once

#include "Core/Exchange.h"
#include "Core/Generate.h"
#include "Core/Reports.h"
#include "Core/Update.h"

#include <QDialog>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QWidget;

namespace BabylonApp
{

struct ExportChoice
{
	Babylon::Lang lang = Babylon::Lang::German;
	Babylon::ExportOptions options;
	QString file;
};

struct GenerateChoice
{
	QString folder;
	QString prefix = QStringLiteral("Generals");
	Babylon::GenerateOptions options;
	std::vector<Babylon::Lang> languages;
};

struct ReportChoice
{
	QString file;
	Babylon::ReportOptions options;
	std::vector<Babylon::Lang> languages;
};

enum class Proceed
{
	Yes,
	No,
	Always
};

enum class SpeechAnswer
{
	Matches,
	Skip,
	Stop
};

enum class SaveAnswer
{
	Save,
	Discard,
	Cancel
};

class Prompts
{
public:
	virtual ~Prompts() = default;
	virtual Babylon::ChangeDecision changedText(QWidget *parent, const Babylon::Label &label, const Babylon::Text &newText, const Babylon::Text &oldText);
	virtual Babylon::MatchDecision matchText(QWidget *parent, const Babylon::Label &label, const Babylon::Text &newText, const std::vector<int> &candidates);
	virtual bool sameText(QWidget *parent, const std::u16string &sheetEnglish, const std::u16string &dbEnglish, int stringId);
	virtual SpeechAnswer speech(QWidget *parent, const Babylon::Label &label, const QString &text, const QString &file);
	virtual Proceed proceed(QWidget *parent, const QString &reason);
	virtual SaveAnswer unsaved(QWidget *parent);
	virtual bool yesNo(QWidget *parent, const QString &title, const QString &text);
	virtual void message(QWidget *parent, const QString &title, const QString &text);
	virtual bool exportOptions(QWidget *parent, ExportChoice &choice, const std::function<int(const ExportChoice &)> &count);
	virtual bool generateOptions(QWidget *parent, GenerateChoice &choice);
	virtual bool reportOptions(QWidget *parent, ReportChoice &choice);
	virtual QString openSheet(QWidget *parent, const QString &title);
	virtual QString openGameFile(QWidget *parent, const QString &title);
	virtual QString openFolder(QWidget *parent, const QString &title, const QString &start);
};

// A changed text of Generals.str: new meaning, correction, skip, skip all.
class ChangeDialog : public QDialog
{
public:
	ChangeDialog(QWidget *parent, const Babylon::Label &label, const Babylon::Text &newText, const Babylon::Text &oldText);
	[[nodiscard]] Babylon::ChangeDecision decision() const { return m_decision; }

private:
	Babylon::ChangeDecision m_decision = Babylon::ChangeDecision::SkipLabel;
};

// A new text of a label with several texts: which old one it replaces.
class MatchDialog : public QDialog
{
public:
	MatchDialog(QWidget *parent, const Babylon::Label &label, const Babylon::Text &newText, const std::vector<int> &candidates);
	[[nodiscard]] Babylon::MatchDecision decision() const { return m_decision; }

private:
	Babylon::MatchDecision m_decision;
};

class SameTextDialog : public QDialog
{
public:
	SameTextDialog(QWidget *parent, const QString &sheetEnglish, const QString &dbEnglish, int stringId);
};

class SpeechDialog : public QDialog
{
public:
	SpeechDialog(QWidget *parent, const Babylon::Label &label, const QString &text, const QString &file);
	[[nodiscard]] SpeechAnswer answer() const { return m_answer; }

private:
	SpeechAnswer m_answer = SpeechAnswer::Stop;
};

class ExportDialog : public QDialog
{
public:
	ExportDialog(QWidget *parent, const ExportChoice &start, std::function<int(const ExportChoice &)> count);
	[[nodiscard]] ExportChoice choice() const;

private:
	void refresh();
	ExportChoice m_choice;
	std::function<int(const ExportChoice &)> m_count;
	std::function<void()> m_read;
	std::function<void(int)> m_showCount;
};

class GenerateDialog : public QDialog
{
public:
	GenerateDialog(QWidget *parent, const GenerateChoice &start);
	[[nodiscard]] GenerateChoice choice() const { return m_read(); }

private:
	std::function<GenerateChoice()> m_read;
};

class ReportDialog : public QDialog
{
public:
	ReportDialog(QWidget *parent, const ReportChoice &start);
	[[nodiscard]] ReportChoice choice() const { return m_read(); }

private:
	std::function<ReportChoice()> m_read;
};

class ProceedDialog : public QDialog
{
public:
	ProceedDialog(QWidget *parent, const QString &reason);
	[[nodiscard]] Proceed answer() const { return m_answer; }

private:
	Proceed m_answer = Proceed::No;
};

QString qs(const std::u16string &text);
QString languageName(Babylon::Lang lang);

} // namespace BabylonApp
