/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindow.h -- the Babylon window: the actions of the original tool on the left, the database as
// a table with the texts of the chosen language in the middle, the details of a text on the right and
// the log below.

#pragma once

#include "App/Dialogs.h"
#include "Core/Project.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

namespace BabylonApp
{

class MainWindow : public QMainWindow
{
public:
	MainWindow();
	~MainWindow() override;

	// Questions go here; the self test puts in fixed answers.
	void setPrompts(std::unique_ptr<Prompts> prompts);
	Babylon::Project &project() { return m_project; }

	// Opens the files and asks whether to update the database if Generals.str changed.
	void openProject(const Babylon::ProjectPaths &paths);
	void reload();
	void updateDatabase();
	bool save();
	void showErrors();
	void showWarnings();
	void exportSheet();
	void importSheet(const QString &file = QString());
	void markSent(const QString &file = QString());
	// Translations of the current language from a .csf or .str file of the game, matched by label.
	void importGameFile(const QString &file = QString());
	void generateFiles();
	void writeReport();
	void verifyTexts();
	void verifySpeech();
	void resetSpeech();

	// Without dialogs, for the self test and the actions above.
	bool exportTo(const ExportChoice &choice);
	bool generateTo(const GenerateChoice &choice);
	bool reportTo(const ReportChoice &choice);

	void setLanguage(Babylon::Lang lang);
	[[nodiscard]] Babylon::Lang language() const { return m_lang; }
	void setFilter(int filter);
	void setSearch(const QString &text);
	// Rows of the table and the text of the log, for the self test.
	[[nodiscard]] int rowCount() const;
	[[nodiscard]] QString cellText(int row, int column) const;
	[[nodiscard]] QString logText() const;
	[[nodiscard]] QPushButton *actionButton(const QString &key) const;
	void switchInterfaceLanguage(bool german);

protected:
	void closeEvent(QCloseEvent *event) override;

private:
	void buildUi();
	void refreshTable();
	void refreshState();
	void showDetails();
	void log(const QString &line);
	void logLines(const std::vector<std::string> &lines, const QString &indent = QStringLiteral("  "));
	bool canProceed();		// unsaved changes
	bool canOperate();		// Generals.str changed or with errors
	void saveLog();

	Babylon::Project m_project;
	std::unique_ptr<Prompts> m_prompts;
	Babylon::Lang m_lang = Babylon::Lang::German;
	int m_filter = 0;
	QString m_search;
	bool m_operateAlways = false;
	QStringList m_logLines;
	ExportChoice m_lastExport;
	GenerateChoice m_lastGenerate;
	ReportChoice m_lastReport;

	QComboBox *m_langCombo = nullptr;
	QComboBox *m_filterCombo = nullptr;
	QLineEdit *m_searchEdit = nullptr;
	QTreeWidget *m_table = nullptr;
	QLabel *m_details = nullptr;
	QPlainTextEdit *m_log = nullptr;
	QLabel *m_projectLabel = nullptr;
	QLabel *m_strState = nullptr;
	QLabel *m_dbState = nullptr;
	std::map<QString, QPushButton *> m_actions;
};

} // namespace BabylonApp
