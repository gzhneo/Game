/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EngineParticles.cpp -- particle system templates for the editor: list, INI text, loading edited
// blocks, saving, and a preview in the 3D view.
//
// The particle code of the game asks the game client for its frame number, the terrain logic for
// the ground height (IsEmitAboveGroundOnly) and the LOD manager for the priorities it may create.
// The editor has none of these subsystems; small stand-ins are set only while the particle code
// runs, so the rest of the engine sees them absent as before. The stand-ins are built once and never
// destroyed: the destructors of GameClient and TerrainLogic tear down game subsystems (fonts,
// polygon triggers) that the editor owns differently or not at all.

#include "Engine/EngineApi.h"
#include "Engine/EngineInternal.h"

#include "Common/GameCommon.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "Common/GameLOD.h"
#include "Common/LocalFileSystem.h"
#include "Common/file.h"
#include "GameClient/GameClient.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/TerrainLogic.h"
#include "W3DDevice/GameClient/W3DDisplay.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <cstdio>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The game's writer of one template (ScriptEngine.cpp), as its particle editor saves it.
extern void _writeSingleParticleSystem(File *out, ParticleSystemTemplate *particleTemplate);

namespace
{

// The editor keeps particle templates whole: reading a block starts from the defaults, and a failed
// read puts the old values back. The template's data is a protected base.
struct TemplateAccess : public ParticleSystemTemplate
{
	static ParticleSystemInfo &info(ParticleSystemTemplate *t) { return *t; }
	// The slave template is looked up once and kept; after loading, the name may point elsewhere.
	static void forgetSlave(ParticleSystemTemplate *t) { t->*(&TemplateAccess::m_slaveTemplate) = nullptr; }
};

// Answers the frame number; the particle code needs nothing else from the client.
class EditorGameClient final : public GameClient
{
public:
	virtual void createRayEffectByTemplate(const Coord3D *, const Coord3D *, const ThingTemplate *) override {}
	virtual void addScorch(const Coord3D *, Real, Scorches) override {}
	virtual Drawable *friend_createDrawable(const ThingTemplate *, DrawableStatus) override { return nullptr; }
	virtual void setTeamColor(Int, Int, Int) override {}
	virtual void adjustLOD(Int) override {}
	virtual void notifyTerrainObjectMoved(Object *) override {}

protected:
	virtual Display *createGameDisplay(void) override { return nullptr; }
	virtual InGameUI *createInGameUI(void) override { return nullptr; }
	virtual GameWindowManager *createWindowManager(void) override { return nullptr; }
	virtual FontLibrary *createFontLibrary(void) override { return nullptr; }
	virtual DisplayStringManager *createDisplayStringManager(void) override { return nullptr; }
	virtual VideoPlayerInterface *createVideoPlayer(void) override { return nullptr; }
	virtual TerrainVisual *createTerrainVisual(void) override { return nullptr; }
	virtual Keyboard *createKeyboard(void) override { return nullptr; }
	virtual Mouse *createMouse(void) override { return nullptr; }
	virtual SnowManager *createSnowManager(void) override { return nullptr; }
	virtual void setFrameRate(Real) override {}
};

// Ground height from the view's terrain, 0 without a map.
class EditorTerrainLogic final : public TerrainLogic
{
public:
	virtual Real getGroundHeight(Real x, Real y, Coord3D *normal = nullptr) const override
	{
		if (normal)
		{
			normal->x = 0.0f;
			normal->y = 0.0f;
			normal->z = 1.0f;
		}
		float z = 0.0f;
		MapEditorEngine::terrainHeightAt(x, y, &z);
		return z;
	}
};

// Every priority is created and no particle is skipped, as at the highest detail level.
class EditorLODManager final : public GameLODManager
{
public:
	EditorLODManager()
	{
		m_minDynamicParticlePriority = PARTICLE_PRIORITY_LOWEST;
		m_minDynamicParticleSkipPriority = PARTICLE_PRIORITY_LOWEST;
	}
};

// Particle limit while no detail level has set one (the game sets it from GameLOD.ini).
constexpr Int PREVIEW_MAX_PARTICLES = 5000;
// At most this many logic frames per rendered frame; a view that was not drawn for a while does not
// fast-forward.
constexpr int MAX_STEPS_PER_RENDER = 4;
// Logic frames between a system dying out and the preview starting it again.
constexpr int RESTART_PAUSE_FRAMES = LOGICFRAMES_PER_SECOND / 2;

// Particle limit of the preview; 0 = the game's setting.
Int s_limit = 0;

template <class T>
T *standIn()
{
	alignas(T) static unsigned char storage[sizeof(T)];
	static T *object = nullptr;
	if (!object)
		object = ::new (static_cast<void *>(storage)) T();
	return object;
}

EditorGameClient *editorClient()
{
	static EditorGameClient *client = nullptr;
	if (!client)
		client = standIn<EditorGameClient>();	// never initialised: it creates no subsystems
	return client;
}

// Sets the stand-ins while the particle code runs; subsystems that exist stay.
class ParticleGlobals
{
public:
	ParticleGlobals()
		: m_client(TheGameClient), m_terrain(TheTerrainLogic), m_lod(TheGameLODManager), m_maxParticles(TheGlobalData->m_maxParticleCount)
	{
		if (!TheGameClient)
			::Engine::context().gameClient = editorClient();
		if (!TheTerrainLogic)
			::Engine::context().terrainLogic = standIn<EditorTerrainLogic>();
		if (!TheGameLODManager)
			::Engine::context().gameLODManager = standIn<EditorLODManager>();
		if (s_limit > 0)
			TheWritableGlobalData->m_maxParticleCount = s_limit;
		else if (TheGlobalData->m_maxParticleCount <= 0)
			TheWritableGlobalData->m_maxParticleCount = PREVIEW_MAX_PARTICLES;
	}
	~ParticleGlobals()
	{
		::Engine::context().gameClient = m_client;
		::Engine::context().terrainLogic = m_terrain;
		::Engine::context().gameLODManager = m_lod;
		TheWritableGlobalData->m_maxParticleCount = m_maxParticles;
	}
	ParticleGlobals(const ParticleGlobals &) = delete;
	ParticleGlobals &operator=(const ParticleGlobals &) = delete;

private:
	GameClient *m_client;
	TerrainLogic *m_terrain;
	GameLODManager *m_lod;
	Int m_maxParticles;
};

struct Preview
{
	bool active = false;
	std::string name;
	Coord3D position = {0.0f, 0.0f, 0.0f};
	int pause = 0;
	std::chrono::steady_clock::time_point last;
	double pendingSeconds = 0.0;
};

Preview s_preview;
std::vector<std::string> s_names;
// New names whose text was refused. The manager keeps every template it has made, so these stay in
// its map; the editor lists, shows and saves them no more.
std::set<std::string> s_refused;
// Values of the game data, kept when meParticleApply first changes a template; names it made.
std::map<ParticleSystemTemplate *, ParticleSystemInfo> s_original;
std::set<std::string> s_added;

bool nameLess(const std::string &a, const std::string &b)
{
	const int c = _stricmp(a.c_str(), b.c_str());
	return c != 0 ? c < 0 : a < b;
}

std::vector<ParticleSystemTemplate *> sortedTemplates()
{
	std::vector<ParticleSystemTemplate *> list;
	if (!TheParticleSystemManager)
		return list;
	for (auto it = TheParticleSystemManager->beginParticleSystemTemplate(); it != TheParticleSystemManager->endParticleSystemTemplate(); ++it)
	{
		if (it->second && !it->second->getName().isEmpty() && !s_refused.count(it->second->getName().str()))
			list.push_back(it->second);
	}
	std::sort(list.begin(), list.end(), [](ParticleSystemTemplate *a, ParticleSystemTemplate *b) {
		return nameLess(a->getName().str(), b->getName().str());
	});
	return list;
}

const std::vector<std::string> &names()
{
	if (s_names.empty())
	{
		for (ParticleSystemTemplate *t : sortedTemplates())
			s_names.emplace_back(t->getName().str());
	}
	return s_names;
}

// A file in the temp folder; the engine's writer and the INI reader work on files only.
std::string tempFile()
{
	char dir[MAX_PATH + 1] = {};
	const DWORD n = GetTempPathA(MAX_PATH + 1, dir);
	if (n == 0 || n > MAX_PATH)
		return std::string();
	static unsigned counter = 0;
	char name[80];
	std::snprintf(name, sizeof(name), "MapEditorParticles_%lu_%u.ini", static_cast<unsigned long>(GetCurrentProcessId()), ++counter);
	return std::string(dir) + name;
}

bool readAndDelete(const std::string &path, std::string &text)
{
	text.clear();
	FILE *f = std::fopen(path.c_str(), "rb");
	if (f)
	{
		char buffer[4096];
		size_t n;
		while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
			text.append(buffer, n);
		std::fclose(f);
	}
	DeleteFileA(path.c_str());
	return f != nullptr;
}

// The blocks of the templates as the game writes them, lines ending in \n.
bool writeTemplates(const std::vector<ParticleSystemTemplate *> &templates, std::string &text)
{
	const std::string path = tempFile();
	if (path.empty() || !TheLocalFileSystem)
		return false;
	File *out = TheLocalFileSystem->openFile(path.c_str(), File::WRITE | File::CREATE | File::TRUNCATE | File::BINARY);
	if (!out)
		return false;
	for (ParticleSystemTemplate *t : templates)
		_writeSingleParticleSystem(out, t);
	out->close();
	return readAndDelete(path, text);
}

// Loads INI text with the game's reader, over existing templates. False with the reason.
bool loadIni(const std::string &text, std::string &reason)
{
	const std::string path = tempFile();
	FILE *f = path.empty() ? nullptr : std::fopen(path.c_str(), "wb");
	if (!f)
	{
		reason = "cannot write a temporary file";
		return false;
	}
	const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
	std::fclose(f);
	bool ok = written;
	if (written)
	{
		try
		{
			INI ini;
			ini.load(AsciiString(path.c_str()), INILoadType::INI_LOAD_OVERWRITE, nullptr);
		}
		catch (...)
		{
			// The reader wraps every error of a block into an INIException naming only the block.
			ok = false;
			reason = "unknown field or invalid value";
		}
	}
	else
	{
		reason = "cannot write a temporary file";
	}
	DeleteFileA(path.c_str());
	return ok;
}

struct Block
{
	std::string name;
	int firstLine = 0;						// 1-based, in the caller's text
	std::vector<std::string> lines;			// header, fields and End
	std::vector<int> lineNumbers;
};

// First word of a line, INI separators and comments considered; empty for blank or comment lines.
std::string firstWord(const std::string &line, std::string *second = nullptr)
{
	static const char SEPARATORS[] = " \t=";
	std::string text = line;
	const size_t comment = text.find(';');
	if (comment != std::string::npos)
		text.erase(comment);
	const size_t start = text.find_first_not_of(SEPARATORS);
	if (start == std::string::npos || text.compare(start, 2, "//") == 0)
		return std::string();
	const size_t end = text.find_first_of(SEPARATORS, start);
	std::string word = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
	if (second)
	{
		second->clear();
		if (end != std::string::npos)
		{
			const size_t s = text.find_first_not_of(SEPARATORS, end);
			if (s != std::string::npos)
				*second = text.substr(s, text.find_first_of(SEPARATORS, s) - s);
		}
	}
	return word;
}

std::string lineError(int line, const std::string &text)
{
	return "line " + std::to_string(line) + ": " + text;
}

bool splitBlocks(const char *iniText, std::vector<Block> &blocks, std::string &error)
{
	std::vector<std::string> lines;
	for (const char *p = iniText;;)
	{
		const char *end = std::strchr(p, '\n');
		std::string line(p, end ? static_cast<size_t>(end - p) : std::strlen(p));
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
		if (!end)
			break;
		p = end + 1;
	}
	Block *open = nullptr;
	for (size_t i = 0; i < lines.size(); ++i)
	{
		const int number = static_cast<int>(i) + 1;
		std::string name;
		const std::string word = firstWord(lines[i], &name);
		if (!open)
		{
			if (word.empty())
				continue;
			if (word != "ParticleSystem")
			{
				error = lineError(number, "expected \"ParticleSystem <Name>\"");
				return false;
			}
			if (name.empty())
			{
				error = lineError(number, "ParticleSystem without a name");
				return false;
			}
			blocks.emplace_back();
			open = &blocks.back();
			open->name = name;
			open->firstLine = number;
		}
		open->lines.push_back(lines[i]);
		open->lineNumbers.push_back(number);
		if (open->lines.size() > 1 && _stricmp(word.c_str(), "End") == 0)
			open = nullptr;
	}
	if (open)
	{
		error = lineError(open->firstLine, "ParticleSystem " + open->name + " has no End");
		return false;
	}
	if (blocks.empty())
	{
		error = "no ParticleSystem block";
		return false;
	}
	return true;
}

std::string joinLines(const std::vector<std::string> &lines, size_t count, bool addEnd)
{
	std::string text;
	for (size_t i = 0; i < count && i < lines.size(); ++i)
		text += lines[i] + "\n";
	if (addEnd)
		text += "End\n";
	return text;
}

// Reads the block into its template, which starts from the defaults as at the game's start.
bool loadBlock(const Block &block, size_t lineCount, bool addEnd, std::string &reason)
{
	if (ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(block.name.c_str())))
		TemplateAccess::info(t) = ParticleSystemInfo();
	return loadIni(joinLines(block.lines, lineCount, addEnd), reason);
}

// Index in block.lines of the line the reader rejects: the shortest run of fields that fails, found
// by halving. 0 if the fields read fine without the block's own End.
size_t failingLine(const Block &block)
{
	const size_t fields = block.lines.size() >= 2 ? block.lines.size() - 2 : 0;
	std::string reason;
	if (fields == 0 || loadBlock(block, fields + 1, true, reason))
		return 0;
	size_t lo = 1, hi = fields;	// hi fails
	while (lo < hi)
	{
		const size_t mid = (lo + hi) / 2;
		if (loadBlock(block, mid + 1, true, reason))
			lo = mid + 1;
		else
			hi = mid;
	}
	return hi;
}

void destroyAllSystems()
{
	if (TheParticleSystemManager && TheParticleSystemManager->getParticleSystemCount() > 0)
		TheParticleSystemManager->reset();
}

// The block of the template as it would be written with the given values.
std::string blockWith(ParticleSystemTemplate *t, const ParticleSystemInfo &values)
{
	const ParticleSystemInfo current = TemplateAccess::info(t);
	TemplateAccess::info(t) = values;
	std::string text;
	writeTemplates({t}, text);
	TemplateAccess::info(t) = current;
	return text;
}

int changeOf(ParticleSystemTemplate *t)
{
	if (!t || s_refused.count(t->getName().str()))
		return 0;
	if (s_added.count(t->getName().str()))
		return 2;
	const auto original = s_original.find(t);
	if (original == s_original.end())
		return 0;
	std::string now;
	writeTemplates({t}, now);
	return now != blockWith(t, original->second) ? 1 : 0;
}

// Back to the game data; a template the editor made is dropped.
bool resetTemplate(ParticleSystemTemplate *t)
{
	const std::string name = t->getName().str();
	if (s_added.erase(name))
	{
		TemplateAccess::info(t) = ParticleSystemInfo();
		s_refused.insert(name);
		return true;
	}
	const auto original = s_original.find(t);
	if (original == s_original.end())
		return false;
	TemplateAccess::info(t) = original->second;
	TemplateAccess::forgetSlave(t);
	s_original.erase(original);
	return true;
}

bool createPreviewSystem()
{
	const ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(s_preview.name.c_str()));
	if (!t || s_refused.count(s_preview.name))
		return false;
	ParticleGlobals globals;
	ParticleSystem *sys = TheParticleSystemManager->createParticleSystem(t, TRUE);
	if (!sys)
		return false;
	sys->setPosition(&s_preview.position);
	return true;
}

// One logic frame of every system, as ParticleSystemManager::update does it; that one is tied to
// the game logic's frame number, which the editor has not.
void updateSystems()
{
	ParticleSystemManager::ParticleSystemList &systems = TheParticleSystemManager->getAllParticleSystems();
	for (auto it = systems.begin(); it != systems.end();)
	{
		ParticleSystem *sys = *it;
		++it;
		if (sys && !sys->update(0))
			deleteInstanceOf(sys);
	}
}

} // namespace

namespace MapEditorEngine
{

void stepParticles()
{
	if (!s_preview.active || !TheParticleSystemManager)
		return;
	const auto now = std::chrono::steady_clock::now();
	s_preview.pendingSeconds += std::chrono::duration<double>(now - s_preview.last).count();
	s_preview.last = now;
	const double frameSeconds = 1.0 / static_cast<double>(LOGICFRAMES_PER_SECOND);
	int steps = static_cast<int>(s_preview.pendingSeconds / frameSeconds);
	if (steps <= 0)
		return;
	if (steps > MAX_STEPS_PER_RENDER)
	{
		steps = MAX_STEPS_PER_RENDER;
		s_preview.pendingSeconds = 0.0;
	}
	else
	{
		s_preview.pendingSeconds -= steps * frameSeconds;
	}
	ParticleGlobals globals;
	for (int i = 0; i < steps; ++i)
	{
		TheGameClient->setFrame(TheGameClient->getFrame() + 1);
		updateSystems();
		if (TheParticleSystemManager->getParticleSystemCount() == 0 && ++s_preview.pause >= RESTART_PAUSE_FRAMES)
		{
			s_preview.pause = 0;
			if (!createPreviewSystem())
				s_preview.active = false;
		}
	}
}

void stopParticles()
{
	s_preview.active = false;
	destroyAllSystems();
}

ParticleRenderScope::ParticleRenderScope(W3DAssetManager *assets)
{
	if (assets && TheParticleSystemManager && TheParticleSystemManager->getParticleSystemCount() > 0 && !W3DDisplay::m_assetManager)
	{
		m_previous = W3DDisplay::m_assetManager;
		W3DDisplay::m_assetManager = assets;
		m_active = true;
	}
}

ParticleRenderScope::~ParticleRenderScope()
{
	if (m_active)
		W3DDisplay::m_assetManager = m_previous;
}

} // namespace MapEditorEngine

MEAPI int32_t meParticleCount(void)
{
	s_names.clear();
	return static_cast<int32_t>(names().size());
}

MEAPI int32_t meParticleName(int32_t index, char *name, int32_t size)
{
	const std::vector<std::string> &list = names();
	if (index < 0 || index >= static_cast<int32_t>(list.size()) || !name || size <= 0)
		return 0;
	std::snprintf(name, static_cast<size_t>(size), "%s", list[static_cast<size_t>(index)].c_str());
	return 1;
}

MEAPI int32_t meParticleIni(const char *name, char *text, int32_t size)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager || !name)
		return 0;
	ParticleSystemTemplate *t = s_refused.count(name) ? nullptr : TheParticleSystemManager->findTemplate(AsciiString(name));
	std::string ini;
	if (!t || !writeTemplates({t}, ini))
		return 0;
	if (text && size > 0)
	{
		const size_t n = std::min(ini.size(), static_cast<size_t>(size - 1));
		std::memcpy(text, ini.data(), n);
		text[n] = '\0';
	}
	return static_cast<int32_t>(ini.size());
}

MEAPI int32_t meParticleApply(const char *iniText, char *error, int32_t errorSize)
{
	using MapEditorEngine::setError;
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager)
	{
		setError(error, errorSize, "engine not started");
		return 0;
	}
	std::vector<Block> blocks;
	std::string message;
	if (!iniText || !splitBlocks(iniText, blocks, message))
	{
		setError(error, errorSize, iniText ? message : std::string("no text"));
		return 0;
	}
	// Values before the load, to put back on an error; names that are new get the defaults.
	std::vector<std::pair<ParticleSystemTemplate *, ParticleSystemInfo>> saved;
	std::vector<std::string> added;
	for (const Block &b : blocks)
	{
		ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(b.name.c_str()));
		if (t && s_refused.count(b.name))
			t = nullptr;
		if (t && std::none_of(saved.begin(), saved.end(), [t](const auto &s) { return s.first == t; }))
			saved.emplace_back(t, TemplateAccess::info(t));
		if (t && !s_added.count(b.name))
			s_original.emplace(t, TemplateAccess::info(t));
		else if (!t && std::find(added.begin(), added.end(), b.name) == added.end())
			added.push_back(b.name);
	}
	for (const Block &b : blocks)
	{
		std::string reason;
		if (loadBlock(b, b.lines.size(), false, reason))
			continue;
		const size_t bad = failingLine(b);
		if (bad > 0)
		{
			std::string text = b.lines[bad];
			const size_t start = text.find_first_not_of(" \t");
			text.erase(0, start == std::string::npos ? text.size() : start);
			message = lineError(b.lineNumbers[bad], reason + ": " + text);
		}
		else
		{
			message = lineError(b.firstLine, "ParticleSystem " + b.name + ": " + reason);
		}
		for (auto &s : saved)
			TemplateAccess::info(s.first) = s.second;
		for (const std::string &name : added)
		{
			if (ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(name.c_str())))
				TemplateAccess::info(t) = ParticleSystemInfo();
			s_refused.insert(name);
		}
		s_names.clear();
		setError(error, errorSize, message);
		return 0;
	}
	for (const Block &b : blocks)
	{
		s_refused.erase(b.name);
		if (std::find(added.begin(), added.end(), b.name) != added.end())
			s_added.insert(b.name);
	}
	for (ParticleSystemTemplate *t : sortedTemplates())
		TemplateAccess::forgetSlave(t);
	s_names.clear();
	if (s_preview.active)
	{
		destroyAllSystems();
		s_preview.pause = 0;
		s_preview.active = createPreviewSystem();
	}
	return 1;
}

MEAPI int32_t meParticleSaveAll(const wchar_t *path)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager || !path || !path[0])
		return 0;
	std::string text;
	if (!writeTemplates(sortedTemplates(), text))
		return 0;
	std::string crlf;
	crlf.reserve(text.size() + text.size() / 16);
	for (const char c : text)
	{
		if (c == '\n')
			crlf += '\r';
		crlf += c;
	}
	FILE *f = _wfopen(path, L"wb");
	if (!f)
		return 0;
	const bool ok = std::fwrite(crlf.data(), 1, crlf.size(), f) == crlf.size();
	return std::fclose(f) == 0 && ok ? 1 : 0;
}

MEAPI int32_t meParticlePreview(const char *name, float x, float y, float z)
{
	MapEditorEngine::stopParticles();
	if (!MapEditorEngine::engineStarted() || !MapEditorEngine::viewReady() || !TheParticleSystemManager || !name)
		return 0;
	// The scene draws particles only with a terrain (RTS3DScene::Customized_Renderer).
	float ground = 0.0f;
	if (!MapEditorEngine::terrainHeightAt(x, y, &ground))
		return 0;
	s_preview.name = name;
	s_preview.position.x = x;
	s_preview.position.y = y;
	s_preview.position.z = z < -1.0e7f ? ground : z;
	s_preview.pause = 0;
	s_preview.pendingSeconds = 0.0;
	s_preview.last = std::chrono::steady_clock::now();
	s_preview.active = createPreviewSystem();
	return s_preview.active ? 1 : 0;
}

MEAPI void meParticleStopAll(void)
{
	MapEditorEngine::stopParticles();
}

MEAPI int32_t meParticleChanged(const char *name)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager || !name)
		return 0;
	return changeOf(TheParticleSystemManager->findTemplate(AsciiString(name)));
}

MEAPI int32_t meParticleReset(const char *name)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager)
		return 0;
	std::vector<ParticleSystemTemplate *> targets;
	if (name && name[0])
	{
		if (ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(name)))
			targets.push_back(t);
	}
	else
	{
		targets = sortedTemplates();
	}
	int32_t count = 0;
	for (ParticleSystemTemplate *t : targets)
		count += resetTemplate(t) ? 1 : 0;
	if (count > 0)
	{
		for (ParticleSystemTemplate *t : sortedTemplates())
			TemplateAccess::forgetSlave(t);
		s_names.clear();
		if (s_preview.active)
		{
			destroyAllSystems();
			s_preview.pause = 0;
			s_preview.active = createPreviewSystem();
		}
	}
	return count;
}

MEAPI int32_t meParticleChangedIni(char *text, int32_t size)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager)
		return 0;
	std::vector<ParticleSystemTemplate *> changed;
	for (ParticleSystemTemplate *t : sortedTemplates())
	{
		if (changeOf(t) != 0)
			changed.push_back(t);
	}
	std::string ini;
	if (changed.empty() || !writeTemplates(changed, ini))
		return 0;
	if (text && size > 0)
	{
		const size_t n = std::min(ini.size(), static_cast<size_t>(size - 1));
		std::memcpy(text, ini.data(), n);
		text[n] = '\0';
	}
	return static_cast<int32_t>(ini.size());
}

MEAPI void meParticleSetLimit(int32_t count)
{
	s_limit = count > 0 ? count : 0;
}

MEAPI int32_t meParticleOriginalIni(const char *name, char *text, int32_t size)
{
	if (!MapEditorEngine::engineStarted() || !TheParticleSystemManager || !name || s_added.count(name) || s_refused.count(name))
		return 0;
	ParticleSystemTemplate *t = TheParticleSystemManager->findTemplate(AsciiString(name));
	if (!t)
		return 0;
	const auto original = s_original.find(t);
	std::string ini;
	if (original != s_original.end())
		ini = blockWith(t, original->second);
	else if (!writeTemplates({t}, ini))
		return 0;
	if (ini.empty())
		return 0;
	if (text && size > 0)
	{
		const size_t n = std::min(ini.size(), static_cast<size_t>(size - 1));
		std::memcpy(text, ini.data(), n);
		text[n] = '\0';
	}
	return static_cast<int32_t>(ini.size());
}
