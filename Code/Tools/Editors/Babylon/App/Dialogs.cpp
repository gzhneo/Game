/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Dialogs.h"

#include "EditorCommon/Language.h"
#include "EditorCommon/Widgets.h"
#include "Model/Text.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

using namespace Babylon;
using namespace EditorCommon;

namespace BabylonApp
{

QString qs(const std::u16string &text)
{
	return QString::fromStdU16String(text);
}

QString languageName(Lang lang)
{
	switch (lang)
	{
	case Lang::US:
		return T("US English");
	case Lang::UK:
		return T("UK English");
	case Lang::German:
		return T("German");
	case Lang::French:
		return T("French");
	case Lang::Spanish:
		return T("Spanish");
	case Lang::Italian:
		return T("Italian");
	case Lang::Japanese:
		return T("Japanese");
	case Lang::Jabber:
		return T("Jabberwockie (test)");
	case Lang::Korean:
		return T("Korean");
	case Lang::Chinese:
		return T("Chinese");
	case Lang::Unknown:
		break;
	}
	return QString::fromLatin1(langName(lang));
}

namespace
{

QPlainTextEdit *textBox(const QString &text, QWidget *parent)
{
	auto *box = new QPlainTextEdit(text, parent);
	box->setReadOnly(true);
	box->setMinimumHeight(70);
	box->setMaximumHeight(140);
	return box;
}

QPushButton *button(const QString &text, QWidget *parent, bool primary = false)
{
	auto *b = new QPushButton(text, parent);
	if (primary)
		b->setObjectName(QStringLiteral("Primary"));
	return b;
}

// File name with a browse button; filter empty: a folder.
QLineEdit *pathRow(QVBoxLayout *layout, QWidget *parent, const QString &caption, const QString &value, const QString &filter, bool save)
{
	layout->addWidget(label(caption, "Section", parent));
	auto *row = new QHBoxLayout();
	auto *edit = new QLineEdit(value, parent);
	auto *browse = button(T("Browse..."), parent);
	row->addWidget(edit, 1);
	row->addWidget(browse);
	layout->addLayout(row);
	QObject::connect(browse, &QPushButton::clicked, parent, [edit, parent, filter, save, caption]() {
		QString chosen;
		if (filter.isEmpty())
			chosen = QFileDialog::getExistingDirectory(parent, caption, edit->text());
		else if (save)
			chosen = QFileDialog::getSaveFileName(parent, caption, edit->text(), filter);
		else
			chosen = QFileDialog::getOpenFileName(parent, caption, edit->text(), filter);
		if (!chosen.isEmpty())
			edit->setText(QDir::toNativeSeparators(chosen));
	});
	return edit;
}

// Languages as a list with check boxes.
QListWidget *languageList(QWidget *parent, const std::vector<Lang> &checked)
{
	auto *list = new QListWidget(parent);
	for (const LangInfo &info : languages())
	{
		auto *item = new QListWidgetItem(languageName(info.id), list);
		item->setData(Qt::UserRole, static_cast<int>(info.id));
		bool on = false;
		for (const Lang l : checked)
			on = on || l == info.id;
		item->setCheckState(on ? Qt::Checked : Qt::Unchecked);
	}
	list->setMinimumHeight(200);
	return list;
}

std::vector<Lang> checkedLanguages(const QListWidget *list)
{
	std::vector<Lang> out;
	for (int i = 0; i < list->count(); ++i)
		if (list->item(i)->checkState() == Qt::Checked)
			out.push_back(static_cast<Lang>(list->item(i)->data(Qt::UserRole).toInt()));
	return out;
}

QHBoxLayout *selectButtons(QListWidget *list, QWidget *parent)
{
	auto *row = new QHBoxLayout();
	auto *all = button(T("Select all"), parent);
	auto *invert = button(T("Invert"), parent);
	row->addWidget(all);
	row->addWidget(invert);
	row->addStretch(1);
	QObject::connect(all, &QPushButton::clicked, parent, [list]() {
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setCheckState(Qt::Checked);
	});
	QObject::connect(invert, &QPushButton::clicked, parent, [list]() {
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setCheckState(list->item(i)->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
	});
	return row;
}

QString sheetFilter()
{
	return T("Spreadsheet (*.xlsx *.ods *.csv);;Excel workbook (*.xlsx);;OpenDocument spreadsheet (*.ods);;CSV text (*.csv)");
}

} // namespace

// ------------------------------------------------------------------ ChangeDialog

ChangeDialog::ChangeDialog(QWidget *parent, const Label &label, const Text &newText, const Text &oldText) : QDialog(parent)
{
	setWindowTitle(T("Text changed in Generals.str"));
	setObjectName(QStringLiteral("ChangeDialog"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(EditorCommon::label(T("Label %1").arg(qs(label.name)), "Heading", this));
	layout->addWidget(EditorCommon::label(T("Was"), "Section", this));
	layout->addWidget(textBox(qs(oldText.text), this));
	layout->addWidget(EditorCommon::label(T("Now"), "Section", this));
	layout->addWidget(textBox(qs(newText.text), this));
	auto *hint = EditorCommon::label(
		T("Does the change alter the meaning? Then every language must translate it again. A correction (typo, punctuation) keeps the translations."), "Hint", this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	auto *row = new QHBoxLayout();
	auto *redo = button(T("New meaning: translate again"), this, true);
	auto *keep = button(T("Correction: keep translations"), this);
	auto *skip = button(T("Skip label"), this);
	auto *skipAll = button(T("Skip all"), this);
	redo->setObjectName(QStringLiteral("Primary"));
	row->addWidget(redo);
	row->addWidget(keep);
	row->addStretch(1);
	row->addWidget(skip);
	row->addWidget(skipAll);
	layout->addLayout(row);
	const auto answer = [this](ChangeDecision d) {
		m_decision = d;
		accept();
	};
	connect(redo, &QPushButton::clicked, this, [answer]() { answer(ChangeDecision::Retranslate); });
	connect(keep, &QPushButton::clicked, this, [answer]() { answer(ChangeDecision::Keep); });
	connect(skip, &QPushButton::clicked, this, [answer]() { answer(ChangeDecision::SkipLabel); });
	connect(skipAll, &QPushButton::clicked, this, [answer]() { answer(ChangeDecision::SkipAll); });
	resize(620, 380);
}

// ------------------------------------------------------------------ MatchDialog

MatchDialog::MatchDialog(QWidget *parent, const Label &label, const Text &newText, const std::vector<int> &candidates) : QDialog(parent)
{
	setWindowTitle(T("Unmatched text in label %1").arg(qs(label.name)));
	setObjectName(QStringLiteral("MatchDialog"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(EditorCommon::label(T("New text"), "Section", this));
	layout->addWidget(textBox(qs(newText.text), this));
	layout->addWidget(EditorCommon::label(T("Replaces the old text"), "Section", this));
	auto *combo = new QComboBox(this);
	for (const int c : candidates)
		combo->addItem(qs(label.texts[static_cast<std::size_t>(c)].text), c);
	layout->addWidget(combo);
	auto *retranslate = new QCheckBox(T("New meaning: translate again"), this);
	retranslate->setChecked(true);
	layout->addWidget(retranslate);
	auto *row = new QHBoxLayout();
	auto *match = button(T("Match"), this, true);
	auto *none = button(T("No match: new text"), this);
	auto *skip = button(T("Skip label"), this);
	auto *skipAll = button(T("Skip all"), this);
	row->addWidget(match);
	row->addWidget(none);
	row->addStretch(1);
	row->addWidget(skip);
	row->addWidget(skipAll);
	layout->addLayout(row);
	connect(match, &QPushButton::clicked, this, [this, combo, retranslate]() {
		m_decision.match = combo->currentData().toInt();
		m_decision.decision = retranslate->isChecked() ? ChangeDecision::Retranslate : ChangeDecision::Keep;
		accept();
	});
	connect(none, &QPushButton::clicked, this, [this]() {
		m_decision.match = -1;
		m_decision.decision = ChangeDecision::Keep;
		accept();
	});
	connect(skip, &QPushButton::clicked, this, [this]() {
		m_decision.decision = ChangeDecision::SkipLabel;
		accept();
	});
	connect(skipAll, &QPushButton::clicked, this, [this]() {
		m_decision.decision = ChangeDecision::SkipAll;
		accept();
	});
	resize(600, 320);
}

// ------------------------------------------------------------------ SameTextDialog

SameTextDialog::SameTextDialog(QWidget *parent, const QString &sheetEnglish, const QString &dbEnglish, int stringId) : QDialog(parent)
{
	setWindowTitle(T("Check the imported row"));
	setObjectName(QStringLiteral("SameTextDialog"));
	auto *layout = new QVBoxLayout(this);
	auto *hint = EditorCommon::label(T("The English text of string %1 in the sheet differs from the database and its label does not confirm it. Is it still the same text? If not, the string IDs of the sheet are wrong and the import stops.")
										 .arg(stringId),
		"Hint", this);
	hint->setWordWrap(true);
	layout->addWidget(hint);
	layout->addWidget(EditorCommon::label(T("In the sheet"), "Section", this));
	layout->addWidget(textBox(sheetEnglish, this));
	layout->addWidget(EditorCommon::label(T("In the database"), "Section", this));
	layout->addWidget(textBox(dbEnglish, this));
	auto *row = new QHBoxLayout();
	auto *same = button(T("Same text: import"), this, true);
	auto *other = button(T("Different: stop the import"), this);
	row->addStretch(1);
	row->addWidget(same);
	row->addWidget(other);
	layout->addLayout(row);
	connect(same, &QPushButton::clicked, this, &QDialog::accept);
	connect(other, &QPushButton::clicked, this, &QDialog::reject);
	resize(600, 380);
}

// ------------------------------------------------------------------ SpeechDialog

SpeechDialog::SpeechDialog(QWidget *parent, const Label &label, const QString &text, const QString &file) : QDialog(parent)
{
	setWindowTitle(T("Verify speech"));
	setObjectName(QStringLiteral("SpeechDialog"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(EditorCommon::label(qs(label.name), "Heading", this));
	if (!label.speaker.empty())
		layout->addWidget(EditorCommon::label(T("Speaker: %1").arg(qs(label.speaker)), "Secondary", this));
	layout->addWidget(textBox(text, this));
	auto *fileLabel = EditorCommon::label(QDir::toNativeSeparators(file), "Hint", this);
	fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(fileLabel);
	auto *row = new QHBoxLayout();
	auto *play = button(T("Play"), this);
	auto *ok = button(T("Matches the text"), this, true);
	auto *skip = button(T("Does not match"), this);
	auto *stop = button(T("Stop"), this);
	row->addWidget(play);
	row->addStretch(1);
	row->addWidget(ok);
	row->addWidget(skip);
	row->addWidget(stop);
	layout->addLayout(row);
	connect(play, &QPushButton::clicked, this, [file]() { QDesktopServices::openUrl(QUrl::fromLocalFile(file)); });
	connect(ok, &QPushButton::clicked, this, [this]() {
		m_answer = SpeechAnswer::Matches;
		accept();
	});
	connect(skip, &QPushButton::clicked, this, [this]() {
		m_answer = SpeechAnswer::Skip;
		accept();
	});
	connect(stop, &QPushButton::clicked, this, [this]() {
		m_answer = SpeechAnswer::Stop;
		reject();
	});
	resize(560, 300);
}

// ------------------------------------------------------------------ ExportDialog

ExportDialog::ExportDialog(QWidget *parent, const ExportChoice &start, std::function<int(const ExportChoice &)> count)
	: QDialog(parent), m_choice(start), m_count(std::move(count))
{
	setWindowTitle(T("Export translation sheet"));
	setObjectName(QStringLiteral("ExportDialog"));
	auto *layout = new QVBoxLayout(this);
	layout->addWidget(EditorCommon::label(T("Language"), "Section", this));
	auto *lang = new QComboBox(this);
	for (const LangInfo &info : languages())
	{
		lang->addItem(languageName(info.id), static_cast<int>(info.id));
		if (info.id == start.lang)
			lang->setCurrentIndex(lang->count() - 1);
	}
	layout->addWidget(lang);

	layout->addWidget(EditorCommon::label(T("Texts"), "Section", this));
	auto *group = new QButtonGroup(this);
	const std::pair<ExportFilter, QString> filters[] = {
		{ExportFilter::Changes, T("Texts that need work (missing, outdated, too long, wrong format)")},
		{ExportFilter::All, T("All texts")},
		{ExportFilter::Sample, T("A sample (about 50 texts)")},
		{ExportFilter::Dialog, T("Speech texts only")},
		{ExportFilter::NonDialog, T("Texts without speech")},
		{ExportFilter::MissingDialog, T("Speech texts whose recording is missing")},
		{ExportFilter::Unverified, T("Speech texts whose recording is not verified")},
		{ExportFilter::Unsent, T("Texts not marked as sent")},
	};
	for (const auto &[filter, text] : filters)
	{
		auto *radio = new QRadioButton(text, this);
		radio->setChecked(filter == start.options.filter);
		group->addButton(radio, static_cast<int>(filter));
		layout->addWidget(radio);
	}
	auto *withTrans = new QCheckBox(T("Fill in the current translations"), this);
	withTrans->setChecked(start.options.includeTranslations);
	layout->addWidget(withTrans);
	auto *protect = new QCheckBox(T("Protect the sheet: only the translation column can be edited"), this);
	protect->setChecked(start.options.protect);
	layout->addWidget(protect);
	QLineEdit *file = pathRow(layout, this, T("File (.xlsx, .ods or .csv)"), start.file, sheetFilter(), true);
	auto *countLabel = EditorCommon::label(QString(), "Hint", this);
	layout->addWidget(countLabel);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(T("Export"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("Primary"));
	buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
	layout->addWidget(buttons);

	m_read = [this, lang, group, withTrans, protect, file]() {
		m_choice.lang = static_cast<Lang>(lang->currentData().toInt());
		m_choice.options.filter = static_cast<ExportFilter>(group->checkedId());
		m_choice.options.includeTranslations = withTrans->isChecked();
		m_choice.options.protect = protect->isChecked();
		m_choice.file = file->text();
	};
	m_showCount = [countLabel, buttons, file](int n) {
		countLabel->setText(n ? T("%1 texts will be exported").arg(n) : T("Nothing to export"));
		buttons->button(QDialogButtonBox::Ok)->setEnabled(n > 0 && sheetFormatFromName(file->text().toStdString()) != SheetFormat::Unknown);
	};
	connect(lang, &QComboBox::currentIndexChanged, this, [this, lang, file]() {
		// suggest a file name for the language in the same folder
		const LangInfo *info = langInfo(static_cast<Lang>(lang->currentData().toInt()));
		QFileInfo fi(file->text());
		const QString ext = fi.suffix().isEmpty() ? QStringLiteral("xlsx") : fi.suffix();
		if (info)
			file->setText(QDir::toNativeSeparators(fi.dir().filePath(QStringLiteral("Generals_%1.%2").arg(QString::fromLatin1(info->initials), ext))));
		refresh();
	});
	connect(group, &QButtonGroup::idClicked, this, [this]() { refresh(); });
	connect(file, &QLineEdit::textChanged, this, [this]() { refresh(); });
	connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
		m_read();
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	refresh();
	resize(560, 560);
}

void ExportDialog::refresh()
{
	m_read();
	m_showCount(m_count ? m_count(m_choice) : 0);
}

ExportChoice ExportDialog::choice() const
{
	return m_choice;
}

// ------------------------------------------------------------------ GenerateDialog

GenerateDialog::GenerateDialog(QWidget *parent, const GenerateChoice &start) : QDialog(parent)
{
	setWindowTitle(T("Generate game files"));
	setObjectName(QStringLiteral("GenerateDialog"));
	auto *layout = new QVBoxLayout(this);
	QLineEdit *folder = pathRow(layout, this, T("Folder"), start.folder, QString(), true);
	layout->addWidget(label(T("File name"), "Section", this));
	auto *prefix = new QLineEdit(start.prefix, this);
	layout->addWidget(prefix);
	auto *preview = label(QString(), "Hint", this);
	layout->addWidget(preview);
	auto *csf = new QRadioButton(T("Compiled .csf (what the game loads)"), this);
	auto *str = new QRadioButton(T("Text .str"), this);
	(start.options.format == GameFileFormat::Csf ? csf : str)->setChecked(true);
	auto *formatGroup = new QButtonGroup(this);
	formatGroup->addButton(csf);
	formatGroup->addButton(str);
	layout->addWidget(label(T("Format"), "Section", this));
	layout->addWidget(csf);
	layout->addWidget(str);
	auto *ids = new QRadioButton(T("String ID, so gaps stand out in the game"), this);
	auto *original = new QRadioButton(T("English text"), this);
	(start.options.untranslated == Untranslated::UseIds ? ids : original)->setChecked(true);
	auto *untranslatedGroup = new QButtonGroup(this);
	untranslatedGroup->addButton(ids);
	untranslatedGroup->addButton(original);
	layout->addWidget(label(T("Untranslated texts"), "Section", this));
	layout->addWidget(ids);
	layout->addWidget(original);
	layout->addWidget(label(T("Languages"), "Section", this));
	QListWidget *list = languageList(this, start.languages);
	layout->addWidget(list);
	layout->addLayout(selectButtons(list, this));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(T("Generate"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("Primary"));
	buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
	layout->addWidget(buttons);

	m_read = [start, folder, prefix, csf, ids, list]() {
		GenerateChoice c = start;
		c.folder = folder->text();
		c.prefix = prefix->text().trimmed();
		c.options.format = csf->isChecked() ? GameFileFormat::Csf : GameFileFormat::Str;
		c.options.untranslated = ids->isChecked() ? Untranslated::UseIds : Untranslated::UseOriginal;
		c.languages = checkedLanguages(list);
		return c;
	};
	const auto update = [this, preview, buttons]() {
		const GenerateChoice c = m_read();
		preview->setText(QString::fromStdString(gameFileName(c.prefix.toStdString(), Lang::German, c.options.format)).replace(QStringLiteral("_ge."), QStringLiteral("_{xx}.")));
		buttons->button(QDialogButtonBox::Ok)->setEnabled(!c.prefix.isEmpty() && !c.languages.empty());
	};
	connect(prefix, &QLineEdit::textChanged, this, update);
	connect(csf, &QRadioButton::toggled, this, update);
	connect(list, &QListWidget::itemChanged, this, update);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	update();
	resize(520, 640);
}

// ------------------------------------------------------------------ ReportDialog

ReportDialog::ReportDialog(QWidget *parent, const ReportChoice &start) : QDialog(parent)
{
	setWindowTitle(T("Write report"));
	setObjectName(QStringLiteral("ReportDialog"));
	auto *layout = new QVBoxLayout(this);
	QLineEdit *file = pathRow(layout, this, T("File"), start.file, T("Text (*.txt)"), true);
	auto *translations = new QCheckBox(T("Translations"), this);
	translations->setChecked(start.options.translations);
	auto *dialog = new QCheckBox(T("Speech files"), this);
	dialog->setChecked(start.options.dialog);
	layout->addWidget(label(T("Contents"), "Section", this));
	layout->addWidget(translations);
	layout->addWidget(dialog);
	auto *limitRow = new QHBoxLayout();
	limitRow->addWidget(new QLabel(T("List each problem if there are fewer than"), this));
	auto *limit = new QSpinBox(this);
	limit->setRange(0, 100000);
	limit->setValue(start.options.limit);
	limitRow->addWidget(limit);
	limitRow->addStretch(1);
	layout->addLayout(limitRow);
	layout->addWidget(label(T("Languages"), "Section", this));
	QListWidget *list = languageList(this, start.languages);
	layout->addWidget(list);
	layout->addLayout(selectButtons(list, this));
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText(T("Write"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("Primary"));
	buttons->button(QDialogButtonBox::Cancel)->setText(T("Cancel"));
	layout->addWidget(buttons);
	m_read = [start, file, translations, dialog, limit, list]() {
		ReportChoice c = start;
		c.file = file->text();
		c.options.translations = translations->isChecked();
		c.options.dialog = dialog->isChecked();
		c.options.limit = limit->value();
		c.languages = checkedLanguages(list);
		return c;
	};
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	resize(480, 560);
}

// ------------------------------------------------------------------ ProceedDialog

ProceedDialog::ProceedDialog(QWidget *parent, const QString &reason) : QDialog(parent)
{
	setWindowTitle(QStringLiteral("Babylon"));
	setObjectName(QStringLiteral("ProceedDialog"));
	auto *layout = new QVBoxLayout(this);
	auto *text = label(reason, nullptr, this);
	text->setWordWrap(true);
	layout->addWidget(text);
	layout->addWidget(label(T("Do you want to continue anyway?"), "Secondary", this));
	auto *row = new QHBoxLayout();
	auto *yes = button(T("Continue"), this);
	auto *always = button(T("Always continue"), this);
	auto *no = button(T("Cancel"), this, true);
	row->addStretch(1);
	row->addWidget(yes);
	row->addWidget(always);
	row->addWidget(no);
	layout->addLayout(row);
	connect(yes, &QPushButton::clicked, this, [this]() {
		m_answer = Proceed::Yes;
		accept();
	});
	connect(always, &QPushButton::clicked, this, [this]() {
		m_answer = Proceed::Always;
		accept();
	});
	connect(no, &QPushButton::clicked, this, &QDialog::reject);
	resize(480, 200);
}

// ------------------------------------------------------------------ Prompts

ChangeDecision Prompts::changedText(QWidget *parent, const Label &label, const Text &newText, const Text &oldText)
{
	ChangeDialog dialog(parent, label, newText, oldText);
	return dialog.exec() == QDialog::Accepted ? dialog.decision() : ChangeDecision::SkipAll;
}

MatchDecision Prompts::matchText(QWidget *parent, const Label &label, const Text &newText, const std::vector<int> &candidates)
{
	MatchDialog dialog(parent, label, newText, candidates);
	if (dialog.exec() != QDialog::Accepted)
		return {ChangeDecision::SkipAll, -1};
	return dialog.decision();
}

bool Prompts::sameText(QWidget *parent, const std::u16string &sheetEnglish, const std::u16string &dbEnglish, int stringId)
{
	SameTextDialog dialog(parent, qs(sheetEnglish), qs(dbEnglish), stringId);
	return dialog.exec() == QDialog::Accepted;
}

SpeechAnswer Prompts::speech(QWidget *parent, const Label &label, const QString &text, const QString &file)
{
	SpeechDialog dialog(parent, label, text, file);
	dialog.exec();
	return dialog.answer();
}

Proceed Prompts::proceed(QWidget *parent, const QString &reason)
{
	ProceedDialog dialog(parent, reason);
	dialog.exec();
	return dialog.answer();
}

SaveAnswer Prompts::unsaved(QWidget *parent)
{
	QMessageBox box(QMessageBox::Question, QStringLiteral("Babylon"), T("The translation database has changed. Save it?"), QMessageBox::NoButton, parent);
	QPushButton *save = box.addButton(T("Save"), QMessageBox::AcceptRole);
	QPushButton *discard = box.addButton(T("Discard changes"), QMessageBox::DestructiveRole);
	box.addButton(T("Cancel"), QMessageBox::RejectRole);
	box.exec();
	if (box.clickedButton() == save)
		return SaveAnswer::Save;
	if (box.clickedButton() == discard)
		return SaveAnswer::Discard;
	return SaveAnswer::Cancel;
}

bool Prompts::yesNo(QWidget *parent, const QString &title, const QString &text)
{
	QMessageBox box(QMessageBox::Question, title, text, QMessageBox::NoButton, parent);
	QPushButton *yes = box.addButton(T("Yes"), QMessageBox::YesRole);
	box.addButton(T("No"), QMessageBox::NoRole);
	box.exec();
	return box.clickedButton() == yes;
}

void Prompts::message(QWidget *parent, const QString &title, const QString &text)
{
	QMessageBox::information(parent, title, text);
}

bool Prompts::exportOptions(QWidget *parent, ExportChoice &choice, const std::function<int(const ExportChoice &)> &count)
{
	ExportDialog dialog(parent, choice, count);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	choice = dialog.choice();
	return true;
}

bool Prompts::generateOptions(QWidget *parent, GenerateChoice &choice)
{
	GenerateDialog dialog(parent, choice);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	choice = dialog.choice();
	return true;
}

bool Prompts::reportOptions(QWidget *parent, ReportChoice &choice)
{
	ReportDialog dialog(parent, choice);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	choice = dialog.choice();
	return true;
}

QString Prompts::openSheet(QWidget *parent, const QString &title)
{
	return QFileDialog::getOpenFileName(parent, title, QString(), sheetFilter());
}

QString Prompts::openGameFile(QWidget *parent, const QString &title)
{
	return QFileDialog::getOpenFileName(parent, title, QString(), T("String files of the game (*.csf *.str)"));
}

QString Prompts::openFolder(QWidget *parent, const QString &title, const QString &start)
{
	return QFileDialog::getExistingDirectory(parent, title, start);
}

} // namespace BabylonApp
