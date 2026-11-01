/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SettingsSchema.cpp -- every user setting, described once (see Settings.h). A default of nullptr
// comes from game data (GameData.ini, AudioSettings, the detail levels) and is set by the engine.

#include "Engine/Settings.h"

namespace Engine
{
namespace Settings
{

namespace
{

const char *const LOD_LEVELS = "Low|Medium|High|VeryHigh|Custom";

const File SET = File::Settings;
const File STA = File::State;
const Apply NOW = Apply::Immediate;
const Apply RESTART = Apply::Restart;
const Legacy OPT = Legacy::Options;
const Legacy NET = Legacy::Network;
const Legacy SKI = Legacy::Skirmish;
const LegacyCoding PLAIN = LegacyCoding::Plain;

// A static table: no allocation, so nothing of it shows up in the engine's leak report at exit.
// The game's readers (UserSettings) keep the checks of the old Options.ini readers, so a range or a set
// of choices here only repeats them; a tighter one would change the meaning of a value on import
// (AntiAliasing 17 means off for the reader, clamped to 16 it would mean 16x).
// Columns: id, file, type, default (nullptr: from game data), minimum, maximum (equal: no range),
// choices, apply, command line switch, old file, old key, old coding, description.
const Definition TABLE[] = {
	// ---- Settings.ini
	{ "General.Language", SET, Type::Text, "", 0, 0, nullptr, RESTART, nullptr, OPT, "Language", PLAIN,
		"Language of the game texts (english, german, ...); empty: from the installed archives" },
	{ "General.CampaignDifficulty", SET, Type::Int, nullptr, 0, 2, nullptr, NOW, nullptr, OPT, "CampaignDifficulty", PLAIN,
		"Campaign difficulty: 0 easy, 1 normal, 2 hard" },
	{ "General.UseSystemMapDir", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, OPT, "UseSystemMapDir", PLAIN,
		"Map list shows the official maps (no: your own maps)" },
	{ "General.HardwareHint", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, OPT, "HardwareHint", PLAIN,
		"Hint at start when the computer is below the minimum" },
	{ "General.TestAids", SET, Type::Bool, "no", 0, 0, nullptr, RESTART, "-testhilfen", OPT, "Testhilfen", PLAIN,
		"Developer keys (test aids such as money, power, winning a mission)" },

	{ "Display.Resolution", SET, Type::Resolution, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "Resolution", PLAIN,
		"Width and height in pixels" },
	{ "Display.Gamma", SET, Type::Int, "50", -1000000, 1000000, nullptr, NOW, "-gamma", OPT, "Gamma", PLAIN,
		"Brightness 0..100, 50 is neutral" },
	{ "Display.AntiAliasing", SET, Type::Enum, "0", 0, 0, "0|1|2|4|8|16", RESTART, nullptr, OPT, "AntiAliasing", PLAIN,
		"Multisampling: 0 off, 2, 4 or 8 samples" },
	{ "Display.VSync", SET, Type::Bool, "yes", 0, 0, nullptr, RESTART, "-noVSync", OPT, "VSync", PLAIN,
		"Wait for the display refresh" },
	{ "Display.RenderFPSLimit", SET, Type::Int, "0", 0, 1000000, nullptr, RESTART, nullptr, OPT, "RenderFPSLimit", PLAIN,
		"Frame rate limit of the picture, 0: none" },
	{ "Display.RenderScale", SET, Type::Float, nullptr, 0, 0, nullptr, RESTART, "-renderScale", OPT, "RenderScale", PLAIN,
		"Share of the window resolution the 3D scene is drawn in, 0.5..2 (or 50..200; above 1 supersampling); not set: from the detail level" },
	{ "Display.RenderScaleFilter", SET, Type::Enum, nullptr, 0, 0, "bilinear|bicubic|cas", RESTART, "-renderScaleFilter", OPT,
		"RenderScaleFilter", PLAIN, "Filter that scales the 3D scene up: bilinear, bicubic or cas (sharpened)" },
	{ "Display.RenderBackend", SET, Type::Enum, "d3d11", 0, 0, "d3d11|vulkan", RESTART, "-vulkan", Legacy::None, nullptr, PLAIN,
		"Renderer: d3d11 or vulkan (Direct3D 11 starts and this goes back to d3d11 if Vulkan cannot be used)" },

	{ "Detail.StaticGameLOD", SET, Type::Enum, nullptr, 0, 0, LOD_LEVELS, NOW, nullptr, OPT, "StaticGameLOD", PLAIN,
		"Detail level; Custom uses the switches below" },
	{ "Detail.TextureQuality", SET, Type::Enum, "Auto", 0, 0, "Auto|SD|HD|UHD", RESTART, "-textureQuality", OPT,
		"TextureQuality", PLAIN, "Texture tier: Auto, SD, HD, UHD" },
	{ "Detail.D3D11Effects", SET, Type::Text, "", 0, 0, nullptr, RESTART, "-d3d11Effects", OPT, "D3D11Effects", PLAIN,
		"Direct3D 11 effects on top of the detail level, comma-separated (list in GameLODOverride.ini)" },
	{ "Effects.FXAA", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: FXAA edge smoothing; auto: as the detail level" },
	{ "Effects.Sharpen", SET, Type::Enum, "auto", 0, 0, "auto|off|0.25|0.5|1", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: sharpening strength (CAS); auto: as the detail level" },
	{ "Effects.SoftParticles", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: soft particles; auto: as the detail level" },
	{ "Effects.SSAO", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: ambient occlusion; auto: as the detail level" },
	{ "Effects.ColorGrade", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: color grading; auto: as the detail level" },
	{ "Effects.ShadowMap", SET, Type::Enum, "auto", 0, 0, "auto|off|1024|2048|4096", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: sun shadows, shadow map size; auto: as the detail level" },
	{ "Effects.ShadowTerrain", SET, Type::Enum, "auto", 0, 0, "auto|off|0.5|1", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: share of the shadows the terrain casts onto itself; auto: as the detail level" },
	{ "Effects.TerrainDetail", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: terrain detail layer, strength; auto: as the detail level" },
	{ "Effects.TerrainRock", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: rock layer on steep slopes; auto: as the detail level" },
	{ "Effects.TerrainNormal", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", Apply::NextMatch, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: terrain relief, strength; auto: as the detail level" },
	{ "Effects.TerrainTess", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", Apply::NextMatch, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: terrain tessellation, displacement; auto: as the detail level" },
	{ "Effects.RockTess", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", Apply::NextMatch, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: rock slopes in the tessellation; auto: as the detail level" },
	{ "Effects.Bloom", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: bloom, strength; auto: as the detail level" },
	{ "Effects.Heat", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: heat shimmer, strength; auto: as the detail level" },
	{ "Effects.Haze", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: distance haze; auto: as the detail level" },
	{ "Effects.Lights", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: dynamic lights, strength; auto: as the detail level" },
	{ "Effects.ParticleLight", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: particles lit by the scene; auto: as the detail level" },
	{ "Effects.ParticleGround", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: particles kept above the ground; auto: as the detail level" },
	{ "Effects.SoftShadows", SET, Type::Enum, "auto", 0, 0, "auto|off|1|2", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: soft sun shadows (penumbra by distance), light size; auto: as the detail level" },
	{ "Effects.ContactShadows", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: contact shadows from screen space rays; auto: as the detail level" },
	{ "Effects.ShadowCascades", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: sun shadows in two cascades, twice the range; auto: as the detail level" },
	{ "Effects.AlphaCoverage", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: with multisampling, smooth cut-out edges (trees, fences); auto: as the detail level" },
	{ "Effects.MsaaDepth", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: depth effects (SSAO, shadow map, soft particles, haze, lights, heat) with multisampling; auto: on whenever multisampling runs" },
	{ "Effects.Deband", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: fine noise against steps in gradients (sky, haze, water); auto: as the detail level" },
	{ "Effects.WaterSSR", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: water reflects shores and objects with a flat camera; auto: as the detail level" },
	{ "Effects.GTAO", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: ambient occlusion by horizon search at full resolution (GTAO); auto: as the detail level" },
	{ "Effects.TAA", SET, Type::Enum, "auto", 0, 0, "auto|off|on", NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Direct3D 11: temporal anti-aliasing (calm edges in motion, replaces FXAA); auto: as the detail level" },
	{ "Detail.WaterTexture", SET, Type::Enum, "free", 0, 0, "ea|free", RESTART, "-waterTexture", OPT, "WaterTexture", PLAIN,
		"Water color texture: ea (game data) or free (CC0 tiles in ProjectData)" },
	{ "Detail.FreeContent", SET, Type::Enum, "free", 0, 0, "ea|free", RESTART, "-freeContent", OPT, "FreeContent", PLAIN,
		"Terrain and road textures: ea (game data) or free (CC0 packages in ProjectData\\Inhalt)" },
	{ "Detail.WaterQuality", SET, Type::Int, nullptr, 0, 2, nullptr, RESTART, "-waterQuality", Legacy::None, nullptr, PLAIN,
		"Water: 0 as in the game data, 1 turbidity and Fresnel, 2 also refraction; not set: from the detail level" },
	{ "Detail.TerrainAnisotropy", SET, Type::Int, nullptr, 0, 16, nullptr, RESTART, "-terrainAnisotropy", Legacy::None, nullptr, PLAIN,
		"Anisotropic filtering of the terrain 0..16; not set: from the detail level" },
	{ "Detail.TextureReduction", SET, Type::Int, nullptr, -1000000, 2, nullptr, NOW, nullptr, OPT, "TextureReduction", PLAIN,
		"Custom: texture reduction 0..2" },
	{ "Detail.UseShadowVolumes", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseShadowVolumes", PLAIN, "Custom: shadow volumes" },
	{ "Detail.UseShadowDecals", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseShadowDecals", PLAIN, "Custom: shadow decals" },
	{ "Detail.UseCloudMap", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseCloudMap", PLAIN, "Custom: cloud shadows" },
	{ "Detail.UseLightMap", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseLightMap", PLAIN, "Custom: light map" },
	{ "Detail.ShowSoftWaterEdge", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "ShowSoftWaterEdge", PLAIN, "Custom: soft water edge" },
	{ "Detail.BuildingOcclusion", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "BuildingOcclusion", PLAIN, "Custom: units behind buildings" },
	{ "Detail.ShowTrees", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "ShowTrees", PLAIN, "Custom: trees" },
	{ "Detail.HeatEffects", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "HeatEffects", PLAIN, "Custom: heat haze" },
	{ "Detail.ExtraAnimations", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "ExtraAnimations", PLAIN, "Custom: extra animations" },
	{ "Detail.DynamicLOD", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "DynamicLOD", PLAIN, "Custom: lower details when the frame rate drops" },
	{ "Detail.FPSLimit", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "FPSLimit", PLAIN, "Custom: limit the frame rate" },
	{ "Detail.MaxParticleCount", SET, Type::Int, nullptr, 100, 1000000000, nullptr, NOW, nullptr, OPT, "MaxParticleCount", PLAIN,
		"Custom: particle limit" },

	{ "Audio.SFXVolume", SET, Type::Float, nullptr, 0, 1000000, nullptr, NOW, nullptr, OPT, "SFXVolume", PLAIN, "Sound effects 0..100" },
	{ "Audio.SFX3DVolume", SET, Type::Float, nullptr, 0, 1000000, nullptr, NOW, nullptr, OPT, "SFX3DVolume", PLAIN, "Sound effects in the world 0..100" },
	{ "Audio.VoiceVolume", SET, Type::Float, nullptr, 0, 1000000, nullptr, NOW, nullptr, OPT, "VoiceVolume", PLAIN, "Voices 0..100" },
	{ "Audio.MusicVolume", SET, Type::Float, nullptr, 0, 1000000, nullptr, NOW, nullptr, OPT, "MusicVolume", PLAIN, "Music 0..100" },

	{ "Controls.ScrollFactor", SET, Type::Int, nullptr, 0, 100, nullptr, NOW, nullptr, OPT, "ScrollFactor", PLAIN, "Scroll speed 0..100" },
	{ "Controls.UseAlternateMouse", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseAlternateMouse", PLAIN,
		"Left button selects and commands" },
	{ "Controls.Retaliation", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "Retaliation", PLAIN,
		"Units fire back when attacked" },
	{ "Controls.UseDoubleClickAttackMove", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "UseDoubleClickAttackMove", PLAIN,
		"Double click orders an attack move" },
	{ "Controls.DrawScrollAnchor", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "DrawScrollAnchor", PLAIN,
		"Show the anchor while scrolling with the right button" },
	{ "Controls.ScrollSpeedKeyboard", SET, Type::Int, nullptr, 0, 100, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Scroll speed with the arrow keys 0..100; not set: ScrollFactor" },
	{ "Controls.ScrollSpeedEdge", SET, Type::Int, nullptr, 0, 100, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Scroll speed at the screen edge 0..100; not set: ScrollFactor" },
	{ "Controls.ScrollSpeedDrag", SET, Type::Int, nullptr, 0, 100, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Speed when dragging with the right button 0..100; not set: ScrollFactor" },
	{ "Controls.EdgeScrollInWindow", SET, Type::Bool, "no", 0, 0, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Scroll at the window edge also in a window (fullscreen and borderless always do)" },
	{ "Controls.MoveScrollAnchor", SET, Type::Bool, nullptr, 0, 0, nullptr, NOW, nullptr, OPT, "MoveScrollAnchor", PLAIN,
		"Anchor follows the mouse while scrolling" },
	{ "Controls.HideCursorWhileDragging", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Dragging with the right button hides the cursor and keeps it in place, so the screen edge does not stop the drag" },
	{ "Controls.ConfineCursorInWindow", SET, Type::Bool, "no", 0, 0, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Keep the cursor in the game window also when windowed (fullscreen and borderless always do while active)" },
	{ "Controls.CursorScale", SET, Type::Int, "0", 0, 400, nullptr, RESTART, nullptr, Legacy::None, nullptr, PLAIN,
		"Cursor size in percent; 0: by the monitor's scaling (normal size up to 150 %, larger above)" },

	{ "Camera.MaxZoomFactor", SET, Type::Float, "1", 1, 1.25, nullptr, RESTART, "-maxZoom", Legacy::None, nullptr, PLAIN,
		"How far the player's camera zooms out, times the normal height: 1 as before, up to 1.25" },

	{ "Replays.SaveCamera", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, OPT, "SaveCameraInReplays", PLAIN,
		"Record the camera in replays" },
	{ "Replays.UseCamera", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, OPT, "UseCameraInReplays", PLAIN,
		"Follow the recorded camera when watching" },

	{ "Network.IPAddress", SET, Type::Text, "", 0, 0, nullptr, NOW, nullptr, OPT, "IPAddress", PLAIN,
		"Local address for network games; empty: every adapter" },
	{ "Network.AllowSpectators", SET, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Others may watch a running game you host" },
	{ "Network.SpectatorDelay", SET, Type::Int, "0", 0, 600, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Seconds the spectators of a game you host lag behind the players (0: none)" },
	{ "Network.ReturnWindow", SET, Type::Int, "300", 0, 3600, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Seconds a player dropped for not answering may come back while its units wait (0: destroyed at once)" },

	{ "System.WorkerThreads", SET, Type::Int, "-1", -1000000, 1000000, nullptr, RESTART, "-workers", OPT, "WorkerThreads", PLAIN,
		"Worker threads, -1: automatic" },
	{ "System.PrefetchArchives", SET, Type::Bool, "yes", 0, 0, nullptr, RESTART, nullptr, OPT, "PrefetchArchives", PLAIN,
		"Read the game archives ahead at start" },

	{ "Mods.Active", SET, Type::Text, "", 0, 0, nullptr, RESTART, "-mod", Legacy::None, nullptr, PLAIN,
		"Active mods in the Mods folder of the user data, highest priority first, separated by ;" },

	// ---- State.ini
	{ "Window.Monitor", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"The monitor the game window was on last (device name); the next start opens there" },
	{ "Hints.HardwareHintShown", STA, Type::Bool, "no", 0, 0, nullptr, NOW, nullptr, OPT, "HardwareHintShown", PLAIN,
		"The hardware hint was shown once" },
	{ "Detection.IdealStaticGameLOD", STA, Type::Enum, nullptr, 0, 0, LOD_LEVELS, NOW, nullptr, OPT, "IdealStaticGameLOD", PLAIN,
		"Detail level found for this computer" },

	{ "Lan.UserName", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, NET, "UserName", LegacyCoding::QuotedUnicode, "Player name" },
	{ "Lan.Color", STA, Type::Int, "-1", 0, 0, nullptr, NOW, nullptr, NET, "Color", PLAIN, "Player color, -1: random" },
	{ "Lan.PlayerTemplate", STA, Type::Int, "-1", 0, 0, nullptr, NOW, nullptr, NET, "PlayerTemplate", PLAIN, "Side, -1: random" },
	{ "Lan.Map", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, NET, "Map", LegacyCoding::QuotedAscii, "Last map" },
	{ "Lan.SuperweaponRestrict", STA, Type::Bool, "no", 0, 0, nullptr, NOW, nullptr, NET, "SuperweaponRestrict", PLAIN, "Limit superweapons" },
	{ "Lan.StartingCash", STA, Type::Int, nullptr, 0, 1000000000, nullptr, NOW, nullptr, NET, "StartingCash", PLAIN, "Starting money" },
	{ "Lan.GracePeriod", STA, Type::Int, "0", 0, 60, nullptr, NOW, nullptr, NET, "GracePeriod", PLAIN, "Start protection in minutes" },
	{ "Lan.MaxZoom", STA, Type::Int, "100", 100, 125, nullptr, NOW, nullptr, Legacy::None, nullptr, PLAIN,
		"Zoom-out limit the host sets for every player, percent of the normal camera height" },
	{ "Lan.UseSystemMapDir", STA, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, NET, "UseSystemMapDir", PLAIN,
		"Map list shows the official maps" },
	{ "Lan.RemoteIPs", STA, Type::List, "", 0, 0, nullptr, NOW, nullptr, NET, "RemoteIP", LegacyCoding::NumberedList,
		"Addresses used for direct connections, newest first" },

	{ "Skirmish.UserName", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, SKI, "UserName", LegacyCoding::QuotedUnicode, "Player name" },
	{ "Skirmish.Color", STA, Type::Int, "-1", 0, 0, nullptr, NOW, nullptr, SKI, "Color", PLAIN, "Player color, -1: random" },
	{ "Skirmish.PlayerTemplate", STA, Type::Int, "-1", 0, 0, nullptr, NOW, nullptr, SKI, "PlayerTemplate", PLAIN, "Side, -1: random" },
	{ "Skirmish.Map", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, SKI, "Map", LegacyCoding::QuotedAscii, "Last map" },
	{ "Skirmish.SuperweaponRestrict", STA, Type::Bool, "no", 0, 0, nullptr, NOW, nullptr, SKI, "SuperweaponRestrict", PLAIN, "Limit superweapons" },
	{ "Skirmish.StartingCash", STA, Type::Int, nullptr, 0, 1000000000, nullptr, NOW, nullptr, SKI, "StartingCash", PLAIN, "Starting money" },
	{ "Skirmish.GracePeriod", STA, Type::Int, "0", 0, 60, nullptr, NOW, nullptr, SKI, "GracePeriod", PLAIN, "Start protection in minutes" },
	{ "Skirmish.UseSystemMapDir", STA, Type::Bool, "yes", 0, 0, nullptr, NOW, nullptr, SKI, "UseSystemMapDir", PLAIN,
		"Map list shows the official maps" },
	{ "Skirmish.SlotList", STA, Type::Text, "", 0, 0, nullptr, NOW, nullptr, SKI, "SlotList", PLAIN, "Last slot setup" },
	{ "Skirmish.FPS", STA, Type::Int, nullptr, 0, 1000000, nullptr, NOW, nullptr, SKI, "FPS", PLAIN, "Game speed" },
};

// No reader since the online code and the old network were removed, or never read at all. The old
// RenderBackend chose Direct3D 9 and is not taken over into Display.RenderBackend.
const char *const DROPPED[] = {
	"TextureFilter", "AnisotropyLevel", "FirewallNeedToRefresh", "FirewallBehavior",
	"FirewallPortOverride", "FirewallPortAllocationDelta", "SendDelay", "GameSpyIPAddress",
	"3DAudioProvider", "SpeakerType", "RenderBackend", "LanguageFilter" };

}	// namespace

std::span<const Definition> schema()
{
	return TABLE;
}

std::span<const char *const> droppedLegacyKeys()
{
	return DROPPED;
}

}	// namespace Settings
}	// namespace Engine
