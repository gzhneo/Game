/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/ParticlesWindow.h"

#include "App/EditorContext.h"
#include "App/EngineClient.h"
#include "App/Common.h"
#include "App/Language.h"
#include "App/ParticleCurves.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/AreaCommands.h"
#include "Core/ParticleIni.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>

namespace MapEditorApp
{

namespace
{

// Colour or alpha key fields, which the two editors above show instead of text fields.
bool isKeyField(const std::string &key)
{
	const bool alpha = key.rfind("Alpha", 0) == 0 && key.size() == 6;
	const bool color = key.rfind("Color", 0) == 0 && key.size() == 6;
	return (alpha || color) && key.back() >= '1' && key.back() <= '8';
}

} // namespace

ParticlesWindow::ParticlesWindow(EditorContext &editor, QWidget *parent) : QDialog(parent), m_editor(editor)
{
	setObjectName(QStringLiteral("ParticlesWindow"));
	setWindowTitle(T("Particle systems"));
	resize(1040, 680);
	auto *row = new QHBoxLayout(this);
	auto *left = new QVBoxLayout;
	auto *search = new QLineEdit(this);
	search->setObjectName(QStringLiteral("ParticleSearch"));
	search->setPlaceholderText(T("Search"));
	auto *list = new QListWidget(this);
	list->setObjectName(QStringLiteral("ParticleList"));
	auto *onlyChanged = new QCheckBox(T("Only changed systems"), this);
	onlyChanged->setObjectName(QStringLiteral("ParticleOnlyChanged"));
	left->addWidget(search);
	left->addWidget(onlyChanged);
	left->addWidget(list, 1);
	row->addLayout(left, 1);
	auto *right = new QVBoxLayout;
	auto *tabs = new QTabWidget(this);
	tabs->setObjectName(QStringLiteral("ParticleTabs"));
	auto *scroll = new QScrollArea(tabs);
	scroll->setObjectName(QStringLiteral("ParticleFields"));
	scroll->setWidgetResizable(true);
	tabs->addTab(scroll, T("Fields"));
	auto *text = new QPlainTextEdit(tabs);
	text->setObjectName(QStringLiteral("ParticleText"));
	QFont mono(QStringLiteral("Consolas"));
	mono.setStyleHint(QFont::Monospace);
	text->setFont(mono);
	text->setLineWrapMode(QPlainTextEdit::NoWrap);
	tabs->addTab(text, T("INI text"));
	auto *changes = new QTreeWidget(tabs);
	changes->setObjectName(QStringLiteral("ParticleChanges"));
	changes->setColumnCount(3);
	changes->setHeaderLabels({T("Field"), T("Game data"), T("Now")});
	changes->setRootIsDecorated(false);
	changes->header()->setSectionResizeMode(QHeaderView::Stretch);
	tabs->addTab(changes, T("Changes"));
	right->addWidget(tabs, 1);
	auto *buttons = new QHBoxLayout;
	const auto button = [&](const QString &caption, const char *name, const std::function<void()> &action) {
		auto *b = new QPushButton(caption, this);
		b->setObjectName(QString::fromLatin1(name));
		connect(b, &QPushButton::clicked, this, action);
		buttons->addWidget(b);
	};
	button(T("Apply"), "ParticleApply", [this, tabs, text] {
		// The text tab is taken as written; the fields build the block anew.
		const QString ini = tabs->currentIndex() == 1 ? text->toPlainText() : latin(MapEditorCore::writeParticleBlock(m_particleBlock));
		QStringList names;
		for (const std::string &name : MapEditorCore::particleBlockNames(ascii(ini)))
			names << latin(name);
		QString error;
		particleStep(names, [&ini, &error] { return EngineClient::instance().applyParticleIni(ini, &error); });
		if (!error.isEmpty())
		{
			m_editor.toast(T("The particle system cannot be loaded: %1").arg(error));
			return;
		}
		if (const std::optional<MapEditorCore::ParticleBlock> block = MapEditorCore::parseParticleBlock(ascii(ini)))
			selectParticle(latin(block->name));
		m_editor.toast(T("Applied; the preview shows the new values."));
	});
	button(T("Undo"), "ParticleUndo", [this] { undoParticles(); });
	button(T("Redo"), "ParticleRedo", [this] { redoParticles(); });
	button(T("Show in the view"), "ParticlePreview", [this] { previewSelectedParticle(); });
	this->findChild<QWidget *>(QStringLiteral("ParticlePreview"))->setToolTip(T("At the selected object, otherwise in the middle of the view."));
	// The this has its own undo list; the map's undo stays with the main this.
	auto *undoKey = new QShortcut(QKeySequence::Undo, this);
	connect(undoKey, &QShortcut::activated, this, [this] { undoParticles(); });
	auto *redoKey = new QShortcut(QKeySequence::Redo, this);
	connect(redoKey, &QShortcut::activated, this, [this] { redoParticles(); });
	button(T("Stop"), "ParticleStop", [this] { stopParticlePreview(); });
	buttons->addStretch(1);
	button(T("Reset"), "ParticleReset", [this] { resetParticles(false, false); });
	button(T("Reset all..."), "ParticleResetAll", [this] { resetParticles(true, true); });
	button(T("Test in game"), "ParticleTestInGame", [this] { emit testInGameRequested(); });
	button(T("Save all as..."), "ParticleSaveAll", [this] {
		const QString path = QFileDialog::getSaveFileName(this, T("Save all particle systems"),
			QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("ParticleSystem.ini")), T("INI files (*.ini)"));
		if (path.isEmpty())
			return;
		m_editor.toast(EngineClient::instance().saveAllParticles(path) ? T("Saved: %1").arg(path) : T("The file cannot be written: %1").arg(path));
	});
	right->addLayout(buttons);
	auto *options = new QHBoxLayout;
	auto *outline = new QCheckBox(T("Show the emission volume"), this);
	outline->setObjectName(QStringLiteral("ParticleOutline"));
	outline->setChecked(m_particleOutline);
	connect(outline, &QCheckBox::toggled, this, [this](bool on) {
		m_particleOutline = on;
		emit overlayChanged();
	});
	options->addWidget(outline);
	options->addStretch(1);
	options->addWidget(label(T("Particle limit:"), "Hint", this));
	auto *limit = new QSpinBox(this);
	limit->setObjectName(QStringLiteral("ParticleLimit"));
	limit->setRange(0, 50000);
	limit->setSingleStep(500);
	limit->setSpecialValueText(T("as in the game"));
	connect(limit, &QSpinBox::valueChanged, this, [](int count) { EngineClient::instance().setParticleLimit(count); });
	options->addWidget(limit);
	right->addLayout(options);
	auto *hint = label(T("Changes apply to this editor session and go along with Test in game, in a copy of the map. Saving writes all systems to a file of your choice; for the game it belongs in a mod or the game's Data\\INI folder."), "Hint", this);
	hint->setWordWrap(true);
	right->addWidget(hint);
	row->addLayout(right, 3);
	connect(search, &QLineEdit::textChanged, this, [this] { refreshParticleList(); });
	connect(onlyChanged, &QCheckBox::toggled, this, [this] { refreshParticleList(); });
	connect(list, &QListWidget::currentTextChanged, this, [this](const QString &name) {
		if (!name.isEmpty())
			loadParticle(name);
	});
	connect(this, &QDialog::finished, this, [this] { stopParticlePreview(); });
}


bool ParticlesWindow::previewRunning() const
{
	return m_particleTimer && m_particleTimer->isActive();
}

void ParticlesWindow::refreshParticleList()
{
	auto *list = this->findChild<QListWidget *>(QStringLiteral("ParticleList"));
	auto *search = this->findChild<QLineEdit *>(QStringLiteral("ParticleSearch"));
	auto *onlyChanged = child<QCheckBox>(this, "ParticleOnlyChanged");
	const QString current = list->currentItem() ? list->currentItem()->text() : m_particleBlockName;
	const QSignalBlocker block(list);
	list->clear();
	EngineClient &engine = EngineClient::instance();
	for (const QString &name : engine.particleNames())
	{
		if (!name.contains(search->text().trimmed(), Qt::CaseInsensitive))
			continue;
		// Changed systems stand out in bold, new ones in bold italics.
		const int change = engine.particleChange(name);
		if (change == 0 && onlyChanged && onlyChanged->isChecked())
			continue;
		auto *item = new QListWidgetItem(name, list);
		if (change != 0)
		{
			QFont font = item->font();
			font.setBold(true);
			font.setItalic(change == 2);
			item->setFont(font);
			item->setToolTip(change == 2 ? T("New in this session") : T("Changed in this session"));
		}
	}
	const QList<QListWidgetItem *> found = list->findItems(current, Qt::MatchExactly);
	if (!found.isEmpty())
		list->setCurrentItem(found.front());
}

void ParticlesWindow::selectParticle(const QString &name)
{
	refreshParticleList();
	auto *list = this->findChild<QListWidget *>(QStringLiteral("ParticleList"));
	const QList<QListWidgetItem *> found = list->findItems(name, Qt::MatchExactly);
	if (!found.isEmpty())
		list->setCurrentItem(found.front());
	loadParticle(name);
}

void ParticlesWindow::loadParticle(const QString &name)
{
	const QString ini = EngineClient::instance().particleIni(name);
	const std::optional<MapEditorCore::ParticleBlock> block = MapEditorCore::parseParticleBlock(ascii(ini));
	if (!block)
		return;
	m_particleBlock = *block;
	m_particleBlockName = name;
	this->findChild<QPlainTextEdit *>(QStringLiteral("ParticleText"))->setPlainText(ini);
	fillParticleFields();
	fillParticleChanges();
}

void ParticlesWindow::fillParticleChanges()
{
	auto *tree = true ? child<QTreeWidget>(this, "ParticleChanges") : nullptr;
	auto *tabs = true ? this->findChild<QTabWidget *>(QStringLiteral("ParticleTabs")) : nullptr;
	if (!tree || !tabs)
		return;
	tree->clear();
	EngineClient &engine = EngineClient::instance();
	const QString now = engine.particleIni(m_particleBlockName);
	const QString original = engine.particleOriginalIni(m_particleBlockName);
	int count = 0;
	if (!now.isEmpty() && original.isEmpty())
	{
		new QTreeWidgetItem(tree, {T("(new system)"), QString(), T("made in this session")});
	}
	else
	{
		const std::optional<MapEditorCore::ParticleBlock> before = MapEditorCore::parseParticleBlock(ascii(original));
		const std::optional<MapEditorCore::ParticleBlock> after = MapEditorCore::parseParticleBlock(ascii(now));
		if (before && after)
		{
			for (const MapEditorCore::ParticleChange &c : MapEditorCore::compareParticleBlocks(*before, *after))
			{
				new QTreeWidgetItem(tree, {latin(c.key), latin(c.before), latin(c.after)});
				++count;
			}
		}
	}
	tabs->setTabText(2, count > 0 ? T("Changes (%1)").arg(count) : T("Changes"));
}

void ParticlesWindow::particleStep(const QStringList &names, const std::function<bool()> &change)
{
	EngineClient &engine = EngineClient::instance();
	std::vector<ParticleStep> step;
	for (const QString &name : names)
		step.push_back(ParticleStep{name, engine.particleIni(name), QString()});
	if (!change())
		return;
	bool any = false;
	for (ParticleStep &s : step)
	{
		s.after = engine.particleIni(s.name);
		any = any || s.after != s.before;
	}
	if (!any)
		return;
	m_particleUndo.push_back(std::move(step));
	m_particleRedo.clear();
}

namespace
{

// Brings each named system to the given text; an empty text removes a system the editor made.
void restoreParticles(const std::vector<std::pair<QString, QString>> &texts)
{
	EngineClient &engine = EngineClient::instance();
	for (const auto &[name, text] : texts)
	{
		if (text.isEmpty())
			engine.resetParticles(name);
		else
			engine.applyParticleIni(text, nullptr);
	}
}

} // namespace

void ParticlesWindow::showParticleAgain()
{
	// A system the step removed leaves the editor empty.
	if (!EngineClient::instance().particleNames().contains(m_particleBlockName))
	{
		m_particleBlockName.clear();
		m_particleBlock = MapEditorCore::ParticleBlock{};
		refreshParticleList();
		this->findChild<QPlainTextEdit *>(QStringLiteral("ParticleText"))->clear();
		fillParticleFields();
		fillParticleChanges();
		return;
	}
	selectParticle(m_particleBlockName);
}

void ParticlesWindow::undoParticles()
{
	if (m_particleUndo.empty())
	{
		m_editor.toast(T("Nothing to undo."));
		return;
	}
	std::vector<ParticleStep> step = std::move(m_particleUndo.back());
	m_particleUndo.pop_back();
	std::vector<std::pair<QString, QString>> texts;
	for (const ParticleStep &s : step)
		texts.emplace_back(s.name, s.before);
	restoreParticles(texts);
	m_particleRedo.push_back(std::move(step));
	showParticleAgain();
}

void ParticlesWindow::redoParticles()
{
	if (m_particleRedo.empty())
	{
		m_editor.toast(T("Nothing to redo."));
		return;
	}
	std::vector<ParticleStep> step = std::move(m_particleRedo.back());
	m_particleRedo.pop_back();
	std::vector<std::pair<QString, QString>> texts;
	for (const ParticleStep &s : step)
		texts.emplace_back(s.name, s.after);
	restoreParticles(texts);
	m_particleUndo.push_back(std::move(step));
	showParticleAgain();
}

void ParticlesWindow::fillParticleFields()
{
	auto *scroll = this->findChild<QScrollArea *>(QStringLiteral("ParticleFields"));
	auto *page = new QWidget;
	auto *col = new QVBoxLayout(page);
	col->addWidget(label(latin(m_particleBlock.name), "Heading", page));

	// Colour and alpha over a particle's life on one time axis: the used keys, and at least the
	// particle's longest lifetime.
	FrameAxis axis;
	const std::vector<MapEditorCore::ColorKey> colors = MapEditorCore::usedColorKeys(m_particleBlock);
	const std::vector<MapEditorCore::AlphaKey> alphas = MapEditorCore::usedAlphaKeys(m_particleBlock);
	int last = 30;
	for (const MapEditorCore::ColorKey &k : colors)
		last = std::max(last, k.frame);
	for (const MapEditorCore::AlphaKey &k : alphas)
		last = std::max(last, k.frame);
	if (const std::string *lifetime = m_particleBlock.find("Lifetime"))
	{
		float low = 0.0f;
		float high = 0.0f;
		if (std::sscanf(lifetime->c_str(), " %f %f", &low, &high) == 2)
			last = std::max(last, static_cast<int>(high));
	}
	axis.last = last + last / 10;
	auto *gradient = new ColorGradient;
	gradient->setObjectName(QStringLiteral("ParticleColors"));
	gradient->keys = colors;
	gradient->axis = axis;
	gradient->onChanged = [this, gradient] { MapEditorCore::setColorKeys(m_particleBlock, gradient->keys); };
	auto *curve = new AlphaCurve;
	curve->setObjectName(QStringLiteral("ParticleAlpha"));
	curve->keys = alphas;
	curve->axis = axis;
	curve->onChanged = [this, curve] { MapEditorCore::setAlphaKeys(m_particleBlock, curve->keys); };
	col->addWidget(label(T("COLOUR OVER A PARTICLE'S LIFE"), "Section", page));
	col->addWidget(gradient);
	col->addWidget(label(T("ALPHA OVER A PARTICLE'S LIFE"), "Section", page));
	col->addWidget(curve);
	col->addWidget(label(T("0 to %1 frames (30 per second); the keys are Color1..8 and Alpha1..8 in the INI text.").arg(axis.last), "Hint", page));

	// Every other field of the block in file order: a choice, a switch, a texture or free text.
	col->addWidget(label(T("FIELDS"), "Section", page));
	auto *form = new QFormLayout;
	const std::string *type = m_particleBlock.find("Type");
	const bool drawable = type && *type == "DRAWABLE";
	for (const MapEditorCore::ParticleField &f : m_particleBlock.fields)
	{
		const std::string key = f.key;
		if (isKeyField(key))
			continue;
		const std::vector<std::string> choices = MapEditorCore::particleChoices(key);
		QWidget *editor = nullptr;
		const auto fieldChanged = [this, key](const QString &value) {
			m_particleBlock.set(key, ascii(value));
			if (key.rfind("Vol", 0) == 0 && m_particlePreviewAt)
				emit overlayChanged();
		};
		if (!choices.empty())
		{
			auto *combo = new QComboBox(page);
			for (const std::string &c : choices)
				combo->addItem(latin(c));
			if (combo->findText(latin(f.value)) < 0)
				combo->addItem(latin(f.value));
			combo->setCurrentText(latin(f.value));
			connect(combo, &QComboBox::currentTextChanged, this, fieldChanged);
			// Another type needs another kind of ParticleName (texture or object).
			if (key == "Type")
				connect(combo, &QComboBox::currentTextChanged, this, [this] { QTimer::singleShot(0, this, [this] { fillParticleFields(); }); });
			editor = combo;
		}
		else if (key == "ParticleName" && !drawable)
		{
			// A texture: its picture beside the name, and a picker with pictures of all textures.
			auto *row = new QWidget(page);
			auto *line = new QHBoxLayout(row);
			line->setContentsMargins(0, 0, 0, 0);
			auto *picture = new QLabel(row);
			picture->setObjectName(QStringLiteral("ParticleTexturePicture"));
			picture->setFixedSize(40, 40);
			auto *edit = new QLineEdit(latin(f.value), row);
			edit->setObjectName(QStringLiteral("ParticleField_") + latin(key));
			auto *choose = new QPushButton(T("Choose..."), row);
			choose->setObjectName(QStringLiteral("ParticleTextureChoose"));
			const auto showPicture = [picture](const QString &name) {
				const QImage image = EngineClient::instance().textureThumbnail(name, 40);
				picture->setPixmap(image.isNull() ? QPixmap() : QPixmap::fromImage(image));
				picture->setToolTip(image.isNull() ? T("The texture cannot be read: %1").arg(name) : name);
			};
			showPicture(edit->text());
			connect(edit, &QLineEdit::textEdited, this, [fieldChanged, showPicture](const QString &value) {
				fieldChanged(value);
				showPicture(value);
			});
			connect(choose, &QPushButton::clicked, this, [this, edit, fieldChanged, showPicture] {
				const QString chosen = pickTexture(edit->text());
				if (chosen.isEmpty())
					return;
				edit->setText(chosen);
				fieldChanged(chosen);
				showPicture(chosen);
			});
			line->addWidget(picture);
			line->addWidget(edit, 1);
			line->addWidget(choose);
			form->addRow(latin(key), row);
			continue;
		}
		else
		{
			auto *edit = new QLineEdit(latin(f.value), page);
			connect(edit, &QLineEdit::textEdited, this, fieldChanged);
			if (key == "ParticleName" || key == "SlaveSystem" || key == "PerParticleAttachedSystem")
			{
				// An object for drawable particles, a particle system for the others.
				QStringList names;
				if (key == "ParticleName")
				{
					for (const MeTemplateInfo &info : EngineClient::instance().catalog())
						names << QString::fromUtf8(info.name);
				}
				else
				{
					names = EngineClient::instance().particleNames();
				}
				auto *completer = new QCompleter(names, edit);
				completer->setCaseSensitivity(Qt::CaseInsensitive);
				completer->setFilterMode(Qt::MatchContains);
				edit->setCompleter(completer);
			}
			editor = edit;
		}
		editor->setObjectName(QStringLiteral("ParticleField_") + latin(key));
		form->addRow(latin(key), editor);
	}
	col->addLayout(form);
	col->addStretch(1);
	scroll->setWidget(page);
}

QString ParticlesWindow::pickTexture(const QString &current)
{
	QDialog dialog(true ? static_cast<QWidget *>(this) : this);
	dialog.setObjectName(QStringLiteral("TexturePicker"));
	dialog.setWindowTitle(T("Choose a texture"));
	dialog.resize(760, 560);
	auto *col = new QVBoxLayout(&dialog);
	auto *search = new QLineEdit(&dialog);
	search->setPlaceholderText(T("Search"));
	col->addWidget(search);
	auto *list = new QListWidget(&dialog);
	list->setObjectName(QStringLiteral("TextureList"));
	list->setViewMode(QListView::IconMode);
	list->setIconSize(QSize(64, 64));
	list->setGridSize(QSize(110, 96));
	list->setResizeMode(QListView::Adjust);
	list->setUniformItemSizes(true);
	col->addWidget(list, 1);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
	col->addWidget(buttons);
	for (const QString &name : EngineClient::instance().textureNames())
	{
		auto *item = new QListWidgetItem(name, list);
		item->setToolTip(name);
		if (name.compare(current, Qt::CaseInsensitive) == 0)
			list->setCurrentItem(item);
	}
	connect(search, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
		for (int i = 0; i < list->count(); ++i)
			list->item(i)->setHidden(!list->item(i)->text().contains(text.trimmed(), Qt::CaseInsensitive));
	});
	// The pictures come a few at a time, the visible ones first, so a large texture folder opens at once.
	auto *loader = new QTimer(&dialog);
	connect(loader, &QTimer::timeout, &dialog, [list, loader] {
		int loaded = 0;
		const QRect view = list->viewport()->rect();
		for (int pass = 0; pass < 2 && loaded < 24; ++pass)
		{
			for (int i = 0; i < list->count() && loaded < 24; ++i)
			{
				QListWidgetItem *item = list->item(i);
				if (item->isHidden() || !item->icon().isNull() || item->data(Qt::UserRole).toBool())
					continue;
				if (pass == 0 && !view.intersects(list->visualItemRect(item)))
					continue;
				const QImage image = EngineClient::instance().textureThumbnail(item->text(), 64);
				item->setData(Qt::UserRole, true);
				if (!image.isNull())
					item->setIcon(QIcon(QPixmap::fromImage(image)));
				++loaded;
			}
		}
		if (loaded == 0)
			loader->stop();
	});
	connect(search, &QLineEdit::textChanged, loader, [loader] { loader->start(0); });
	connect(list->verticalScrollBar(), &QScrollBar::valueChanged, loader, [loader] { loader->start(0); });
	loader->start(0);
	if (list->currentItem())
		list->scrollToItem(list->currentItem());
	if (dialog.exec() != QDialog::Accepted || !list->currentItem())
		return QString();
	return list->currentItem()->text();
}

void ParticlesWindow::previewSelectedParticle()
{
	if (m_particleBlockName.isEmpty() || !m_editor.hasView())
	{
		m_editor.toast(T("The preview needs the 3D view with a map."));
		return;
	}
	// At the selected object, as the game attaches effects to objects; otherwise in the middle of the view.
	const MeCamera c = EngineClient::instance().camera();
	float x = c.targetX;
	float y = c.targetY;
	if (m_editor.openMap() && m_editor.openMap()->selection.objects.size() == 1 && m_editor.openMap()->map().objects)
	{
		const std::int32_t index = *m_editor.openMap()->selection.objects.begin();
		if (index >= 0 && static_cast<std::size_t>(index) < m_editor.openMap()->map().objects->size())
		{
			x = (*m_editor.openMap()->map().objects)[static_cast<std::size_t>(index)].x;
			y = (*m_editor.openMap()->map().objects)[static_cast<std::size_t>(index)].y;
		}
	}
	if (!EngineClient::instance().previewParticle(m_particleBlockName, x, y, -2.0e7f))
	{
		m_editor.toast(T("The particle system cannot be shown."));
		return;
	}
	m_particlePreviewAt = std::array<float, 3>{x, y, m_editor.openMap() ? MapEditorCore::groundHeight(m_editor.openMap()->map(), x, y) : 0.0f};
	emit overlayChanged();
	// Particles move by time: the view draws continuously while the preview runs.
	if (!m_particleTimer)
	{
		m_particleTimer = new QTimer(this);
		connect(m_particleTimer, &QTimer::timeout, this, [this] {
			emit viewChanged();
		});
	}
	m_particleTimer->start(33);
}

void ParticlesWindow::stopParticlePreview()
{
	if (m_particleTimer)
		m_particleTimer->stop();
	EngineClient::instance().stopParticles();
	if (m_particlePreviewAt)
	{
		m_particlePreviewAt.reset();
		emit overlayChanged();
	}
	emit viewChanged();
}

void ParticlesWindow::resetParticles(bool all, bool ask)
{
	EngineClient &engine = EngineClient::instance();
	if (!all && m_particleBlockName.isEmpty())
		return;
	if (all && ask && QMessageBox::question(true ? static_cast<QWidget *>(this) : this, T("Reset all..."),
			T("Put every particle system back as the game data have it? Systems made in the editor are dropped.")) != QMessageBox::Yes)
		return;
	const bool wasNew = !all && engine.particleChange(m_particleBlockName) == 2;
	QStringList names;
	if (all)
	{
		for (const QString &name : engine.particleNames())
			if (engine.particleChange(name) != 0)
				names << name;
	}
	else
	{
		names << m_particleBlockName;
	}
	int count = 0;
	particleStep(names, [&engine, &count, all, this] {
		count = engine.resetParticles(all ? QString() : m_particleBlockName);
		return count > 0;
	});
	m_editor.toast(count == 0 ? T("Nothing to put back.") : T("Systems put back as in the game data: %1").arg(count));
	if (wasNew || (all && !engine.particleNames().contains(m_particleBlockName)))
	{
		m_particleBlockName.clear();
		refreshParticleList();
		return;
	}
	if (!m_particleBlockName.isEmpty())
		selectParticle(m_particleBlockName);
}

void ParticlesWindow::appendParticleOutline(std::vector<float> &points, std::vector<MeOverlayShape> &shapes) const
{
	if (!m_particlePreviewAt || !m_particleOutline)
		return;
	const std::array<float, 3> &at = *m_particlePreviewAt;
	for (const MapEditorCore::ParticleOutline &o : MapEditorCore::emissionOutline(m_particleBlock, at[0], at[1], at[2]))
	{
		MeOverlayShape shape{};
		shape.kind = o.closed ? ME_OVERLAY_LOOP : ME_OVERLAY_PATH;
		shape.color = 0xFFFFB020u;
		shape.firstPoint = static_cast<int32_t>(points.size() / 3);
		shape.pointCount = static_cast<int32_t>(o.points.size() / 3);
		shape.size = 2.0f;
		points.insert(points.end(), o.points.begin(), o.points.end());
		shapes.push_back(shape);
	}
}

} // namespace MapEditorApp
