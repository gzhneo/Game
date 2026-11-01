/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/MainWindow.h"

#include "Core/Checks.h"
#include "Core/Exchange.h"
#include "Core/GameImport.h"
#include "Core/Generate.h"
#include "EditorCommon/Language.h"
#include "EditorCommon/Widgets.h"
#include "Model/Sheet.h"
#include "Model/Text.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace Babylon;
using namespace EditorCommon;

namespace BabylonApp
{

namespace
{

enum Column
{
	COL_LABEL,
	COL_ID,
	COL_REV,
	COL_STATE,
	COL_ENGLISH,
	COL_TRANSLATION
};

enum Filter
{
	FILTER_ALL,
	FILTER_WORK,
	FILTER_SPEECH,
	FILTER_OBSOLETE
};

const int ROLE_LABEL = Qt::UserRole;
const int ROLE_TEXT = Qt::UserRole + 1;

class PromptResolver : public UpdateResolver
{
public:
	PromptResolver(Prompts &prompts, QWidget *parent) : m_prompts(prompts), m_parent(parent) {}
	ChangeDecision changedText(const Label &label, const Text &newText, const Text &oldText) override
	{
		return m_prompts.changedText(m_parent, label, newText, oldText);
	}
	MatchDecision matchText(const Label &label, const Text &newText, const std::vector<int> &candidates) override
	{
		return m_prompts.matchText(m_parent, label, newText, candidates);
	}

private:
	Prompts &m_prompts;
	QWidget *m_parent;
};

class PromptVerifier : public ImportVerifier
{
public:
	PromptVerifier(Prompts &prompts, QWidget *parent) : m_prompts(prompts), m_parent(parent) {}
	bool sameText(const std::u16string &sheetEnglish, const std::u16string &dbEnglish, int stringId) override
	{
		return m_prompts.sameText(m_parent, sheetEnglish, dbEnglish, stringId);
	}

private:
	Prompts &m_prompts;
	QWidget *m_parent;
};

// The state of a text in a language, as shown in the table.
QString textState(const Label &label, const Text &text, Lang lang)
{
	if (text.text.empty())
		return T("empty");
	if (lang == Lang::US)
		return label.maxLen && static_cast<int>(text.text.size()) > label.maxLen ? T("too long") : T("ok");
	const Translation *t = text.translation(lang);
	if (!t)
		return T("missing");
	if (text.revision > t->revision)
		return T("outdated");
	if (label.maxLen && static_cast<int>(t->text.size()) > label.maxLen)
		return T("too long");
	if (!sameFormat(t->text, text.text))
		return T("format");
	return T("ok");
}

QString esc(const std::u16string &s)
{
	return qs(s).toHtmlEscaped();
}

QString fromPath(const std::filesystem::path &p)
{
	return QDir::toNativeSeparators(QString::fromStdString(pathText(p)));
}

std::filesystem::path toPath(const QString &s)
{
	return pathFromText(QDir::fromNativeSeparators(s).toStdString());
}

} // namespace

MainWindow::MainWindow() : m_prompts(std::make_unique<Prompts>())
{
	setObjectName(QStringLiteral("BabylonWindow"));
	m_project.log = [this](const std::string &line) { log(QString::fromStdString(line)); };
	m_lastExport.options.filter = ExportFilter::Changes;
	m_lastGenerate.languages = {Lang::US};
	m_lastReport.languages = {Lang::German};
	m_lastReport.options.translations = true;
	buildUi();
	resize(1280, 800);
	log(QStringLiteral("Babylon"));
	log(T("Session date: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
}

MainWindow::~MainWindow() = default;

void MainWindow::setPrompts(std::unique_ptr<Prompts> prompts)
{
	m_prompts = std::move(prompts);
}

void MainWindow::buildUi()
{
	setWindowTitle(QStringLiteral("Babylon[*]"));
	m_actions.clear();
	menuBar()->clear();

	QMenu *projectMenu = menuBar()->addMenu(T("&Project"));
	projectMenu->addAction(T("Open folder..."), this, [this]() {
		const QString start = fromPath(m_project.paths.str.parent_path().parent_path());
		const QString folder = m_prompts->openFolder(this, T("Folder with Data/Generals.str"), start);
		if (!folder.isEmpty())
			openProject(ProjectPaths::inFolder(toPath(folder)));
	});
	projectMenu->addAction(T("Reload"), this, [this]() { reload(); });
	projectMenu->addAction(T("Save"), this, [this]() { save(); });
	projectMenu->addSeparator();
	projectMenu->addAction(T("Quit"), this, [this]() { close(); });
	QMenu *helpMenu = menuBar()->addMenu(T("&Help"));
	helpMenu->addAction(QStringLiteral("English"), this, [this]() { switchInterfaceLanguage(false); });
	helpMenu->addAction(QStringLiteral("Deutsch"), this, [this]() { switchInterfaceLanguage(true); });
	helpMenu->addSeparator();
	helpMenu->addAction(T("About Babylon"), this, [this]() {
		m_prompts->message(this, T("About Babylon"),
			T("Babylon keeps the translations of the game's texts: Generals.str is the English master, main.db holds every translation, translators work in spreadsheets (.xlsx, .ods, .csv) that any office suite opens, and the game loads the generated .csf files."));
	});

	auto *central = new QWidget(this);
	central->setObjectName(QStringLiteral("Page"));
	auto *outer = new QVBoxLayout(central);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(0);

	// top bar: language, filter, search
	auto *top = new QWidget(central);
	top->setObjectName(QStringLiteral("TopBar"));
	auto *topRow = new QHBoxLayout(top);
	topRow->setContentsMargins(14, 8, 14, 8);
	topRow->addWidget(label(QStringLiteral("Babylon"), "Heading", top));
	m_projectLabel = label(QString(), "Hint", top);
	topRow->addWidget(m_projectLabel, 1);
	topRow->addWidget(label(T("Language"), "Secondary", top));
	m_langCombo = new QComboBox(top);
	for (const LangInfo &info : languages())
		m_langCombo->addItem(languageName(info.id), static_cast<int>(info.id));
	m_langCombo->setCurrentIndex(m_langCombo->findData(static_cast<int>(m_lang)));
	topRow->addWidget(m_langCombo);
	m_filterCombo = new QComboBox(top);
	m_filterCombo->addItems({T("All texts"), T("Needs work"), T("Speech texts"), T("Obsolete texts")});
	m_filterCombo->setCurrentIndex(m_filter);
	topRow->addWidget(m_filterCombo);
	m_searchEdit = new QLineEdit(m_search, top);
	m_searchEdit->setPlaceholderText(T("Search label, text or ID"));
	m_searchEdit->setClearButtonEnabled(true);
	m_searchEdit->setMinimumWidth(220);
	topRow->addWidget(m_searchEdit);
	outer->addWidget(top);
	connect(m_langCombo, &QComboBox::currentIndexChanged, this, [this]() { setLanguage(static_cast<Lang>(m_langCombo->currentData().toInt())); });
	connect(m_filterCombo, &QComboBox::currentIndexChanged, this, [this](int i) { setFilter(i); });
	connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &t) { setSearch(t); });

	auto *body = new QSplitter(Qt::Horizontal, central);
	outer->addWidget(body, 1);

	// actions
	auto *panel = new QWidget(body);
	panel->setObjectName(QStringLiteral("Panel"));
	auto *actions = new QVBoxLayout(panel);
	actions->setContentsMargins(10, 12, 10, 12);
	actions->setSpacing(2);
	const auto section = [&](const QString &title) {
		actions->addSpacing(8);
		actions->addWidget(label(title.toUpper(), "Section", panel));
	};
	const auto action = [&](const char *key, const QString &text, const QString &tip, auto fn) {
		auto *b = new QPushButton(text, panel);
		b->setObjectName(QStringLiteral("Action"));
		b->setToolTip(tip);
		actions->addWidget(b);
		connect(b, &QPushButton::clicked, this, fn);
		m_actions[QString::fromLatin1(key)] = b;
	};
	section(T("Database"));
	action("reload", T("Reload"), T("Read Generals.str and main.db again"), [this]() { reload(); });
	action("update", T("Update from Generals.str"), T("Take new, changed and removed texts of Generals.str into the database"), [this]() { updateDatabase(); });
	action("save", T("Save"), T("Save main.db (the old file stays as main_back_up.db)"), [this]() { save(); });
	section(QStringLiteral("Generals.str"));
	action("errors", T("Errors"), T("List the errors of Generals.str"), [this]() { showErrors(); });
	action("warnings", T("Warnings"), T("List the warnings of Generals.str"), [this]() { showWarnings(); });
	section(T("Translators"));
	action("export", T("Export sheet..."), T("A spreadsheet for translators (.xlsx, .ods, .csv)"), [this]() { exportSheet(); });
	action("import", T("Import sheet..."), T("Take the translations of a returned sheet"), [this]() { importSheet(); });
	action("gameFile", T("Take a game file..."), T("Translations of the chosen language from a .csf or .str file of the game, matched by label"),
		[this]() { importGameFile(); });
	action("sent", T("Mark as sent..."), T("Mark the texts of a sheet as sent to the translators"), [this]() { markSent(); });
	section(T("Game"));
	action("generate", T("Generate game files..."), T("Write <prefix>_<xx>.csf or .str for the chosen languages"), [this]() { generateFiles(); });
	action("report", T("Report..."), T("Write the state of the translations and speech files to a text file"), [this]() { writeReport(); });
	section(T("Check"));
	action("verifyText", T("Verify texts"), T("Missing, outdated, too long and badly formatted translations"), [this]() { verifyTexts(); });
	action("verifySpeech", T("Verify speech"), T("Listen to recordings that are not verified yet"), [this]() { verifySpeech(); });
	action("resetSpeech", T("Reset speech"), T("Forget which recordings of the language were verified"), [this]() { resetSpeech(); });
	actions->addStretch(1);
	panel->setMinimumWidth(220);

	// table and log
	auto *middle = new QSplitter(Qt::Vertical, body);
	m_table = new QTreeWidget(middle);
	m_table->setObjectName(QStringLiteral("TextTable"));
	m_table->setRootIsDecorated(false);
	m_table->setUniformRowHeights(true);
	m_table->setAlternatingRowColors(false);
	m_table->setHeaderLabels({T("Label"), T("ID"), T("Rev."), T("State"), T("English"), T("Translation")});
	m_table->header()->resizeSection(COL_LABEL, 200);
	m_table->header()->resizeSection(COL_ID, 56);
	m_table->header()->resizeSection(COL_REV, 36);
	m_table->header()->resizeSection(COL_STATE, 80);
	m_table->header()->resizeSection(COL_ENGLISH, 300);
	connect(m_table, &QTreeWidget::currentItemChanged, this, [this]() { showDetails(); });
	m_log = new QPlainTextEdit(middle);
	m_log->setObjectName(QStringLiteral("Log"));
	m_log->setReadOnly(true);
	QFont mono(QStringLiteral("monospace"));
	mono.setStyleHint(QFont::TypeWriter);
	m_log->setFont(mono);
	m_log->setPlainText(m_logLines.join(QLatin1Char('\n')));
	middle->setStretchFactor(0, 3);
	middle->setStretchFactor(1, 1);

	// details
	auto *detailScroll = new QScrollArea(body);
	detailScroll->setWidgetResizable(true);
	auto *detailPanel = new QWidget(detailScroll);
	detailPanel->setObjectName(QStringLiteral("Panel"));
	auto *detailLayout = new QVBoxLayout(detailPanel);
	m_details = new QLabel(detailPanel);
	m_details->setObjectName(QStringLiteral("Details"));
	m_details->setWordWrap(true);
	m_details->setTextFormat(Qt::RichText);
	m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detailLayout->addWidget(m_details);
	detailLayout->addStretch(1);
	detailScroll->setWidget(detailPanel);
	detailScroll->setMinimumWidth(260);
	body->setStretchFactor(0, 0);
	body->setStretchFactor(1, 1);
	body->setStretchFactor(2, 0);
	body->setSizes({230, 800, 300});

	setCentralWidget(central);
	delete m_strState;
	delete m_dbState;
	m_strState = new QLabel(this);
	m_dbState = new QLabel(this);
	statusBar()->addWidget(m_strState);
	statusBar()->addWidget(m_dbState, 1);
	refreshTable();
	refreshState();
}

void MainWindow::switchInterfaceLanguage(bool german)
{
	EditorCommon::setLanguage(german ? Language::German : Language::English);
	QSettings().setValue(QStringLiteral("language"), german ? QStringLiteral("de") : QStringLiteral("en"));
	buildUi();
}

void MainWindow::log(const QString &line)
{
	m_logLines << line;
	if (m_log)
		m_log->appendPlainText(line);
}

void MainWindow::logLines(const std::vector<std::string> &lines, const QString &indent)
{
	for (const std::string &l : lines)
		log(indent + QString::fromStdString(l));
}

QString MainWindow::logText() const
{
	return m_logLines.join(QLatin1Char('\n'));
}

QPushButton *MainWindow::actionButton(const QString &key) const
{
	const auto it = m_actions.find(key);
	return it == m_actions.end() ? nullptr : it->second;
}

void MainWindow::openProject(const ProjectPaths &paths)
{
	if (!canProceed())
		return;
	m_project.paths = paths;
	QSettings().setValue(QStringLiteral("project"), fromPath(paths.str.parent_path().parent_path()));
	m_project.db.modified = false;
	reload();
}

void MainWindow::reload()
{
	if (!canProceed())
		return;
	log(QString());
	m_project.load();
	m_operateAlways = false;
	refreshTable();
	refreshState();
	if (!m_project.errors.empty())
	{
		m_prompts->message(this, QStringLiteral("Babylon"),
			T("Generals.str has %1 errors. Click \"Errors\" for the list; the database cannot be updated until they are fixed.").arg(m_project.errors.size()));
		return;
	}
	if (!m_project.strOutOfDate())
		return;
	if (m_project.dbReadOnly)
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("Generals.str has changed, but the database is read only, so it cannot be updated."));
		return;
	}
	const QString question = m_project.dbLoaded ? T("Generals.str has changed. Update the database now?") : T("New database. Fill it from Generals.str now?");
	if (m_prompts->yesNo(this, QStringLiteral("Babylon"), question))
		updateDatabase();
}

void MainWindow::updateDatabase()
{
	if (!m_project.canUpdate())
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("The database cannot be updated (Generals.str missing or with errors, or the database is read only)."));
		return;
	}
	log(QString());
	PromptResolver resolver(*m_prompts, this);
	m_project.update(resolver);
	refreshTable();
	refreshState();
}

bool MainWindow::save()
{
	std::string error;
	if (!m_project.save(error))
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("Saving failed: %1").arg(QString::fromStdString(error)));
		refreshState();
		return false;
	}
	refreshState();
	return true;
}

bool MainWindow::canProceed()
{
	if (!m_project.db.modified)
		return true;
	switch (m_prompts->unsaved(this))
	{
	case SaveAnswer::Save:
		return save();
	case SaveAnswer::Discard:
		m_project.db.modified = false;
		return true;
	case SaveAnswer::Cancel:
		break;
	}
	return false;
}

bool MainWindow::canOperate()
{
	if (m_operateAlways)
		return true;
	QString reason;
	if (!m_project.errors.empty())
		reason = T("Generals.str has errors, so the translation database is not up to date. Fix Generals.str first.");
	else if (m_project.strOutOfDate())
		reason = T("The translation database is not up to date: Generals.str has changed since the last update. Update the database first.");
	if (reason.isEmpty())
		return true;
	const Proceed answer = m_prompts->proceed(this, reason);
	if (answer == Proceed::Always)
		m_operateAlways = true;
	return answer != Proceed::No;
}

void MainWindow::showErrors()
{
	log(QString());
	log(T("Generals.str errors:"));
	for (const Finding &f : m_project.errors)
		log(QStringLiteral("  ") + QString::fromStdString(findingText(f)));
	log(T("Total errors: %1").arg(m_project.errors.size()));
}

void MainWindow::showWarnings()
{
	log(QString());
	log(T("Generals.str warnings:"));
	for (const Finding &f : m_project.warnings)
		log(QStringLiteral("  ") + QString::fromStdString(findingText(f)));
	log(T("Total warnings: %1").arg(m_project.warnings.size()));
}

void MainWindow::exportSheet()
{
	if (!canOperate())
		return;
	ExportChoice choice = m_lastExport;
	choice.lang = m_lang;
	if (choice.file.isEmpty() || QFileInfo(choice.file).completeBaseName().startsWith(QStringLiteral("Generals_")))
	{
		const LangInfo *info = langInfo(m_lang);
		const QString folder = choice.file.isEmpty() ? fromPath(m_project.paths.db.parent_path()) : QFileInfo(choice.file).absolutePath();
		const QString ext = choice.file.isEmpty() ? QStringLiteral("xlsx") : QFileInfo(choice.file).suffix();
		choice.file = QDir::toNativeSeparators(QDir(folder).filePath(QStringLiteral("Generals_%1.%2").arg(QString::fromLatin1(info ? info->initials : "xx"), ext)));
	}
	const DialogFiles files = m_project.dialogFiles();
	const auto count = [this, &files](const ExportChoice &c) { return exportCount(m_project.db, c.lang, c.options, files); };
	if (!m_prompts->exportOptions(this, choice, count))
		return;
	m_lastExport = choice;
	exportTo(choice);
}

bool MainWindow::exportTo(const ExportChoice &choice)
{
	const SheetFormat format = sheetFormatFromName(choice.file.toStdString());
	const DialogFiles files = m_project.dialogFiles();
	log(QString());
	log(T("Exporting %1 texts (%2) to \"%3\"...").arg(languageName(choice.lang), QString::fromLatin1(exportFilterName(choice.options.filter)), choice.file));
	const int count = exportCount(m_project.db, choice.lang, choice.options, files);
	if (!count)
	{
		log(T("  Nothing to export."));
		m_prompts->message(this, QStringLiteral("Babylon"), T("Nothing to export."));
		return false;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (format == SheetFormat::Unknown || !writeSheet(Babylon::exportSheet(m_project.db, choice.lang, choice.options, files), format, bytes) ||
		!writeFile(toPath(choice.file), bytes, error))
	{
		const QString why = format == SheetFormat::Unknown ? T("the file must end in .xlsx, .ods or .csv") : QString::fromStdString(error);
		log(T("  FAILED: %1").arg(why));
		m_prompts->message(this, QStringLiteral("Babylon"), T("Export failed: %1").arg(why));
		return false;
	}
	log(T("  %1 texts exported. DONE").arg(count));
	return true;
}

void MainWindow::importSheet(const QString &given)
{
	if (!canOperate())
		return;
	if (!m_project.canSave())
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("The database is read only or could not be read; nothing can be imported."));
		return;
	}
	const QString file = given.isEmpty() ? m_prompts->openSheet(this, T("Import translation sheet")) : given;
	if (file.isEmpty())
		return;
	log(QString());
	log(T("Importing \"%1\"...").arg(file));
	std::vector<std::uint8_t> bytes;
	std::string error;
	Sheet sheet;
	if (!readFile(toPath(file), bytes, error) || !readSheet(bytes, sheet, error))
	{
		log(T("  FAILED: %1").arg(QString::fromStdString(error)));
		m_prompts->message(this, QStringLiteral("Babylon"), T("The sheet cannot be read: %1").arg(QString::fromStdString(error)));
		return;
	}
	PromptVerifier verifier(*m_prompts, this);
	const ImportResult r = Babylon::importSheet(m_project.db, sheet, verifier);
	if (r.ok)
		log(T("  Language: %1").arg(languageName(r.lang)));
	logLines(r.log);
	if (!r.ok)
		m_prompts->message(this, QStringLiteral("Babylon"),
			r.badIds ? T("The sheet has wrong string IDs. Fix them and import again.") : T("The file is no translation sheet of a known language."));
	refreshTable();
	refreshState();
}

void MainWindow::markSent(const QString &given)
{
	if (!canOperate())
		return;
	const QString file = given.isEmpty() ? m_prompts->openSheet(this, T("Sheet that was sent to the translators")) : given;
	if (file.isEmpty())
		return;
	log(QString());
	log(T("Marking the texts of \"%1\" as sent...").arg(file));
	std::vector<std::uint8_t> bytes;
	std::string error;
	Sheet sheet;
	if (!readFile(toPath(file), bytes, error) || !readSheet(bytes, sheet, error))
	{
		log(T("  FAILED: %1").arg(QString::fromStdString(error)));
		return;
	}
	PromptVerifier verifier(*m_prompts, this);
	logLines(Babylon::markSent(m_project.db, sheet, verifier).log);
}

void MainWindow::importGameFile(const QString &given)
{
	if (!canOperate())
		return;
	if (!m_project.canSave())
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("The database is read only or could not be read; nothing can be imported."));
		return;
	}
	if (m_lang == Lang::US)
	{
		m_prompts->message(this, QStringLiteral("Babylon"), T("US English comes from Generals.str. Choose the language of the game file first."));
		return;
	}
	const QString file = given.isEmpty() ? m_prompts->openGameFile(this, T("%1 game file").arg(languageName(m_lang))) : given;
	if (file.isEmpty())
		return;
	log(QString());
	log(T("Taking the %1 texts of \"%2\"...").arg(languageName(m_lang), file));
	std::vector<std::uint8_t> bytes;
	std::vector<StrOutLabel> labels;
	std::string error;
	if (!readFile(toPath(file), bytes, error) || !readGameFile(bytes, labels, error))
	{
		log(T("  FAILED: %1").arg(QString::fromStdString(error)));
		m_prompts->message(this, QStringLiteral("Babylon"), T("The file cannot be read: %1").arg(QString::fromStdString(error)));
		return;
	}
	logLines(Babylon::importGameFile(m_project.db, m_lang, labels).log);
	refreshTable();
	refreshState();
}

void MainWindow::generateFiles()
{
	if (!canOperate())
		return;
	GenerateChoice choice = m_lastGenerate;
	if (choice.folder.isEmpty())
		choice.folder = fromPath(m_project.paths.db.parent_path());
	if (!m_prompts->generateOptions(this, choice))
		return;
	m_lastGenerate = choice;
	generateTo(choice);
}

bool MainWindow::generateTo(const GenerateChoice &choice)
{
	GenerateOptions options = choice.options;
	options.generatedOn = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
	const DialogFiles files = m_project.dialogFiles();
	log(QString());
	log(T("Generating game files:"));
	bool all = true;
	for (const Lang lang : choice.languages)
	{
		const GeneratedFile f = generateGameFile(m_project.db, choice.prefix.toStdString(), lang, options, files);
		const std::filesystem::path target = toPath(choice.folder) / pathFromText(f.fileName);
		std::string error = f.error;
		const bool ok = f.ok && writeFile(target, f.data, error);
		all = all && ok;
		const QString head = T("  Writing %1 - %2...").arg(fromPath(target), languageName(lang));
		log(head + (!ok ? T("FAILED: %1").arg(QString::fromStdString(error)) : f.warnings.empty() ? T("OK") : T("WARNING")));
		logLines(f.warnings, QStringLiteral("    "));
	}
	return all;
}

void MainWindow::writeReport()
{
	if (!canOperate())
		return;
	ReportChoice choice = m_lastReport;
	if (choice.file.isEmpty())
		choice.file = QDir::toNativeSeparators(QDir(fromPath(m_project.paths.db.parent_path())).filePath(QStringLiteral("babylon_report.txt")));
	if (!m_prompts->reportOptions(this, choice))
		return;
	m_lastReport = choice;
	reportTo(choice);
}

bool MainWindow::reportTo(const ReportChoice &choice)
{
	log(QString());
	log(T("Writing report \"%1\"...").arg(choice.file));
	const std::string text = reportText(m_project.db, choice.languages, choice.options, m_project.dialogFiles(),
		QDateTime::currentDateTime().toString(Qt::ISODate).toStdString());
	std::string error;
	if (!writeFile(toPath(choice.file), text, error))
	{
		log(T("  FAILED: %1").arg(QString::fromStdString(error)));
		return false;
	}
	log(T("  DONE"));
	return true;
}

void MainWindow::verifyTexts()
{
	if (!canOperate())
		return;
	log(QString());
	log(T("Verifying %1 text...").arg(languageName(m_lang)));
	const TranslationReport r = reportTranslations(m_project.db, m_lang);
	if (r.problems() < 100)
		reportTranslations(m_project.db, m_lang, [this](const std::string &line) { log(QStringLiteral("  String ") + QString::fromStdString(line)); });
	logLines(translationSummary(r, m_lang));
}

void MainWindow::verifySpeech()
{
	if (!canOperate())
		return;
	log(QString());
	log(T("Verifying %1 dialog...").arg(languageName(m_lang)));
	const DialogFiles files = m_project.dialogFiles();
	const LangInfo *info = langInfo(m_lang);
	recheckDialog(m_project.db, m_lang, files);
	bool stop = false;
	for (Label &label : m_project.db.labels)
	{
		for (Text &text : label.texts)
		{
			if (!text.isDialog() || !info || !files.size(text, *info) || dialogConfirmed(text, m_lang, files, false))
				continue;
			const Translation *t = text.translation(m_lang);
			const QString shown = m_lang == Lang::US ? qs(text.text) : t ? qs(t->text) : T("No translation!");
			const SpeechAnswer a = m_prompts->speech(this, label, shown, fromPath(files.path(text, *info)));
			if (a == SpeechAnswer::Stop)
			{
				stop = true;
				break;
			}
			if (a == SpeechAnswer::Matches && confirmDialog(text, m_lang, files))
				m_project.db.modified = true;
		}
		if (stop)
			break;
	}
	const DialogReport r = reportDialog(m_project.db, m_lang, files);
	if (r.problems() < 100)
		reportDialog(m_project.db, m_lang, files, [this](const std::string &line) { log(QStringLiteral("  String ") + QString::fromStdString(line)); });
	logLines(dialogSummary(r));
	refreshState();
}

void MainWindow::resetSpeech()
{
	if (m_prompts->yesNo(this, QStringLiteral("Babylon"), T("Forget for all %1 recordings that they were verified?").arg(languageName(m_lang))))
	{
		invalidateDialog(m_project.db, m_lang);
		log(T("Speech verification of %1 reset.").arg(languageName(m_lang)));
		refreshState();
	}
}

void MainWindow::setLanguage(Lang lang)
{
	m_lang = lang;
	if (m_langCombo && m_langCombo->currentData().toInt() != static_cast<int>(lang))
		m_langCombo->setCurrentIndex(m_langCombo->findData(static_cast<int>(lang)));
	refreshTable();
}

void MainWindow::setFilter(int filter)
{
	m_filter = filter;
	if (m_filterCombo && m_filterCombo->currentIndex() != filter)
		m_filterCombo->setCurrentIndex(filter);
	refreshTable();
}

void MainWindow::setSearch(const QString &text)
{
	m_search = text;
	if (m_searchEdit && m_searchEdit->text() != text)
		m_searchEdit->setText(text);
	refreshTable();
}

int MainWindow::rowCount() const
{
	return m_table ? m_table->topLevelItemCount() : 0;
}

QString MainWindow::cellText(int row, int column) const
{
	if (!m_table || row < 0 || row >= m_table->topLevelItemCount())
		return {};
	return m_table->topLevelItem(row)->text(column);
}

void MainWindow::refreshTable()
{
	if (!m_table)
		return;
	m_table->setUpdatesEnabled(false);
	m_table->clear();
	m_table->headerItem()->setText(COL_TRANSLATION, m_lang == Lang::US ? T("Translation") : languageName(m_lang));
	const QString search = m_search.trimmed();
	const auto matches = [&search](const QString &a, const QString &b, const QString &c, int id) {
		return search.isEmpty() || a.contains(search, Qt::CaseInsensitive) || b.contains(search, Qt::CaseInsensitive) ||
			c.contains(search, Qt::CaseInsensitive) || QString::number(id) == search;
	};
	QList<QTreeWidgetItem *> items;
	const QString ok = T("ok");
	if (m_filter == FILTER_OBSOLETE)
	{
		for (std::size_t i = 0; i < m_project.db.obsolete.size(); ++i)
		{
			const Text &text = m_project.db.obsolete[i];
			const Translation *t = text.translation(m_lang);
			const QString english = qs(text.text);
			const QString trans = t ? qs(t->text) : QString();
			if (!matches(QString(), english, trans, text.id))
				continue;
			auto *item = new QTreeWidgetItem({QString(), QString::number(text.id), QString::number(text.revision), T("obsolete"), english, trans});
			item->setData(0, ROLE_LABEL, -1);
			item->setData(0, ROLE_TEXT, static_cast<int>(i));
			items << item;
		}
	}
	else
	{
		for (std::size_t l = 0; l < m_project.db.labels.size(); ++l)
		{
			const Label &label = m_project.db.labels[l];
			const QString name = qs(label.name);
			for (std::size_t i = 0; i < label.texts.size(); ++i)
			{
				const Text &text = label.texts[i];
				const QString state = textState(label, text, m_lang);
				if (m_filter == FILTER_WORK && (state == ok || text.text.empty()))
					continue;
				if (m_filter == FILTER_SPEECH && !text.isDialog())
					continue;
				const Translation *t = text.translation(m_lang);
				const QString english = qs(text.text);
				const QString trans = m_lang == Lang::US ? QString() : t ? qs(t->text) : QString();
				if (!matches(name, english, trans, text.id))
					continue;
				auto *item = new QTreeWidgetItem({name, QString::number(text.id), QString::number(text.revision), state, english, trans});
				item->setData(0, ROLE_LABEL, static_cast<int>(l));
				item->setData(0, ROLE_TEXT, static_cast<int>(i));
				items << item;
			}
		}
	}
	m_table->addTopLevelItems(items);
	if (!items.isEmpty())
		m_table->setCurrentItem(items.front());
	m_table->setUpdatesEnabled(true);
	showDetails();
}

void MainWindow::refreshState()
{
	if (!m_strState)
		return;
	m_projectLabel->setText(m_project.paths.str.empty() ? QString() : fromPath(m_project.paths.str.parent_path().parent_path()));
	QString str;
	if (!m_project.strFound)
		str = T("Generals.str: not found");
	else if (!m_project.strLoaded)
		str = T("Generals.str: not readable");
	else
		str = T("Generals.str: %1 labels, %2 errors, %3 warnings").arg(m_project.str.labels.size()).arg(m_project.errors.size()).arg(m_project.warnings.size());
	m_strState->setText(str);
	QString db;
	if (m_project.dbBroken)
		db = T("main.db: cannot be read");
	else
		db = T("main.db: %1 labels, %2 texts").arg(m_project.db.labels.size()).arg(m_project.db.textCount());
	if (m_project.dbReadOnly)
		db += T(", read only");
	if (m_project.db.modified)
		db += T(", changed");
	if (m_project.strOutOfDate())
		db += T(", not up to date with Generals.str");
	m_dbState->setText(db);
	const bool haveDb = !m_project.dbBroken;
	const auto enable = [this](const char *key, bool on) {
		if (QPushButton *b = actionButton(QString::fromLatin1(key)))
			b->setEnabled(on);
	};
	enable("update", m_project.canUpdate() && m_project.strOutOfDate());
	enable("save", m_project.canSave() && m_project.db.modified);
	enable("errors", !m_project.errors.empty());
	enable("warnings", !m_project.warnings.empty());
	enable("export", haveDb && !m_project.db.labels.empty());
	enable("import", m_project.canSave() && !m_project.db.labels.empty());
	enable("sent", haveDb && !m_project.db.labels.empty());
	enable("gameFile", m_project.canSave() && !m_project.db.labels.empty());
	enable("generate", haveDb && !m_project.db.labels.empty());
	enable("report", haveDb && !m_project.db.labels.empty());
	enable("verifyText", haveDb && !m_project.db.labels.empty());
	enable("verifySpeech", haveDb && !m_project.db.labels.empty());
	enable("resetSpeech", haveDb && !m_project.db.labels.empty());
	setWindowModified(m_project.db.modified);
}

void MainWindow::showDetails()
{
	if (!m_details)
		return;
	QTreeWidgetItem *item = m_table ? m_table->currentItem() : nullptr;
	if (!item)
	{
		m_details->setText(T("<p>Select a text to see its details.</p>"));
		return;
	}
	const int l = item->data(0, ROLE_LABEL).toInt();
	const int i = item->data(0, ROLE_TEXT).toInt();
	const Text *text = nullptr;
	const Label *label = nullptr;
	if (l < 0)
	{
		if (i >= 0 && i < static_cast<int>(m_project.db.obsolete.size()))
			text = &m_project.db.obsolete[static_cast<std::size_t>(i)];
	}
	else if (l < static_cast<int>(m_project.db.labels.size()))
	{
		label = &m_project.db.labels[static_cast<std::size_t>(l)];
		if (i >= 0 && i < static_cast<int>(label->texts.size()))
			text = &label->texts[static_cast<std::size_t>(i)];
	}
	if (!text)
		return;
	QString html;
	html += QStringLiteral("<h3>%1</h3>").arg(label ? esc(label->name) : T("Obsolete text"));
	const auto row = [&html](const QString &key, const QString &value) {
		if (!value.isEmpty())
			html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(key, value);
	};
	if (label)
	{
		row(T("Comment"), esc(label->comment));
		row(T("Context"), esc(label->context));
		row(T("Speaker"), esc(label->speaker));
		row(T("Listener"), esc(label->listener));
		if (label->maxLen)
			row(T("Maximum length"), QString::number(label->maxLen));
	}
	row(T("String ID / revision"), QStringLiteral("%1 / %2").arg(text->id).arg(text->revision));
	row(T("Speech file"), esc(text->wave));
	row(T("English"), esc(text->text) + QStringLiteral(" <i>(%1)</i>").arg(text->text.size()));
	for (const Translation &t : text->translations)
	{
		QString value = esc(t.text) + QStringLiteral(" <i>(%1)</i>").arg(t.text.size());
		if (t.revision < text->revision)
			value += QStringLiteral(" <b>") + T("outdated (revision %1)").arg(t.revision) + QStringLiteral("</b>");
		if (!t.comment.empty())
			value += QStringLiteral("<br><i>") + esc(t.comment) + QStringLiteral("</i>");
		row(languageName(t.lang), value);
	}
	m_details->setText(html);
}

void MainWindow::saveLog()
{
	const std::filesystem::path folder = m_project.paths.db.parent_path();
	std::error_code ec;
	if (folder.empty() || !std::filesystem::is_directory(folder, ec))
		return;
	QFile file(fromPath(folder / "babylon.log"));
	if (!file.open(QIODevice::Append | QIODevice::Text))
		return;
	QTextStream out(&file);
	out << "\nLOG START ******************\n\n" << logText() << "\n\nQuitting Babylon\n\nLOG END ******************\n\n";
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	if (!canProceed())
	{
		event->ignore();
		return;
	}
	saveLog();
	event->accept();
}

} // namespace BabylonApp
