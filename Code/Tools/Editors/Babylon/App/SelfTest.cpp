/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/SelfTest.h"

#include "App/MainWindow.h"
#include "Core/Project.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/SelfTest.h"
#include "Engine/TextFiles.h"
#include "Model/Sheet.h"
#include "Model/Text.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTextStream>

#include <cstdio>

using namespace Babylon;
using namespace EditorCommon;

namespace BabylonApp
{

namespace
{

const char *STR_FILE =
	"// self test\r\n"
	"GUI:Ok\r\n\"OK\"\r\nEND\r\n\r\n"
	"GUI:Built\r\n// COMMENT: %d is a number\r\n\"%d units built\"\r\nEND\r\n\r\n"
	"DIALOG:Hello\r\n// SPEAKER: General\r\n\"Hello, commander.\\nWelcome back!\" = HELLO01e\r\nEND\r\n\r\n"
	"GUI:Greeting\r\n\"Gr\xFC\xDF" "e\"\r\nEND\r\n";

// Fixed answers; counts what was asked.
class ScriptedPrompts : public Prompts
{
public:
	QString folder;
	ChangeDecision change = ChangeDecision::Retranslate;
	bool same = false;
	bool yes = true;
	SaveAnswer saveAnswer = SaveAnswer::Save;
	int changes = 0;
	int sameTexts = 0;
	int speeches = 0;
	int messages = 0;
	int proceeds = 0;
	QString lastMessage;

	ChangeDecision changedText(QWidget *, const Label &, const Text &, const Text &) override
	{
		++changes;
		return change;
	}
	MatchDecision matchText(QWidget *, const Label &, const Text &, const std::vector<int> &candidates) override
	{
		return {ChangeDecision::Retranslate, candidates.empty() ? -1 : candidates.front()};
	}
	bool sameText(QWidget *, const std::u16string &, const std::u16string &, int) override
	{
		++sameTexts;
		return same;
	}
	SpeechAnswer speech(QWidget *, const Label &, const QString &, const QString &) override
	{
		++speeches;
		return SpeechAnswer::Matches;
	}
	Proceed proceed(QWidget *, const QString &) override
	{
		++proceeds;
		return Proceed::Yes;
	}
	SaveAnswer unsaved(QWidget *) override { return saveAnswer; }
	bool yesNo(QWidget *, const QString &, const QString &) override { return yes; }
	void message(QWidget *, const QString &, const QString &text) override
	{
		++messages;
		lastMessage = text;
	}
	bool exportOptions(QWidget *, ExportChoice &choice, const std::function<int(const ExportChoice &)> &count) override
	{
		choice.lang = Lang::German;
		choice.options.filter = ExportFilter::All;
		choice.file = QDir(folder).filePath(QStringLiteral("Generals_ge.xlsx"));
		return count(choice) > 0;
	}
	bool generateOptions(QWidget *, GenerateChoice &choice) override
	{
		choice.folder = folder;
		choice.prefix = QStringLiteral("Generals");
		choice.languages = {Lang::US, Lang::German};
		return true;
	}
	bool reportOptions(QWidget *, ReportChoice &choice) override
	{
		choice.file = QDir(folder).filePath(QStringLiteral("report.txt"));
		choice.options.translations = true;
		choice.options.dialog = true;
		choice.languages = {Lang::US, Lang::German};
		return true;
	}
	QString openSheet(QWidget *, const QString &) override { return QDir(folder).filePath(QStringLiteral("Generals_ge_filled.ods")); }
};

bool writeText(const QString &path, const QByteArray &bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

bool picture(QWidget &w, const QString &path)
{
	w.show();
	QApplication::processEvents();
	const bool ok = w.grab().save(path);
	w.hide();
	return ok;
}

int stateRows(MainWindow &window, const QString &state)
{
	int n = 0;
	for (int r = 0; r < window.rowCount(); ++r)
		if (window.cellText(r, 3) == state)
			++n;
	return n;
}

} // namespace

int runSelfTest(MainWindow &window, const QString &folder)
{
	QStringList report;
	TestSteps step(report, "babylon");
	QDir(folder).removeRecursively();
	QDir().mkpath(folder);
	const QString root = QDir(folder).filePath(QStringLiteral("project"));
	writeText(QDir(root).filePath(QStringLiteral("Data/Generals.str")), QByteArray(STR_FILE));
	writeText(QDir(root).filePath(QStringLiteral("dialog/e/HELLO01e.wav")), QByteArray(100, 'w'));
	writeText(QDir(root).filePath(QStringLiteral("dialog/g/hello01g.wav")), QByteArray(90, 'w'));

	auto owned = std::make_unique<ScriptedPrompts>();
	ScriptedPrompts &prompts = *owned;
	prompts.folder = folder;
	window.setPrompts(std::move(owned));
	window.show();
	Project &p = window.project();

	// a new database, filled from Generals.str
	window.openProject(ProjectPaths::inFolder(pathFromText(root.toStdString())));
	step(p.strLoaded && p.errors.empty(), QStringLiteral("Generals.str read without errors"));
	step(p.db.labels.size() == 4 && window.rowCount() == 4, QStringLiteral("database filled, table shows 4 texts"));
	step(window.cellText(0, 1) == QStringLiteral("10000") && window.cellText(3, 4) == QString::fromUtf8("Gr\xC3\xBC\xC3\x9F" "e"), QStringLiteral("IDs from 10000, Latin-1 text"));
	step(stateRows(window, QStringLiteral("missing")) == 4, QStringLiteral("German: 4 missing"));
	step(window.save() && QFile::exists(QDir(root).filePath(QStringLiteral("Data/main.db"))), QStringLiteral("main.db saved"));

	// export, a translator fills the sheet and saves it as .ods
	window.exportSheet();
	const QString exported = QDir(folder).filePath(QStringLiteral("Generals_ge.xlsx"));
	Sheet sheet;
	std::vector<std::uint8_t> data;
	std::string error;
	const bool read = readFile(pathFromText(exported.toStdString()), data, error) && readSheet(data, sheet, error);
	step(read && sheet.text(0, 0) == "German" && sheet.integer(0, 1, 0) == 4, QStringLiteral("sheet exported as .xlsx"));
	const char *translations[] = {"OK", "%d Einheiten gebaut", "Hallo, Kommandant.\nWillkommen zur\xC3\xBC" "ck!", "Gr\xC3\xBC\xC3\x9F" "e"};
	for (int r = 0; r < 4; ++r)
		sheet.setText(2 + r, 6, translations[r]);
	writeSheet(sheet, SheetFormat::Ods, data);
	writeFile(pathFromText(prompts.openSheet(nullptr, QString()).toStdString()), data, error);
	window.importSheet();
	step(stateRows(window, QStringLiteral("ok")) == 4, QStringLiteral("translations of an .ods sheet imported"));
	const Translation *hello = p.db.labels[2].texts[0].translation(Lang::German);
	step(hello && hello->text == fromUtf8("Hallo, Kommandant.\\nWillkommen zur\xC3\xBC" "ck!"), QStringLiteral("line break in a cell stored as \\n"));
	window.setFilter(1);
	step(window.rowCount() == 0, QStringLiteral("nothing needs work"));
	window.setFilter(0);

	// a sheet with wrong IDs is refused
	Sheet bad = sheet;
	bad.setText(3, 1, "OTHER:Label");
	bad.setText(3, 5, "something else");
	bad.setText(3, 6, "x");
	writeSheet(bad, SheetFormat::Xlsx, data);
	const QString badFile = QDir(folder).filePath(QStringLiteral("bad.xlsx"));
	writeFile(pathFromText(badFile.toStdString()), data, error);
	const int messages = prompts.messages;
	window.importSheet(badFile);
	step(prompts.sameTexts == 1 && prompts.messages == messages + 1 && p.db.labels[1].texts[0].translation(Lang::German)->text == u"%d Einheiten gebaut",
		QStringLiteral("sheet with wrong IDs stops the import"));

	// Generals.str changes: unsaved changes are saved first, the changed text needs a new translation
	QByteArray changed(STR_FILE);
	changed.replace("%d units built", "%d units ready");
	writeText(QDir(root).filePath(QStringLiteral("Data/Generals.str")), changed);
	window.reload();
	step(prompts.changes == 1 && p.db.labels[1].texts[0].revision == 2, QStringLiteral("changed text: new revision"));
	step(stateRows(window, QStringLiteral("outdated")) == 1, QStringLiteral("German: 1 outdated"));
	step(QFile::exists(QDir(root).filePath(QStringLiteral("Data/main_back_up.db"))), QStringLiteral("backup of main.db kept"));

	// game files and report
	window.generateFiles();
	for (const char *name : {"generals_us.csf", "generals_ge.csf"})
	{
		QFile f(QDir(folder).filePath(QString::fromLatin1(name)));
		const QByteArray csf = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
		int version = 0;
		int language = 0;
		std::vector<Engine::CsfEntry> entries;
		const bool ok = Engine::readCsf(reinterpret_cast<const unsigned char *>(csf.constData()), static_cast<std::size_t>(csf.size()), version, language, entries) ==
			Engine::CsfError::None;
		step(ok && entries.size() == 4, QStringLiteral("%1 reads in the game's reader").arg(QString::fromLatin1(name)));
	}
	// a game file of the language as source: the French texts come from the German .csf
	window.setLanguage(Lang::French);
	window.importGameFile(QDir(folder).filePath(QStringLiteral("generals_ge.csf")));
	const Translation *fromCsf = p.db.labels[0].texts[0].translation(Lang::French);
	step(fromCsf && fromCsf->text == u"OK" && stateRows(window, QStringLiteral("ok")) == 4, QStringLiteral("translations taken from a .csf"));
	window.setLanguage(Lang::German);
	window.writeReport();
	QFile reportFile(QDir(folder).filePath(QStringLiteral("report.txt")));
	const QByteArray reportText = reportFile.open(QIODevice::ReadOnly) ? reportFile.readAll() : QByteArray();
	step(reportText.contains("German Status:") && reportText.contains("Retranslation: 1"), QStringLiteral("report written"));

	// checks
	window.verifyTexts();
	step(window.logText().contains(QStringLiteral("Retranslations")), QStringLiteral("verify texts finds the outdated translation"));
	window.verifySpeech();
	const Translation *helloGe = p.db.labels[2].texts[0].translation(Lang::German);
	step(prompts.speeches == 1 && helloGe && helloGe->wave.valid, QStringLiteral("German recording verified"));
	window.setLanguage(Lang::US);
	window.verifySpeech();
	step(prompts.speeches == 2 && p.db.labels[2].texts[0].waveInfo.valid, QStringLiteral("US recording verified"));
	window.setLanguage(Lang::French);
	window.verifySpeech();
	step(prompts.speeches == 2 && window.logText().contains(QStringLiteral("Missing dialog   : 1")), QStringLiteral("French recording missing"));
	window.setLanguage(Lang::German);
	window.setSearch(QStringLiteral("hello"));
	step(window.rowCount() == 1, QStringLiteral("search"));
	window.setSearch(QString());

	// pictures: the window in both languages and every dialog
	const auto pic = [&](QWidget &w, const char *name) { step(picture(w, QDir(folder).filePath(QString::fromLatin1(name))), QStringLiteral("picture %1").arg(QString::fromLatin1(name))); };
	window.resize(1280, 800);
	pic(window, "babylon.png");
	window.switchInterfaceLanguage(true);
	step(window.actionButton(QStringLiteral("reload")) && window.actionButton(QStringLiteral("reload"))->text() != QStringLiteral("Reload") &&
			window.rowCount() == 4,
		QStringLiteral("German interface, table rebuilt"));
	pic(window, "babylon_de.png");
	window.switchInterfaceLanguage(false);
	window.show();
	const Label &label = p.db.labels[1];
	Text older = label.texts[0];
	older.text = u"%d units built";
	ChangeDialog change(&window, label, label.texts[0], older);
	pic(change, "change.png");
	MatchDialog match(&window, label, label.texts[0], {0});
	pic(match, "match.png");
	SameTextDialog sameText(&window, QStringLiteral("something else"), QStringLiteral("%d units ready"), 10001);
	pic(sameText, "sametext.png");
	SpeechDialog speech(&window, p.db.labels[2], QStringLiteral("Hello, commander."), QStringLiteral("dialog/e/HELLO01e.wav"));
	pic(speech, "speech.png");
	ExportChoice ec;
	ec.file = exported;
	ExportDialog exportDialog(&window, ec, [&p](const ExportChoice &c) { return exportCount(p.db, c.lang, c.options, p.dialogFiles()); });
	pic(exportDialog, "export.png");
	GenerateChoice gc;
	gc.folder = folder;
	gc.languages = {Lang::US, Lang::German};
	GenerateDialog generate(&window, gc);
	step(generate.choice().options.format == GameFileFormat::Csf && generate.choice().options.untranslated == Untranslated::UseIds,
		QStringLiteral("generate dialog keeps format and untranslated choice apart"));
	pic(generate, "generate.png");
	ReportChoice rc;
	rc.file = QDir(folder).filePath(QStringLiteral("report.txt"));
	rc.languages = {Lang::German};
	ReportDialog reportDialog(&window, rc);
	pic(reportDialog, "report.png");
	ProceedDialog proceed(&window, QStringLiteral("The translation database is not up to date."));
	pic(proceed, "proceed.png");

	// closing saves the changes of the speech check
	prompts.saveAnswer = SaveAnswer::Save;
	window.close();
	Project check;
	check.paths = p.paths;
	check.load();
	const Translation *saved = check.db.labels.size() == 4 ? check.db.labels[2].texts[0].translation(Lang::German) : nullptr;
	step(saved && saved->wave.valid, QStringLiteral("closing saved main.db"));
	step(QFile::exists(QDir(root).filePath(QStringLiteral("Data/babylon.log"))), QStringLiteral("log appended to babylon.log"));

	const int failed = static_cast<int>(report.filter(QStringLiteral("FAIL")).size());
	report << QStringLiteral("%1 steps, %2 failed").arg(report.size()).arg(failed);
	QFile out(QDir(folder).filePath(QStringLiteral("selftest.txt")));
	if (out.open(QIODevice::WriteOnly | QIODevice::Text))
		QTextStream(&out) << report.join(QLatin1Char('\n')) << '\n';
	for (const QString &line : report)
		std::printf("%s\n", line.toUtf8().constData());
	return step.ok() ? 0 : 1;
}

} // namespace BabylonApp
