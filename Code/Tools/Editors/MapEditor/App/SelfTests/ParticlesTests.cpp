/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ParticlesTests.cpp -- the self test of MainWindowParticles.cpp (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/Windows/ParticlesWindow.h"
#include "App/SelfTest.h"
#include "App/EngineClient.h"
#include "App/Common.h"
#include "App/Language.h"
#include "App/ParticleCurves.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/AreaCommands.h"
#include "Core/ParticleIni.h"
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QThread>
#include <QTreeWidget>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>

namespace MapEditorApp
{

bool MainWindow::selfTestParticles(const QString &shotDir, QStringList &report)
{
	TestSteps step(report, "particles");
	EngineClient &engine = EngineClient::instance();
	if (!engine.isStarted())
		return true;
	report << QStringLiteral("info particles: %1 systems in the game data").arg(engine.particleNames().size());

	// The test works on any game data: a texture and a system of the data, not names of one data set.
	// The texture must be bright and opaque enough for the additive preview to show in the picture.
	QString texture;
	for (const QString &name : engine.textureNames())
	{
		const QImage image = engine.textureThumbnail(name, 64);
		if (image.isNull())
			continue;
		double light = 0.0;
		for (int y = 0; y < image.height(); ++y)
			for (int x = 0; x < image.width(); ++x)
			{
				const QColor c = image.pixelColor(x, y);
				light += (c.red() + c.green() + c.blue()) / 3.0 * c.alphaF();
			}
		if (light / (image.width() * image.height()) > 60.0)
		{
			texture = name;
			break;
		}
	}
	report << QStringLiteral("info particles: texture %1").arg(texture.isEmpty() ? QStringLiteral("(none readable)") : texture);

	// A new system from INI text, as the text tab applies it.
	const QString ini = QStringLiteral("ParticleSystem SelfTestSparks\n  Priority = CRITICAL\n  IsOneShot = No\n  Shader = ADDITIVE\n"
		"  Type = PARTICLE\n  ParticleName = %1\n  Lifetime = 20.00 30.00\n  Size = 4.00 6.00\n"
		"  Color1 = R:255 G:128 B:0 0\n  Color2 = R:255 G:0 B:0 20\n  BurstDelay = 1.00 1.00\n  BurstCount = 5.00 5.00\n"
		"  VelocityType = SPHERICAL\n  VelSpherical = 1.00 2.00\n  VolumeType = POINT\nEnd\n").arg(texture.isEmpty() ? QStringLiteral("noise0000.tga") : texture);
	QString error;
	const bool applied = engine.applyParticleIni(ini, &error);
	step(applied && engine.particleNames().contains(QStringLiteral("SelfTestSparks")), QStringLiteral("a new system from INI text"));
	showParticles();
	m_particlesWindow->selectParticle(QStringLiteral("SelfTestSparks"));
	const std::string *shader = m_particlesWindow->block().find("Shader");
	auto *strip = m_particlesWindow ? m_particlesWindow->findChild<QWidget *>(QStringLiteral("ParticleColors")) : nullptr;
	step(shader && *shader == "ADDITIVE" && strip, QStringLiteral("the editor shows its %1 fields and the colour gradient").arg(m_particlesWindow->block().fields.size()));

	// Colour and alpha as curves on one time axis instead of sixteen text fields.
	auto *gradient = dynamic_cast<ColorGradient *>(strip);
	auto *curve = m_particlesWindow ? child<AlphaCurve>(m_particlesWindow, "ParticleAlpha") : nullptr;
	const bool noKeyFields = m_particlesWindow && !m_particlesWindow->findChild<QWidget *>(QStringLiteral("ParticleField_Color1"))
		&& !m_particlesWindow->findChild<QWidget *>(QStringLiteral("ParticleField_Alpha1"));
	if (curve && curve->onChanged)
	{
		// The game's writer puts out all eight keys, so the curve starts with the one start key.
		step(curve->keys.size() == 1, QStringLiteral("the alpha curve starts with the start key (%1 keys)").arg(curve->keys.size()));
		curve->keys = {MapEditorCore::AlphaKey{0.2f, 0.4f, 0}, MapEditorCore::AlphaKey{1.0f, 1.0f, 15}};
		curve->onChanged();
	}
	const std::string *alpha2 = m_particlesWindow->block().find("Alpha2");
	step(gradient && gradient->keys.size() == 2 && curve && noKeyFields && MapEditorCore::usedAlphaKeys(m_particlesWindow->block()).size() == 2 && alpha2
			&& *alpha2 == "1.00 1.00 15",
		QStringLiteral("colour gradient and alpha curve replace the key fields; keys set in the curve go into the block"));
	if (auto *fields = m_particlesWindow ? m_particlesWindow->findChild<QScrollArea *>(QStringLiteral("ParticleFields")) : nullptr)
	{
		m_particlesWindow->fillParticleFields();
		fields->widget()->resize(760, 900);
		fields->widget()->grab().save(QDir(shotDir).filePath(QStringLiteral("particles_fields.png")));
	}

	// Textures of the game data by name, with pictures for the picker and beside the field.
	const QImage thumb = engine.textureThumbnail(texture, 64);
	auto *picture = m_particlesWindow ? m_particlesWindow->findChild<QLabel *>(QStringLiteral("ParticleTexturePicture")) : nullptr;
	step(!texture.isEmpty() && !thumb.isNull()
			&& picture && !picture->pixmap().isNull() && engine.textureThumbnail(QStringLiteral("NoSuchTexture.tga"), 64).isNull(),
		QStringLiteral("%1 textures listed, %2 with its picture beside the field").arg(engine.textureNames().size()).arg(texture));

	// A field changed in the editor goes into the game's template.
	m_particlesWindow->block().set("Shader", "ALPHA");
	const bool changed = engine.applyParticleIni(QString::fromLatin1(MapEditorCore::writeParticleBlock(m_particlesWindow->block()).c_str()), &error);
	const std::optional<MapEditorCore::ParticleBlock> back = MapEditorCore::parseParticleBlock(ascii(engine.particleIni(QStringLiteral("SelfTestSparks"))));
	step(changed && back && back->find("Shader") && *back->find("Shader") == "ALPHA", QStringLiteral("a changed field reaches the game's template"));

	// A broken block is refused with a reason.
	error.clear();
	const bool refused = !engine.applyParticleIni(QStringLiteral("ParticleSystem Broken\n  Shader = NOT_A_SHADER\nEnd\n"), &error);
	step(refused && !error.isEmpty(), QStringLiteral("a broken block is refused: %1").arg(error));
	step(!engine.particleNames().contains(QStringLiteral("Broken")), QStringLiteral("a refused new system is not listed"));

	// A system of the game data: changed, marked in the list, carried into the game's test copy, put back.
	// The first system of the data whose shader is not ADDITIVE; the test switches it to ADDITIVE.
	QString smoke;
	QString originalShader;
	std::optional<MapEditorCore::ParticleBlock> smokeBlock;
	for (const QString &name : engine.particleNames())
	{
		if (name == QStringLiteral("SelfTestSparks") || engine.particleChange(name) != 0)
			continue;
		std::optional<MapEditorCore::ParticleBlock> block = MapEditorCore::parseParticleBlock(ascii(engine.particleIni(name)));
		const std::string *shaderValue = block ? block->find("Shader") : nullptr;
		if (shaderValue && *shaderValue != "ADDITIVE")
		{
			smoke = name;
			originalShader = latin(*shaderValue);
			smokeBlock = std::move(block);
			break;
		}
	}
	step(smokeBlock && engine.particleChange(smoke) == 0 && engine.particleChange(QStringLiteral("SelfTestSparks")) == 2,
		QStringLiteral("a system of the game data (%1, shader %2) counts as unchanged, one made in the editor as new")
			.arg(smoke.isEmpty() ? QStringLiteral("none without ADDITIVE") : smoke, originalShader));
	if (smokeBlock)
	{
		smokeBlock->set("Shader", "ADDITIVE");
		const QString smokeIni = latin(MapEditorCore::writeParticleBlock(*smokeBlock));
		m_particlesWindow->particleStep({smoke}, [&smokeIni, &error] { return EngineClient::instance().applyParticleIni(smokeIni, &error); });
		m_particlesWindow->selectParticle(smoke);
		auto *changes = m_particlesWindow ? child<QTreeWidget>(m_particlesWindow, "ParticleChanges") : nullptr;
		step(changes && changes->topLevelItemCount() == 1 && changes->topLevelItem(0)->text(0) == QStringLiteral("Shader")
				&& changes->topLevelItem(0)->text(1) == originalShader && changes->topLevelItem(0)->text(2) == QStringLiteral("ADDITIVE"),
			QStringLiteral("the Changes tab lists the field against the game data"));
		m_particlesWindow->undoParticles();
		const bool undone = engine.particleChange(smoke) == 0;
		m_particlesWindow->redoParticles();
		step(undone && engine.particleChange(smoke) == 1 && m_particlesWindow->block().find("Shader") && *m_particlesWindow->block().find("Shader") == "ADDITIVE",
			QStringLiteral("undo and redo in the particle window"));
		m_particlesWindow->refreshParticleList();
		auto *list = m_particlesWindow ? m_particlesWindow->findChild<QListWidget *>(QStringLiteral("ParticleList")) : nullptr;
		const QList<QListWidgetItem *> items = list ? list->findItems(smoke, Qt::MatchExactly) : QList<QListWidgetItem *>();
		step(engine.particleChange(smoke) == 1 && !items.isEmpty() && items.front()->font().bold(), QStringLiteral("a changed system stands out in the list"));
		auto *onlyChanged = m_particlesWindow ? child<QCheckBox>(m_particlesWindow, "ParticleOnlyChanged") : nullptr;
		if (onlyChanged)
			onlyChanged->setChecked(true);
		step(list && list->count() == 2, QStringLiteral("only changed systems: %1 listed").arg(list ? list->count() : 0));
		if (onlyChanged)
			onlyChanged->setChecked(false);

		// Test in game: the copy's map.ini carries the changed systems, the user's map stays as it is.
		if (m_state.map)
		{
			QString copyError;
			const QString copy = writeTestCopy(QDir(shotDir).filePath(QStringLiteral("MapEditorTest")), &copyError);
			QFile mapIni(QDir(shotDir).filePath(QStringLiteral("MapEditorTest/map.ini")));
			const QByteArray iniText = mapIni.open(QIODevice::ReadOnly) ? mapIni.readAll() : QByteArray();
			step(!copy.isEmpty() && QFile::exists(copy) && iniText.contains(("ParticleSystem " + smoke).toLatin1()) && iniText.contains("ParticleSystem SelfTestSparks")
					&& !iniText.contains("ParticleSystem Broken") && iniText.contains("\r\n"),
				QStringLiteral("test in game: the test copy's map.ini carries the changed systems %1").arg(copyError));
		}

		m_particlesWindow->blockName() = smoke;
		m_particlesWindow->resetParticles(false, false);
		const std::optional<MapEditorCore::ParticleBlock> reset = MapEditorCore::parseParticleBlock(ascii(engine.particleIni(smoke)));
		step(engine.particleChange(smoke) == 0 && reset && reset->find("Shader") && latin(*reset->find("Shader")) == originalShader,
			QStringLiteral("reset puts the values of the game data back"));
		m_particlesWindow->selectParticle(QStringLiteral("SelfTestSparks"));
	}

	const QString saved = QDir(shotDir).filePath(QStringLiteral("particles.ini"));
	step(engine.saveAllParticles(saved) && QFileInfo(saved).size() > 100, QStringLiteral("save all systems to %1").arg(saved));

	if (m_viewport && m_viewport->hasDevice())
	{
		// Additive and large, so the particles show whatever the ground below them.
		m_particlesWindow->block().set("Shader", "ADDITIVE");
		m_particlesWindow->block().set("Size", "30.00 40.00");
		m_particlesWindow->block().set("BurstCount", "20.00 20.00");
		m_particlesWindow->block().set("VolumeType", "SPHERE");
		m_particlesWindow->block().set("VolSphereRadius", "10.00");
		engine.applyParticleIni(QString::fromLatin1(MapEditorCore::writeParticleBlock(m_particlesWindow->block()).c_str()), &error);
		const auto run = [this, &engine](const QString &file) {
			m_particlesWindow->previewSelectedParticle();
			for (int i = 0; i < 20; ++i)
			{
				engine.render();
				QThread::msleep(20);
			}
			m_viewport->captureFrame(file);
		};
		// The particles are counted without the outline; it has a picture of its own.
		m_particlesWindow->outlineShown() = false;
		// At the selected object rather than in the middle of the view.
		if (m_state.map && m_state.map->map().objects && !m_state.map->map().objects->empty())
		{
			m_state.map->selection.set(0);
			selectionChanged();
			m_particlesWindow->previewSelectedParticle();
			const MapModel::MapObject &first = m_state.map->map().objects->front();
			step(m_particlesWindow->previewAt() && (*m_particlesWindow->previewAt())[0] == first.x && (*m_particlesWindow->previewAt())[1] == first.y,
				QStringLiteral("the preview runs at the selected object"));
			m_particlesWindow->stopParticlePreview();
			m_state.map->selection.clear();
			selectionChanged();
		}
		const QString shot = QDir(shotDir).filePath(QStringLiteral("particles_preview.bmp"));
		run(shot);
		const bool running = m_particlesWindow->previewRunning();
		m_particlesWindow->outlineShown() = true;
		std::vector<float> outlinePoints;
		std::vector<MeOverlayShape> outlineShapes;
		m_particlesWindow->appendParticleOutline(outlinePoints, outlineShapes);
		applyOverlay();
		const QString outlineShot = QDir(shotDir).filePath(QStringLiteral("particles_outline.bmp"));
		m_viewport->captureFrame(outlineShot);
		step(outlineShapes.size() == 3 && outlineShapes[0].kind == ME_OVERLAY_LOOP,
			QStringLiteral("the emission volume (a sphere) is drawn as three circles, picture %1").arg(outlineShot));
		m_particlesWindow->outlineShown() = false;
		// The same run with at most one particle.
		engine.setParticleLimit(1);
		const QString limited = QDir(shotDir).filePath(QStringLiteral("particles_limit.bmp"));
		run(limited);
		engine.setParticleLimit(0);
		m_particlesWindow->stopParticlePreview();
		m_particlesWindow->outlineShown() = true;
		const QString without = QDir(shotDir).filePath(QStringLiteral("particles_stopped.bmp"));
		m_viewport->captureFrame(without);
		// Against the same view without the system: the particles are the pixels that differ.
		const QImage b = QImage(without).convertToFormat(QImage::Format_RGB32);
		const auto differing = [&b](const QString &file) {
			const QImage a = QImage(file).convertToFormat(QImage::Format_RGB32);
			int count = 0;
			if (a.isNull() || a.size() != b.size())
				return count;
			for (int y = 0; y < a.height(); ++y)
			{
				const QRgb *ra = reinterpret_cast<const QRgb *>(a.constScanLine(y));
				const QRgb *rb = reinterpret_cast<const QRgb *>(b.constScanLine(y));
				for (int x = 0; x < a.width(); ++x)
				{
					if (std::abs(qRed(ra[x]) - qRed(rb[x])) + std::abs(qGreen(ra[x]) - qGreen(rb[x])) + std::abs(qBlue(ra[x]) - qBlue(rb[x])) > 30)
						++count;
				}
			}
			return count;
		};
		const int full = differing(shot);
		const int few = differing(limited);
		step(running && full > 100, QStringLiteral("preview in the 3D view: %1 pixels of particles, picture %2").arg(full).arg(shot));
		step(few > 0 && few * 2 < full, QStringLiteral("a particle limit of 1 leaves %1 pixels").arg(few));
	}

	m_particlesWindow->resetParticles(true, false);
	step(!engine.particleNames().contains(QStringLiteral("SelfTestSparks")) && engine.changedParticlesIni().isEmpty(),
		QStringLiteral("reset all drops the systems made in the editor"));
	if (m_particlesWindow)
		m_particlesWindow->close();
	return step.ok();
}

} // namespace MapEditorApp
