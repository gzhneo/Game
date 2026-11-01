/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowPreferences.cpp -- the settings page of the editor: theme, language, game data, keyboard.

#include "App/MainWindow.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Model/MapDump.h"
#include "Model/MapIO.h"
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace MapEditorApp
{

// Settings

QWidget *MainWindow::buildSettingsPage()
{
	// In a scroll area: in a window lower than the cards, the layout would otherwise squeeze the rows,
	// since word-wrapped hints ask for almost no height.
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *page = new QWidget;
	page->setObjectName(QStringLiteral("Page"));
	scroll->setWidget(page);
	auto *outer = new QVBoxLayout(page);
	outer->setContentsMargins(48, 36, 48, 36);
	outer->setSpacing(18);
	auto *head = new QHBoxLayout;
	auto *back = new QPushButton(T("Back"));
	back->setCursor(Qt::PointingHandCursor);
	connect(back, &QPushButton::clicked, this, [this] { showPage(m_state.map ? Page::Map : Page::Start); });
	head->addWidget(label(T("Settings"), "Title"), 1);
	head->addWidget(back);
	outer->addLayout(head);

	auto card = [&](const QString &title) {
		outer->addWidget(label(title, "Heading"));
		auto *box = new QWidget;
		box->setObjectName(QStringLiteral("Panel"));
		box->setAttribute(Qt::WA_StyledBackground);
		box->setMaximumWidth(860);
		const Tokens t = tokens(m_state.theme, m_state.game);
		box->setStyleSheet(QStringLiteral("QWidget#Panel { border: 1px solid %1; border-radius: 10px; }").arg(t.bd.name()));
		auto *v = new QVBoxLayout(box);
		v->setContentsMargins(18, 8, 18, 8);
		v->setSpacing(0);
		outer->addWidget(box);
		return v;
	};
	auto settingRow = [&](QVBoxLayout *box, const QString &title, const QString &hint, QWidget *control) {
		if (box->count() > 0)
			box->addWidget(hLine());
		auto *r = new QHBoxLayout;
		r->setContentsMargins(0, 12, 0, 12);
		auto *texts = new QVBoxLayout;
		texts->setSpacing(2);
		auto *tt = label(title, nullptr);
		tt->setStyleSheet(QStringLiteral("font-weight: 600;"));
		texts->addWidget(tt);
		auto *h = label(hint, "Hint");
		h->setWordWrap(true);
		texts->addWidget(h);
		r->addLayout(texts, 1);
		r->addSpacing(24);
		r->addWidget(control);
		box->addLayout(r);
	};
	auto later = [this](auto change) {
		return [this, change](int index) {
			change(index);
			m_state.save();
			QTimer::singleShot(0, this, [this] { applySettings(); });
		};
	};

	QVBoxLayout *look = card(T("Appearance"));
	settingRow(look, T("Language"), T("Interface of the editor. Unit names come from the game data."),
		segmented({QStringLiteral("Deutsch"), QStringLiteral("English")}, m_state.language == Language::German ? 0 : 1,
			later([this](int i) { m_state.language = i == 0 ? Language::German : Language::English; })));
	settingRow(look, T("Theme"), T("Dark is easy on the eyes in long sessions, light suits bright rooms."),
		segmented({T("Dark"), T("Light")}, m_state.theme == ThemeKind::Dark ? 0 : 1,
			later([this](int i) { m_state.theme = i == 0 ? ThemeKind::Dark : ThemeKind::Light; })));
	settingRow(look, T("Game"), T("Accent colour and map folders follow the game."),
		segmented({QStringLiteral("Generals"), QStringLiteral("Zero Hour")}, m_state.game == Game::Generals ? 0 : 1,
			later([this](int i) { m_state.game = i == 0 ? Game::Generals : Game::ZeroHour; })));

	QVBoxLayout *files = card(T("Folders"));
	settingRow(files, T("Generals maps"), QDir::toNativeSeparators(m_state.userMapFolder(Game::Generals)), new QWidget);
	settingRow(files, T("Zero Hour maps"), QDir::toNativeSeparators(m_state.userMapFolder(Game::ZeroHour)), new QWidget);

	QVBoxLayout *saving = card(T("Autosave"));
	auto *autosaveBox = new QWidget;
	auto *autosaveRow = new QHBoxLayout(autosaveBox);
	autosaveRow->setContentsMargins(0, 0, 0, 0);
	auto *autosaveOn = new QCheckBox(T("On"));
	autosaveOn->setChecked(m_state.autosave);
	auto *autosaveMinutes = new QSpinBox;
	autosaveMinutes->setRange(1, 60);
	autosaveMinutes->setSuffix(QStringLiteral(" min"));
	autosaveMinutes->setValue(std::max(1, m_state.autosaveSeconds / 60));
	autosaveRow->addWidget(autosaveOn);
	autosaveRow->addWidget(autosaveMinutes);
	connect(autosaveOn, &QCheckBox::toggled, this, [this](bool on) {
		m_state.autosave = on;
		m_state.save();
		startAutosaveTimer();
	});
	connect(autosaveMinutes, &QSpinBox::valueChanged, this, [this](int minutes) {
		m_state.autosaveSeconds = minutes * 60;
		m_state.save();
		startAutosaveTimer();
	});
	settingRow(saving, T("Autosave"), T("While a map has unsaved changes, it is saved into the editor's folder; the start page offers to recover it after a crash."), autosaveBox);

	QVBoxLayout *gamePrograms = card(T("Test in game"));
	for (Game g : {Game::Generals, Game::ZeroHour})
	{
		const QString program = m_state.gameProgram(g);
		auto *choose = new QPushButton(T("Choose\u2026"));
		choose->setCursor(Qt::PointingHandCursor);
		connect(choose, &QPushButton::clicked, this, [this, g] {
			const QString chosen = QFileDialog::getOpenFileName(this, T("Game program"), m_state.gameProgram(g), T("Programs (*.exe);;All files (*)"));
			if (chosen.isEmpty())
				return;
			m_state.setGameProgram(g, QDir::toNativeSeparators(chosen));
			m_state.save();
			QTimer::singleShot(0, this, [this] { rebuild(); });
		});
		settingRow(gamePrograms, gameName(g), program.isEmpty() ? T("Not set: the game's program, started with -win -file <map>.") : program, choose);
	}

	QVBoxLayout *keys = card(T("Keyboard"));
	auto *editKeys = new QPushButton(T("Edit\u2026"));
	editKeys->setObjectName(QStringLiteral("EditShortcuts"));
	editKeys->setCursor(Qt::PointingHandCursor);
	connect(editKeys, &QPushButton::clicked, this, [this] { showShortcuts(); });
	settingRow(keys, T("Shortcuts"), T("Give menu commands your own keys; they stay when the language changes."), editKeys);
	QVBoxLayout *gameData = card(T("Game data for the 3D view"));
	for (Game g : {Game::Generals, Game::ZeroHour})
	{
		const QString folder = m_state.dataFolder(g);
		auto *choose = new QPushButton(T("Choose\u2026"));
		choose->setCursor(Qt::PointingHandCursor);
		connect(choose, &QPushButton::clicked, this, [this, g] { chooseDataFolder(g); });
		settingRow(gameData, gameName(g), folder.isEmpty() ? T("Not set: the folder with the game's .big archives, or the asset copy of the build.") : folder, choose);
	}
	outer->addStretch(1);
	return scroll;
}

} // namespace MapEditorApp
