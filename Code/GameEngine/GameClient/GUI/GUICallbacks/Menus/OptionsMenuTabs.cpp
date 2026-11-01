/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OptionsMenuTabs.cpp -- tab pages of the options menu, see OptionsMenuTabs.h.
//
// The groups of the window file stay intact and are only moved: display (VideoParent), the detail panel
// (WinAdvancedDisplayOptions) and sound (AudioParent and two of its sliders, which the file puts next to it).
// The controls group (ScrollParent) is taken apart into the sections of its page. Hidden check boxes of the
// file (scroll anchor, replay camera) are shown on their pages. The added controls copy the look of controls of the same kind in the file.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/GameEdition.h"
#include "Common/GameLOD.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/UserSettings.h"
#include "Engine/Settings.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetClone.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameFont.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/OptionsMenuMods.h"
#include "GameClient/OptionsMenuTabs.h"
#include "GameClient/WindowLayout.h"
#include "Common/version.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace OptionsMenuTabs
{

namespace
{

// ------------------------------------------------------------------------------------------------
// Settings without a control in the window file

struct Choice
{
	const char *label;		///< text key (contains ':') or the text itself
	const char *value;		///< nullptr: not set, game data or the detail level decide
	Int number;				///< for a label with %d
};

enum class Kind { Check, Combo };

struct Setting
{
	const char *id;
	Kind kind;
	const char *label;
	const Choice *choices;
	Int choiceCount;
	const char *on;			///< check box: value when checked
	const char *off;
};

template <Int N>
constexpr Setting combo( const char *id, const char *label, const Choice ( &choices )[N] )
{
	return Setting{ id, Kind::Combo, label, choices, N, nullptr, nullptr };
}

constexpr Setting check( const char *id, const char *label, const char *on = "yes", const char *off = "no" )
{
	return Setting{ id, Kind::Check, label, nullptr, 0, on, off };
}

const Choice TEXTURE_QUALITY[] = { { "GZH:OptionsAuto", "Auto", 0 }, { "SD", "SD", 0 }, { "HD", "HD", 0 }, { "UHD", "UHD", 0 } };
const Choice RENDER_SCALE[] = { { "GZH:OptionsByDetail", nullptr, 0 }, { "200 %", "2", 0 }, { "150 %", "1.5", 0 },
	{ "125 %", "1.25", 0 }, { "100 %", "1", 0 }, { "90 %", "0.9", 0 }, { "75 %", "0.75", 0 }, { "67 %", "0.67", 0 },
	{ "50 %", "0.5", 0 } };
const Choice RENDER_SCALE_FILTER[] = { { "GZH:OptionsByDetail", nullptr, 0 }, { "GZH:OptionsFilterBilinear", "bilinear", 0 },
	{ "GZH:OptionsFilterBicubic", "bicubic", 0 }, { "GZH:OptionsFilterCas", "cas", 0 } };
const Choice FPS_LIMIT[] = { { "GZH:OptionsOff", "0", 0 }, { "30 FPS", "30", 0 }, { "60 FPS", "60", 0 }, { "75 FPS", "75", 0 },
	{ "120 FPS", "120", 0 }, { "144 FPS", "144", 0 }, { "165 FPS", "165", 0 }, { "240 FPS", "240", 0 } };
const Choice WATER_QUALITY[] = { { "GZH:OptionsByDetail", nullptr, 0 }, { "GZH:OptionsWaterClassic", "0", 0 },
	{ "GZH:OptionsWaterMedium", "1", 0 }, { "GZH:OptionsWaterHigh", "2", 0 } };
const Choice ANISOTROPY[] = { { "GZH:OptionsByDetail", nullptr, 0 }, { "GZH:OptionsOff", "0", 0 }, { "2x", "2", 0 },
	{ "4x", "4", 0 }, { "8x", "8", 0 }, { "16x", "16", 0 } };
const Choice CURSOR_SCALE[] = { { "GZH:OptionsAuto", "0", 0 }, { "100 %", "100", 0 }, { "125 %", "125", 0 },
	{ "150 %", "150", 0 }, { "200 %", "200", 0 } };
const Choice MAX_ZOOM[] = { { "GZH:OptionsStandard", "1", 0 }, { "110 %", "1.1", 0 }, { "125 %", "1.25", 0 } };
const Choice SPECTATOR_DELAY[] = { { "GZH:OptionsNone", "0", 0 }, { "GZH:OptionsSeconds", "30", 30 },
	{ "GZH:OptionsSeconds", "60", 60 }, { "GZH:OptionsSeconds", "120", 120 }, { "GZH:OptionsSeconds", "300", 300 } };
const Choice RETURN_WINDOW[] = { { "GZH:OptionsOff", "0", 0 }, { "GZH:OptionsMinutes", "60", 1 }, { "GZH:OptionsMinutes", "120", 2 },
	{ "GZH:OptionsMinutes", "300", 5 }, { "GZH:OptionsMinutes", "600", 10 }, { "GZH:OptionsMinutes", "1800", 30 } };
const Choice WORKER_THREADS[] = { { "GZH:OptionsAuto", "-1", 0 }, { "GZH:OptionsOff", "0", 0 }, { "1", "1", 0 }, { "2", "2", 0 },
	{ "3", "3", 0 }, { "4", "4", 0 }, { "6", "6", 0 }, { "8", "8", 0 } };

const Setting TEXTURE_QUALITY_SETTING = combo( "Detail.TextureQuality", "GZH:OptionsTextureQuality", TEXTURE_QUALITY );
const Setting RENDER_SCALE_SETTING = combo( "Display.RenderScale", "GZH:OptionsRenderScale", RENDER_SCALE );
const Setting RENDER_SCALE_FILTER_SETTING = combo( "Display.RenderScaleFilter", "GZH:OptionsRenderScaleFilter", RENDER_SCALE_FILTER );
const Setting FPS_LIMIT_SETTING = combo( "Display.RenderFPSLimit", "GZH:OptionsFPSLimit", FPS_LIMIT );
const Setting WATER_QUALITY_SETTING = combo( "Detail.WaterQuality", "GZH:OptionsWaterQuality", WATER_QUALITY );
const Setting ANISOTROPY_SETTING = combo( "Detail.TerrainAnisotropy", "GZH:OptionsTerrainAnisotropy", ANISOTROPY );
const Setting VSYNC_SETTING = check( "Display.VSync", "GZH:OptionsVSync" );
const Setting RENDER_BACKEND_SETTING = check( "Display.RenderBackend", "GZH:OptionsVulkan", "vulkan", "d3d11" );
const Setting FREE_CONTENT_SETTING = check( "Detail.FreeContent", "GZH:OptionsFreeContent", "free", "ea" );
const Setting WATER_TEXTURE_SETTING = check( "Detail.WaterTexture", "GZH:OptionsWaterTexture", "free", "ea" );
const Setting EDGE_SCROLL_SETTING = check( "Controls.EdgeScrollInWindow", "GZH:OptionsEdgeScrollInWindow" );
const Setting HIDE_CURSOR_SETTING = check( "Controls.HideCursorWhileDragging", "GZH:OptionsHideCursorWhileDragging" );
const Setting CONFINE_CURSOR_SETTING = check( "Controls.ConfineCursorInWindow", "GZH:OptionsConfineCursor" );
const Setting CURSOR_SCALE_SETTING = combo( "Controls.CursorScale", "GZH:OptionsCursorScale", CURSOR_SCALE );
const Setting MAX_ZOOM_SETTING = combo( "Camera.MaxZoomFactor", "GZH:OptionsMaxZoom", MAX_ZOOM );
const Setting ALLOW_SPECTATORS_SETTING = check( "Network.AllowSpectators", "GZH:OptionsAllowSpectators" );
const Setting SPECTATOR_DELAY_SETTING = combo( "Network.SpectatorDelay", "GZH:OptionsSpectatorDelay", SPECTATOR_DELAY );
const Setting RETURN_WINDOW_SETTING = combo( "Network.ReturnWindow", "GZH:OptionsReturnWindow", RETURN_WINDOW );
const Setting WORKER_THREADS_SETTING = combo( "System.WorkerThreads", "GZH:OptionsWorkerThreads", WORKER_THREADS );
const Setting PREFETCH_SETTING = check( "System.PrefetchArchives", "GZH:OptionsPrefetchArchives" );
const Setting HARDWARE_HINT_SETTING = check( "General.HardwareHint", "GZH:OptionsHardwareHint" );
const Setting TEST_AIDS_SETTING = check( "General.TestAids", "GZH:OptionsTestAids" );

// Direct3D 11 effects, each an exception to the list of the detail level ("auto" keeps the list)
const Choice EFFECT_ON[] = { { "GZH:OptionsEffectAuto", "auto", 0 }, { "GZH:OptionsOff", "off", 0 }, { "GZH:OptionsOn", "on", 0 } };
// Depth effects with multisampling: auto means on as soon as multisampling runs, not "as the detail level".
const Choice EFFECT_MSAA_DEPTH[] = { { "GZH:OptionsEffectWithMsaa", "auto", 0 }, { "GZH:OptionsOff", "off", 0 },
	{ "GZH:OptionsOn", "on", 0 } };
const Choice EFFECT_LEVEL[] = { { "GZH:OptionsEffectAuto", "auto", 0 }, { "GZH:OptionsOff", "off", 0 },
	{ "GZH:OptionsEffectNormal", "1", 0 }, { "GZH:OptionsEffectStrong", "2", 0 } };
const Choice EFFECT_SHARPEN[] = { { "GZH:OptionsEffectAuto", "auto", 0 }, { "GZH:OptionsOff", "off", 0 },
	{ "GZH:OptionsEffectWeak", "0.25", 0 }, { "GZH:OptionsEffectMedium", "0.5", 0 }, { "GZH:OptionsEffectStrong", "1", 0 } };
const Choice EFFECT_SHADOW_MAP[] = { { "GZH:OptionsEffectAuto", "auto", 0 }, { "GZH:OptionsOff", "off", 0 },
	{ "1024", "1024", 0 }, { "2048", "2048", 0 }, { "4096", "4096", 0 } };
const Choice EFFECT_SHARE[] = { { "GZH:OptionsEffectAuto", "auto", 0 }, { "GZH:OptionsOff", "off", 0 },
	{ "GZH:OptionsEffectHalf", "0.5", 0 }, { "GZH:OptionsEffectFull", "1", 0 } };

const Setting EFFECTS_IMAGE[] = {
	combo( "Effects.FXAA", "GZH:OptionsEffectFXAA", EFFECT_ON ),
	combo( "Effects.TAA", "GZH:OptionsEffectTAA", EFFECT_ON ),
	combo( "Effects.Sharpen", "GZH:OptionsEffectSharpen", EFFECT_SHARPEN ),
	combo( "Effects.ColorGrade", "GZH:OptionsEffectColorGrade", EFFECT_ON ),
	combo( "Effects.SSAO", "GZH:OptionsEffectSSAO", EFFECT_ON ),
	combo( "Effects.GTAO", "GZH:OptionsEffectGTAO", EFFECT_ON ),
	combo( "Effects.Bloom", "GZH:OptionsEffectBloom", EFFECT_LEVEL ),
	combo( "Effects.Lights", "GZH:OptionsEffectLights", EFFECT_LEVEL ),
	combo( "Effects.ShadowMap", "GZH:OptionsEffectShadowMap", EFFECT_SHADOW_MAP ),
	combo( "Effects.ShadowTerrain", "GZH:OptionsEffectShadowTerrain", EFFECT_SHARE ),
	combo( "Effects.SoftShadows", "GZH:OptionsEffectSoftShadows", EFFECT_LEVEL ),
	combo( "Effects.ContactShadows", "GZH:OptionsEffectContactShadows", EFFECT_ON ),
	combo( "Effects.ShadowCascades", "GZH:OptionsEffectShadowCascades", EFFECT_ON ),
	combo( "Effects.SoftParticles", "GZH:OptionsEffectSoftParticles", EFFECT_ON ),
	combo( "Effects.AlphaCoverage", "GZH:OptionsEffectAlphaCoverage", EFFECT_ON ) };
const Setting EFFECTS_WORLD[] = {
	combo( "Effects.TerrainDetail", "GZH:OptionsEffectTerrainDetail", EFFECT_LEVEL ),
	combo( "Effects.TerrainRock", "GZH:OptionsEffectTerrainRock", EFFECT_ON ),
	combo( "Effects.TerrainNormal", "GZH:OptionsEffectTerrainNormal", EFFECT_LEVEL ),
	combo( "Effects.TerrainTess", "GZH:OptionsEffectTerrainTess", EFFECT_LEVEL ),
	combo( "Effects.RockTess", "GZH:OptionsEffectRockTess", EFFECT_LEVEL ),
	combo( "Effects.ParticleLight", "GZH:OptionsEffectParticleLight", EFFECT_ON ),
	combo( "Effects.ParticleGround", "GZH:OptionsEffectParticleGround", EFFECT_ON ),
	combo( "Effects.Heat", "GZH:OptionsEffectHeat", EFFECT_LEVEL ),
	combo( "Effects.Haze", "GZH:OptionsEffectHaze", EFFECT_ON ),
	combo( "Effects.MsaaDepth", "GZH:OptionsEffectMsaaDepth", EFFECT_MSAA_DEPTH ),
	combo( "Effects.WaterSSR", "GZH:OptionsEffectWaterSSR", EFFECT_ON ),
	combo( "Effects.Deband", "GZH:OptionsEffectDeband", EFFECT_ON ) };

const char *const TAB_NAMES[PAGE_TAB_COUNT] = { "OptionsMenu.wnd:ButtonTabDisplay", "OptionsMenu.wnd:ButtonTabDetails",
	"OptionsMenu.wnd:ButtonTabSound", "OptionsMenu.wnd:ButtonTabControls", "OptionsMenu.wnd:ButtonTabGame",
	"OptionsMenu.wnd:ButtonTabEffects" };
const char *const TAB_LABELS[PAGE_TAB_COUNT] = { "GZH:OptionsTabDisplay", "GZH:OptionsTabDetails", "GZH:OptionsTabSound",
	"GZH:OptionsTabControls", "GZH:OptionsTabGame", "GZH:OptionsTabEffects" };

// ------------------------------------------------------------------------------------------------
// State of the open menu; plain arrays, so nothing stays allocated after the menu closes

struct Control
{
	const Setting *setting;
	GameWindow *window;
	Int initial;			///< combo box: selected position; check box: 1 if checked
};

constexpr Int MAX_CONTROLS = 96;
constexpr Int MAX_PAGE_WINDOWS = 96;

Control s_controls[MAX_CONTROLS];
Int s_controlCount = 0;
GameWindow *s_pageWindows[PAGE_COUNT][MAX_PAGE_WINDOWS];
Int s_pageWindowCount[PAGE_COUNT];
GameWindow *s_tabs[PAGE_TAB_COUNT];
GameWindow *s_modsButton = nullptr;	///< opens the mods page
GameWindow *s_modsSummary = nullptr;	///< below it: mods found and on
GameWindow *s_aboutButton = nullptr;	///< opens the page about GZH Neo
GameWindow *s_aboutBack = nullptr;	///< back from it to the game tab
Bool s_built = FALSE;
GameWindow *s_removeListButton = nullptr;	///< removes the old D3D11Effects list on "Accept"
Bool s_removeList = FALSE;
Int s_page = PAGE_DISPLAY;

// The current tab is drawn inverted: a face in the colour of the headers with dark text.
GameWinDrawFunc s_tabBaseDraw = nullptr;
Color s_accent = 0;

Int tabIndex( const GameWindow *window )
{
	for( Int page = 0; page < PAGE_TAB_COUNT; ++page )
		if( s_tabs[page] == window )
			return page;
	return -1;
}

// The tab drawn as current: the mods page belongs to the game tab.
Int currentTab()
{
	return ( s_page == PAGE_MODS || s_page == PAGE_ABOUT ) ? PAGE_GAME : s_page;
}

void drawTab( GameWindow *window, WinInstanceData *instData )
{
	if( s_tabBaseDraw )
		s_tabBaseDraw( window, instData );
	if( tabIndex( window ) != currentTab() )
		return;
	ICoord2D origin, size;
	window->winGetScreenPosition( &origin.x, &origin.y );
	window->winGetSize( &size.x, &size.y );
	TheWindowManager->winFillRect( s_accent, 1.0f, origin.x, origin.y, origin.x + size.x, origin.y + size.y );
	DisplayString *text = instData->getTextDisplayString();
	if( text && text->getTextLength() > 0 )
	{
		Int w, h;
		text->getSize( &w, &h );
		text->draw( origin.x + ( size.x - w ) / 2, origin.y + ( size.y - h ) / 2, GameMakeColor( 16, 16, 24, 255 ),
			GameMakeColor( 0, 0, 0, 0 ) );
	}
}

// Geometry, all in coordinates of the frame window that holds the groups.
struct Layout
{
	GameWindow *frame;
	Int top, bottom;
	Int labelIndent, comboIndent, gap;
	Int labelHeight, comboHeight, checkHeight, headerHeight;
	GameWindow *labelModel, *comboModel, *checkModel, *headerModel, *noteModel;
};

struct Column
{
	Int x, width, y;
	Int page;
};

GameWindow *findWindow( GameWindow *root, const char *name )
{
	return TheWindowManager->winGetWindowFromId( root, TheNameKeyGenerator->nameToKey( AsciiString( name ) ) );
}

// Windows without a name are found by their text key.
GameWindow *findByText( GameWindow *root, const char *textKey )
{
	for( GameWindow *child = root->winGetChild(); child; child = child->winGetNext() )
	{
		if( child->winGetInstanceData()->m_textLabelString == textKey )
			return child;
		if( GameWindow *found = findByText( child, textKey ) )
			return found;
	}
	return nullptr;
}

void framePosition( GameWindow *window, GameWindow *frame, Int *x, Int *y )
{
	Int wx, wy, fx, fy;
	window->winGetScreenPosition( &wx, &wy );
	frame->winGetScreenPosition( &fx, &fy );
	*x = wx - fx;
	*y = wy - fy;
}

void moveTo( GameWindow *window, GameWindow *frame, Int x, Int y )
{
	Int px = 0, py = 0;
	GameWindow *parent = window->winGetParent();
	if( parent && parent != frame )
		framePosition( parent, frame, &px, &py );
	window->winSetPosition( x - px, y - py );
}

void moveBy( GameWindow *window, Int dx, Int dy )
{
	Int x, y;
	window->winGetPosition( &x, &y );
	window->winSetPosition( x + dx, y + dy );
}

void addToPage( Int page, GameWindow *window )
{
	if( window == nullptr )
		return;
	// a window left out would never be hidden and show on every page
	DEBUG_ASSERTCRASH( s_pageWindowCount[page] < MAX_PAGE_WINDOWS, ( "options page %d: too many windows", page ) );
	if( s_pageWindowCount[page] >= MAX_PAGE_WINDOWS )
		return;
	s_pageWindows[page][s_pageWindowCount[page]++] = window;
}

// ------------------------------------------------------------------------------------------------
// Font levels: every text of a page has one of four levels, each level one font per page

constexpr Int LEVEL_COUNT = 4;
constexpr Int GROUP_TABS = PAGE_COUNT;			///< the tab row and the buttons below the pages shrink as one
constexpr Int GROUP_COUNT = PAGE_COUNT + 1;
constexpr Int MAX_LEVEL_WINDOWS = 96;
// size in the 800 x 600 raster (scaled with the screen height) and the smallest size a level shrinks to, both
// unused for the titles (levelStartPoints); bold
constexpr Int LEVEL_SIZE[LEVEL_COUNT] = { 0, 14, 12, 10 };
constexpr Int LEVEL_MIN_SIZE[LEVEL_COUNT] = { 0, 10, 10, 10 };
constexpr Bool LEVEL_BOLD[LEVEL_COUNT] = { FALSE, TRUE, FALSE, FALSE };

struct LevelWindow
{
	GameWindow *window;
	FontLevel level;
};

LevelWindow s_levelWindows[GROUP_COUNT][MAX_LEVEL_WINDOWS];
Int s_levelWindowCount[GROUP_COUNT];
// the face of each level: the buttons' face for the titles, the labels' face for the rest
const GameFont *s_levelFace[LEVEL_COUNT];
// the size the tab row ended with; the buttons on the pages do not get larger
Int s_titlePoints = 0;

void setLevel( Int group, GameWindow *window, FontLevel level )
{
	if( window == nullptr )
		return;
	DEBUG_ASSERTCRASH( s_levelWindowCount[group] < MAX_LEVEL_WINDOWS, ( "options group %d: too many texts", group ) );
	if( s_levelWindowCount[group] >= MAX_LEVEL_WINDOWS )
		return;
	s_levelWindows[group][s_levelWindowCount[group]++] = { window, level };
}

void addToPage( Int page, GameWindow *window, FontLevel level )
{
	addToPage( page, window );
	setLevel( page, window, level );
}

// The headers of the groups in the window file; their other texts are labels.
Bool isGroupHeader( const GameWindow *window )
{
	static const char *const HEADERS[] = { "GUI:DisplayOptions", "GUI:AudioOptions", "GUI:ControlOptions",
		"GUI:AdvanceDisplayOptions", "GUI:AdvancedOptionsToggleOnOff" };
	const AsciiString &key = const_cast<GameWindow *>( window )->winGetInstanceData()->m_textLabelString;
	for( const char *header : HEADERS )
		if( key == header )
			return TRUE;
	return FALSE;
}

// A group of the window file: its headers, labels, check boxes, combo boxes and buttons get their levels.
void setGroupLevels( Int page, GameWindow *root )
{
	for( GameWindow *child = root->winGetChild(); child; child = child->winGetNext() )
	{
		const UnsignedInt style = child->winGetStyle();
		if( BitTest( style, GWS_STATIC_TEXT ) )
			setLevel( page, child, isGroupHeader( child ) ? FontLevel::Section : FontLevel::Text );
		else if( BitTest( style, GWS_CHECK_BOX ) || BitTest( style, GWS_COMBO_BOX ) || BitTest( style, GWS_RADIO_BUTTON ) )
			setLevel( page, child, FontLevel::Text );
		else if( BitTest( style, GWS_PUSH_BUTTON ) )
			setLevel( page, child, FontLevel::Title );
		else if( !BitTest( style, GWS_GADGET_WINDOW ) )
			setGroupLevels( page, child );
	}
}

// Point sizes after the screen scaling: the titles start at the size of the buttons of the file (the
// buttons below the pages keep it), the other levels at their size in the 800 x 600 raster.
Int levelStartPoints( Int index )
{
	const GameFont *face = s_levelFace[index];
	if( index == static_cast<Int>( FontLevel::Title ) && face != nullptr )
		return s_titlePoints > 0 ? s_titlePoints : face->pointSize;
	return TheGlobalLanguageData ? TheGlobalLanguageData->adjustFontSize( LEVEL_SIZE[index] ) : LEVEL_SIZE[index];
}

Int levelMinPoints( Int index )
{
	if( index == static_cast<Int>( FontLevel::Title ) && s_levelFace[index] != nullptr )
		return s_levelFace[index]->pointSize * 7 / 10;
	return TheGlobalLanguageData ? TheGlobalLanguageData->adjustFontSize( LEVEL_MIN_SIZE[index] ) : LEVEL_MIN_SIZE[index];
}

GameFont *levelFont( Int index, Int points )
{
	const GameFont *face = s_levelFace[index];
	if( face == nullptr || TheFontLibrary == nullptr )
		return nullptr;
	const Bool bold = index == static_cast<Int>( FontLevel::Title ) ? face->bold : LEVEL_BOLD[index];
	return TheFontLibrary->getFont( face->nameString, points, bold );
}

Int measureWidth( DisplayString *measure, const UnicodeString &text, GameFont *font )
{
	measure->setFont( font );
	measure->setText( text );
	Int width = 0, height = 0;
	measure->getSize( &width, &height );
	return width;
}

// The text of the window fits on one line in the font (a static text with room for three lines wraps).
Bool textFits( GameWindow *window, GameFont *font, DisplayString *measure )
{
	const UnsignedInt style = window->winGetStyle();
	Int width, height;
	window->winGetSize( &width, &height );
	if( BitTest( style, GWS_COMBO_BOX ) )
	{
		GameWindow *list = GadgetComboBoxGetListBox( window );
		GameWindow *edit = GadgetComboBoxGetEditBox( window );
		if( list == nullptr || edit == nullptr )
			return TRUE;
		Int editW, editH;
		edit->winGetSize( &editW, &editH );
		const Int available = editW - editW / 12 - 2;
		const Int count = GadgetListBoxGetNumEntries( list );
		for( Int row = 0; row < count; ++row )
			if( measureWidth( measure, GadgetListBoxGetText( list, row ), font ) > available )
				return FALSE;
		return TRUE;
	}
	DisplayString *text = nullptr;
	if( BitTest( style, GWS_STATIC_TEXT ) )
	{
		const TextData *data = static_cast<const TextData *>( window->winGetUserData() );
		text = data ? data->text : nullptr;
		// a row holds one line even when two small ones would fit; only a text box of several rows wraps
		if( height >= 3 * font->height )
			return TRUE;
	}
	else
		text = window->winGetInstanceData()->getTextDisplayString();
	if( text == nullptr || text->getTextLength() == 0 )
		return TRUE;
	Int available = width - width / 16 - 2;
	if( BitTest( style, GWS_CHECK_BOX ) )
		available -= height;
	return measureWidth( measure, text->getText(), font ) <= available;
}

// Each level gets the largest font, not below its smallest size, in which all its texts of the group fit.
// A text that does not fit even then shrinks alone, down to 70 percent of that size.
void applyLevels( Int group )
{
	if( TheDisplayStringManager == nullptr )
		return;
	DisplayString *measure = TheDisplayStringManager->newDisplayString();
	for( Int index = 0; index < LEVEL_COUNT; ++index )
	{
		const FontLevel level = static_cast<FontLevel>( index );
		const Int minPoints = levelMinPoints( index );
		GameFont *font = nullptr;
		Int points = levelStartPoints( index );
		for( ; points >= minPoints; --points )
		{
			font = levelFont( index, points );
			if( font == nullptr )
				break;
			Bool fits = TRUE;
			for( Int i = 0; i < s_levelWindowCount[group] && fits; ++i )
				if( s_levelWindows[group][i].level == level )
					fits = textFits( s_levelWindows[group][i].window, font, measure );
			if( fits )
				break;
		}
		if( font == nullptr )
			continue;
		if( points < minPoints )
			points = minPoints;
		if( group == GROUP_TABS && level == FontLevel::Title )
			s_titlePoints = points;
		for( Int i = 0; i < s_levelWindowCount[group]; ++i )
		{
			if( s_levelWindows[group][i].level != level )
				continue;
			GameWindow *window = s_levelWindows[group][i].window;
			GameFont *own = font;
			for( Int smaller = points - 1; !textFits( window, own, measure ) && smaller >= points * 7 / 10; --smaller )
				if( GameFont *next = levelFont( index, smaller ) )
					own = next;
			window->winSetFont( own );
		}
	}
	TheDisplayStringManager->freeDisplayString( measure );
}

// Marked with "*": takes effect after a restart, or for the effects from the next map on (the page says which).
Bool needsRestart( const char *id )
{
	const Engine::Settings::Definition *definition = Engine::Settings::findDefinition( id );
	return definition != nullptr && ( definition->apply == Engine::Settings::Apply::Restart
		|| definition->apply == Engine::Settings::Apply::NextMatch );
}

UnicodeString labelText( const char *key, Bool restart )
{
	UnicodeString text = TheGameText->fetch( key );
	if( restart )
		text.concat( L" *" );
	return text;
}

UnicodeString choiceText( const Choice &choice )
{
	UnicodeString text;
	if( strchr( choice.label, ':' ) == nullptr )
	{
		text.translate( AsciiString( choice.label ) );
		return text;
	}
	text = TheGameText->fetch( choice.label );
	if( wcsstr( text.str(), L"%d" ) != nullptr )
	{
		UnicodeString formatted;
		formatted.formatUnchecked( text.str(), choice.number );
		return formatted;
	}
	return text;
}

// Numbers compare by value ("1" and "1.0"), everything else without case.
Bool sameValue( const char *a, const char *b )
{
	char *endA = nullptr;
	char *endB = nullptr;
	const double x = strtod( a, &endA );
	const double y = strtod( b, &endB );
	if( *a != 0 && *b != 0 && *endA == 0 && *endB == 0 )
		return fabs( x - y ) < 0.001;
	return AsciiString( a ).compareNoCase( b ) == 0;
}

// Index of the choice the settings select now, -1 if the value is not in the list.
Int currentChoice( const Setting &setting )
{
	const Bool given = UserSettings::isSetInFile( setting.id ) || UserSettings::isForced( setting.id );
	if( !given )
	{
		for( Int i = 0; i < setting.choiceCount; ++i )
			if( setting.choices[i].value == nullptr )
				return i;
	}
	const AsciiString value = UserSettings::get( setting.id );
	for( Int i = 0; i < setting.choiceCount; ++i )
		if( setting.choices[i].value != nullptr && sameValue( value.str(), setting.choices[i].value ) )
			return i;
	return -1;
}

Int defaultChoice( const Setting &setting )
{
	const Engine::Settings::Definition *definition = Engine::Settings::findDefinition( setting.id );
	const char *value = definition ? definition->defaultValue : nullptr;
	for( Int i = 0; i < setting.choiceCount; ++i )
	{
		const char *choice = setting.choices[i].value;
		if( ( value == nullptr && choice == nullptr ) || ( value && choice && sameValue( value, choice ) ) )
			return i;
	}
	return -1;
}

void selectChoice( GameWindow *box, Int choice )
{
	const Int count = GadgetComboBoxGetLength( box );
	for( Int pos = 0; pos < count; ++pos )
	{
		if( static_cast<Int>( reinterpret_cast<intptr_t>( GadgetComboBoxGetItemData( box, pos ) ) ) == choice )
		{
			GadgetComboBoxSetSelectedPos( box, pos, TRUE );
			return;
		}
	}
}

void addControl( const Setting &setting, GameWindow *window )
{
	if( window == nullptr )
		return;
	DEBUG_ASSERTCRASH( s_controlCount < MAX_CONTROLS, ( "options menu: too many settings" ) );
	if( s_controlCount >= MAX_CONTROLS )
		return;
	Control &control = s_controls[s_controlCount++];
	control.setting = &setting;
	control.window = window;
	control.initial = 0;

	if( setting.kind == Kind::Check )
	{
		control.initial = UserSettings::get( setting.id ).compareNoCase( setting.on ) == 0 ? 1 : 0;
		GadgetCheckBoxSetChecked( window, control.initial != 0 );
	}
	else
	{
		const Color color = window->winGetEnabledTextColor();
		GadgetComboBoxReset( window );
		for( Int i = 0; i < setting.choiceCount; ++i )
		{
			const Int pos = GadgetComboBoxAddEntry( window, choiceText( setting.choices[i] ), color );
			GadgetComboBoxSetItemData( window, pos, reinterpret_cast<void *>( static_cast<intptr_t>( i ) ) );
		}
		Int choice = currentChoice( setting );
		if( choice < 0 )
		{
			// a value set by hand: shown as it is, kept unless another entry is chosen
			UnicodeString text;
			text.translate( UserSettings::get( setting.id ) );
			const Int pos = GadgetComboBoxAddEntry( window, text, color );
			GadgetComboBoxSetItemData( window, pos, reinterpret_cast<void *>( static_cast<intptr_t>( -1 ) ) );
		}
		selectChoice( window, choice );
		GadgetComboBoxGetSelectedPos( window, &control.initial );
	}

	// the command line decides for this run
	if( UserSettings::isForced( setting.id ) )
		window->winEnable( FALSE );
}

// ------------------------------------------------------------------------------------------------
// Placing controls in a column

void addHeader( Layout &layout, Column &column, const char *textKey, const char *name )
{
	GameWindow *header = GadgetCloneStaticText( layout.headerModel, layout.frame, name, column.x, column.y,
																							column.width, layout.headerHeight, textKey );
	addToPage( column.page, header, FontLevel::Section );
	column.y += layout.headerHeight + layout.gap;
}

GameWindow *addLabel( Layout &layout, Column &column, const UnicodeString &text, const char *name )
{
	GameWindow *label = GadgetCloneStaticText( layout.labelModel, layout.frame, name, column.x + layout.labelIndent, column.y,
																						 column.width - layout.labelIndent, layout.labelHeight, nullptr );
	if( label )
		GadgetStaticTextSetText( label, text );
	addToPage( column.page, label, FontLevel::Text );
	column.y += layout.labelHeight;
	return label;
}

AsciiString controlName( const Setting &setting, const char *prefix )
{
	AsciiString name;
	name.format( "OptionsMenu.wnd:%s%s", prefix, setting.id );
	return name;
}

void addCombo( Layout &layout, Column &column, const Setting &setting )
{
	addLabel( layout, column, labelText( setting.label, needsRestart( setting.id ) ), controlName( setting, "Label" ).str() );
	Int width = column.width - 2 * layout.comboIndent;
	GameWindow *box = GadgetCloneComboBox( layout.comboModel, layout.frame, controlName( setting, "ComboBox" ).str(),
																				 column.x + layout.comboIndent, column.y, width, layout.comboHeight );
	addToPage( column.page, box, FontLevel::Text );
	addControl( setting, box );
	column.y += layout.comboHeight + 2 * layout.gap;
}

void addCheck( Layout &layout, Column &column, const Setting &setting )
{
	GameWindow *box = GadgetCloneCheckBox( layout.checkModel, layout.frame, controlName( setting, "Check" ).str(),
																				 column.x + layout.comboIndent, column.y, column.width - layout.comboIndent,
																				 layout.checkHeight, nullptr );
	if( box )
		GadgetCheckBoxSetText( box, labelText( setting.label, needsRestart( setting.id ) ) );
	addToPage( column.page, box, FontLevel::Text );
	addControl( setting, box );
	column.y += layout.checkHeight + layout.gap;
}

// A check box of the window file, hidden there, moved into the column. textKey replaces its text (the
// Generals data have no translation for some of them).
void addExistingCheck( Layout &layout, Column &column, GameWindow *box, const char *textKey = nullptr )
{
	if( box == nullptr )
		return;
	if( textKey )
		GadgetCheckBoxSetText( box, TheGameText->fetch( textKey ) );
	moveTo( box, layout.frame, column.x + layout.comboIndent, column.y );
	box->winSetSize( column.width - layout.comboIndent, layout.checkHeight );
	box->winHide( FALSE );
	addToPage( column.page, box, FontLevel::Text );
	column.y += layout.checkHeight + layout.gap;
}

// Some labels of the file are centred over their control; in the sections they start at the left edge.
void alignLeft( GameWindow *label )
{
	if( label && BitTest( label->winGetStyle(), GWS_STATIC_TEXT ) )
		if( TextData *data = static_cast<TextData *>( label->winGetUserData() ) )
			data->centered = FALSE;
}

// A window of a group of the file that is hidden as a whole moves to the frame; its owner, which gets its
// messages, stays.
GameWindow *takeOut( GameWindow *window, GameWindow *frame )
{
	if( window && window->winGetParent() != frame )
	{
		Int x, y;
		framePosition( window, frame, &x, &y );
		window->winSetParent( frame );
		window->winSetPosition( x, y );
	}
	return window;
}

// The label of a slider of the file in its own row, the slider below it in its own width.
void addExistingSlider( Layout &layout, Column &column, GameWindow *label, GameWindow *slider )
{
	if( label )
	{
		moveTo( label, layout.frame, column.x + layout.labelIndent, column.y );
		label->winSetSize( column.width - layout.labelIndent, layout.labelHeight );
		alignLeft( label );
		label->winHide( FALSE );
		addToPage( column.page, label, FontLevel::Text );
		column.y += layout.labelHeight;
	}
	if( slider )
	{
		Int width, height;
		slider->winGetSize( &width, &height );
		moveTo( slider, layout.frame, column.x + layout.comboIndent, column.y );
		slider->winHide( FALSE );
		addToPage( column.page, slider );
		column.y += height + layout.gap;
	}
}

// A label and a combo box of the file in one row, laid out like addRowCombo.
void addExistingRowCombo( Layout &layout, Column &column, GameWindow *label, GameWindow *box, Int labelShare = 11 )
{
	const Int labelWidth = column.width * labelShare / 20;
	if( label )
	{
		moveTo( label, layout.frame, column.x + layout.labelIndent, column.y );
		label->winSetSize( labelWidth - layout.labelIndent, layout.comboHeight );
		alignLeft( label );
		label->winHide( FALSE );
		addToPage( column.page, label, FontLevel::Text );
	}
	if( box )
	{
		moveTo( box, layout.frame, column.x + labelWidth, column.y );
		box->winSetSize( column.width - labelWidth - layout.labelIndent, layout.comboHeight );
		box->winHide( FALSE );
		addToPage( column.page, box, FontLevel::Text );
	}
	column.y += layout.comboHeight + layout.gap;
}

void addNote( Layout &layout, Int page, Int x, Int width, const char *name, const char *textKey = "GZH:OptionsRestartNote" )
{
	Int noteHeight = layout.labelHeight;
	if( layout.noteModel != layout.labelModel )
	{
		Int w;
		layout.noteModel->winGetSize( &w, &noteHeight );
	}
	// left-aligned with the labels of the page
	GameWindow *note = GadgetCloneStaticText( layout.noteModel, layout.frame, name, x + layout.labelIndent,
																						layout.bottom - noteHeight, width - layout.labelIndent, noteHeight, textKey );
	addToPage( page, note, FontLevel::Hint );
}

// Label on the left, the box on the right of the same row; the label takes labelShare twentieths of the column.
void addRowCombo( Layout &layout, Column &column, const Setting &setting, Int labelShare = 11 )
{
	const Int labelWidth = column.width * labelShare / 20;
	GameWindow *label = GadgetCloneStaticText( layout.labelModel, layout.frame, controlName( setting, "Label" ).str(),
																						 column.x + layout.labelIndent, column.y, labelWidth - layout.labelIndent,
																						 layout.comboHeight, nullptr );
	if( label )
		GadgetStaticTextSetText( label, labelText( setting.label, needsRestart( setting.id ) ) );
	addToPage( column.page, label, FontLevel::Text );
	GameWindow *box = GadgetCloneComboBox( layout.comboModel, layout.frame, controlName( setting, "ComboBox" ).str(),
																				 column.x + labelWidth, column.y, column.width - labelWidth - layout.labelIndent,
																				 layout.comboHeight );
	addToPage( column.page, box, FontLevel::Text );
	addControl( setting, box );
	column.y += layout.comboHeight + layout.gap;
}

void checkFits( const Layout &layout, const Column &column )
{
	DEBUG_ASSERTLOG( column.y <= layout.bottom, ( "OptionsMenuTabs: page %d overflows by %d pixels\n", column.page,
		column.y - layout.bottom ) );
}

}	// namespace

// ------------------------------------------------------------------------------------------------

Bool build( GameWindow *menuParent )
{
	release();
	if( menuParent == nullptr )
		return FALSE;

	GameWindow *frame = findWindow( menuParent, "OptionsMenu.wnd:OptionsMenuParentOld" );
	GameWindow *video = findWindow( menuParent, "OptionsMenu.wnd:VideoParent" );
	GameWindow *audio = findWindow( menuParent, "OptionsMenu.wnd:AudioParent" );
	GameWindow *scroll = findWindow( menuParent, "OptionsMenu.wnd:ScrollParent" );
	GameWindow *advanced = findWindow( menuParent, "OptionsMenu.wnd:WinAdvancedDisplayOptions" );
	GameWindow *accept = findWindow( menuParent, "OptionsMenu.wnd:ButtonAccept" );
	GameWindow *defaults = findWindow( menuParent, "OptionsMenu.wnd:ButtonDefaults" );
	GameWindow *labelModel = findWindow( menuParent, "OptionsMenu.wnd:ResolutionLabel" );
	GameWindow *comboModel = findWindow( menuParent, "OptionsMenu.wnd:ComboBoxDetail" );
	// the small check boxes of the detail panel, so that longer texts fit
	GameWindow *checkModel = findWindow( menuParent, "OptionsMenu.wnd:Check3DShadows" );
	if( checkModel == nullptr )
		checkModel = findWindow( menuParent, "OptionsMenu.wnd:CheckAlternateMouse" );
	GameWindow *title = frame ? findByText( frame, "GUI:Options" ) : nullptr;
	GameWindow *headerModel = video ? findByText( video, "GUI:DisplayOptions" ) : nullptr;
	if( frame == nullptr || video == nullptr || audio == nullptr || scroll == nullptr || advanced == nullptr || accept == nullptr
			|| defaults == nullptr || labelModel == nullptr || comboModel == nullptr || checkModel == nullptr || title == nullptr )
	{
		DEBUG_LOG(( "OptionsMenuTabs: the window file lacks a group or model window, no tabs\n" ));
		return FALSE;
	}

	Layout layout;
	layout.frame = frame;
	layout.labelModel = labelModel;
	layout.comboModel = comboModel;
	layout.checkModel = checkModel;
	layout.headerModel = headerModel ? headerModel : labelModel;
	layout.noteModel = labelModel;
	if( GameWindow *version = findWindow( menuParent, "OptionsMenu.wnd:LabelVersion" ) )
		layout.noteModel = version;

	Int w;
	labelModel->winGetSize( &w, &layout.labelHeight );
	comboModel->winGetSize( &w, &layout.comboHeight );
	checkModel->winGetSize( &w, &layout.checkHeight );
	layout.headerModel->winGetSize( &w, &layout.headerHeight );
	layout.gap = layout.labelHeight / 6 > 2 ? layout.labelHeight / 6 : 2;
	s_levelFace[static_cast<Int>( FontLevel::Title )] = accept->winGetFont();
	for( FontLevel level : { FontLevel::Section, FontLevel::Text, FontLevel::Hint } )
		s_levelFace[static_cast<Int>( level )] = labelModel->winGetFont();

	Int videoX, videoY, videoW, videoH, audioX, audioY, audioW, audioH, defaultsX, defaultsY, x, y;
	framePosition( video, frame, &videoX, &videoY );
	video->winGetSize( &videoW, &videoH );
	framePosition( audio, frame, &audioX, &audioY );
	audio->winGetSize( &audioW, &audioH );
	framePosition( defaults, frame, &defaultsX, &defaultsY );
	framePosition( labelModel, frame, &x, &y );
	layout.labelIndent = x - videoX;
	framePosition( comboModel, frame, &x, &y );
	layout.comboIndent = x - videoX;
	layout.top = videoY;
	layout.bottom = defaultsY - layout.gap;
	const Int left = videoX;
	const Int right = audioX + audioW;

	// ---- tab buttons in place of the title
	{
		Int titleX, titleY, titleW, titleH, buttonW, buttonH;
		framePosition( title, frame, &titleX, &titleY );
		title->winGetSize( &titleW, &titleH );
		accept->winGetSize( &buttonW, &buttonH );
		const Int tabWidth = ( right - left - ( PAGE_TAB_COUNT - 1 ) * layout.gap ) / PAGE_TAB_COUNT;
		const Int tabY = titleH > buttonH ? titleY + ( titleH - buttonH ) / 2 : titleY;
		for( Int page = 0; page < PAGE_TAB_COUNT; ++page )
		{
			s_tabs[page] = GadgetClonePushButton( accept, frame, TAB_NAMES[page], left + page * ( tabWidth + layout.gap ), tabY,
																						tabWidth, buttonH, TAB_LABELS[page] );
			if( s_tabs[page] == nullptr )
			{
				release();
				return FALSE;
			}
			GadgetButtonEnableCheckLike( s_tabs[page], TRUE, page == PAGE_DISPLAY );
			// six tabs share the width of one row: they shrink together
			setLevel( GROUP_TABS, s_tabs[page], FontLevel::Title );
			// The button of the file has no images for the selected state, and the three-part drawing draws
			// nothing at all (not even the text) when one is missing: the selected state takes the hilite images.
			GameWindow *tab = s_tabs[page];
			if( !GadgetButtonGetLeftEnabledSelectedImage( tab ) || !GadgetButtonGetMiddleEnabledSelectedImage( tab )
					|| !GadgetButtonGetRightEnabledSelectedImage( tab ) )
			{
				const Bool hilite = GadgetButtonGetLeftHiliteImage( tab ) && GadgetButtonGetMiddleHiliteImage( tab )
					&& GadgetButtonGetRightHiliteImage( tab );
				GadgetButtonSetLeftEnabledSelectedImage( tab, hilite ? GadgetButtonGetLeftHiliteImage( tab ) : GadgetButtonGetLeftEnabledImage( tab ) );
				GadgetButtonSetMiddleEnabledSelectedImage( tab, hilite ? GadgetButtonGetMiddleHiliteImage( tab ) : GadgetButtonGetMiddleEnabledImage( tab ) );
				GadgetButtonSetRightEnabledSelectedImage( tab, hilite ? GadgetButtonGetRightHiliteImage( tab ) : GadgetButtonGetRightEnabledImage( tab ) );
			}
			if( !GadgetButtonGetLeftHiliteSelectedImage( tab ) || !GadgetButtonGetMiddleHiliteSelectedImage( tab )
					|| !GadgetButtonGetRightHiliteSelectedImage( tab ) )
			{
				GadgetButtonSetLeftHiliteSelectedImage( tab, GadgetButtonGetLeftEnabledSelectedImage( tab ) );
				GadgetButtonSetMiddleHiliteSelectedImage( tab, GadgetButtonGetMiddleEnabledSelectedImage( tab ) );
				GadgetButtonSetRightHiliteSelectedImage( tab, GadgetButtonGetRightEnabledSelectedImage( tab ) );
			}
			s_tabBaseDraw = tab->winGetDrawFunc();
			tab->winSetDrawFunc( drawTab );
		}
		// the buttons below the pages share the font of the tabs, so that all buttons of the menu look alike
		setLevel( GROUP_TABS, defaults, FontLevel::Title );
		setLevel( GROUP_TABS, accept, FontLevel::Title );
		setLevel( GROUP_TABS, findWindow( menuParent, "OptionsMenu.wnd:ButtonBack" ), FontLevel::Title );
		s_accent = layout.headerModel->winGetEnabledTextColor();
		title->winHide( TRUE );
	}
	// separates the control and network groups of the file, crosses the pages here
	if( GameWindow *line = findWindow( menuParent, "OptionsMenu.wnd:Line4" ) )
		line->winHide( TRUE );

	// ---- display: two columns in sections, every combo box in a row; the group of the file is taken apart
	{
		// longer values than on the effect page ("Sharpened (CAS)"): label and box share the row equally
		constexpr Int DISPLAY_LABEL_SHARE = 10;
		// short labels, long values ("1920 x 1080 (16:9)")
		constexpr Int SHORT_LABEL_SHARE = 8;
		video->winHide( TRUE );
		if( GameWindow *line = findWindow( menuParent, "OptionsMenu.wnd:Line1" ) )
			line->winHide( TRUE );
		Column screen = { left, videoW, layout.top, PAGE_DISPLAY };
		addHeader( layout, screen, "GZH:OptionsHeaderScreen", "OptionsMenu.wnd:HeaderScreen" );
		addExistingRowCombo( layout, screen, takeOut( findWindow( menuParent, "OptionsMenu.wnd:ResolutionLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:ComboBoxResolution" ), frame ), SHORT_LABEL_SHARE );
		addExistingRowCombo( layout, screen, takeOut( findWindow( menuParent, "OptionsMenu.wnd:DetailLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:ComboBoxDetail" ), frame ), SHORT_LABEL_SHARE );
		addExistingSlider( layout, screen, takeOut( findWindow( menuParent, "OptionsMenu.wnd:GammaLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:SliderGamma" ), frame ) );
		addCheck( layout, screen, VSYNC_SETTING );
		addRowCombo( layout, screen, FPS_LIMIT_SETTING, DISPLAY_LABEL_SHARE );

		screen.y += layout.gap;
		addHeader( layout, screen, "GZH:OptionsHeaderQuality", "OptionsMenu.wnd:HeaderQuality" );
		// anti-aliasing: in the file, but hidden below the gamma slider
		if( GameWindow *antiAliasing = takeOut( findWindow( menuParent, "OptionsMenu.wnd:ComboBoxAntiAliasing" ), frame ) )
		{
			antiAliasing->winSetOwner( frame );
			GameWindow *label = GadgetCloneStaticText( layout.labelModel, frame, "OptionsMenu.wnd:LabelAntiAliasing", 0, 0,
																								 1, 1, nullptr );
			if( label )
				GadgetStaticTextSetText( label, labelText( "GUI:AntiAliasing", needsRestart( "Display.AntiAliasing" ) ) );
			addExistingRowCombo( layout, screen, label, antiAliasing, DISPLAY_LABEL_SHARE );
		}
		addRowCombo( layout, screen, TEXTURE_QUALITY_SETTING, DISPLAY_LABEL_SHARE );
		addRowCombo( layout, screen, WATER_QUALITY_SETTING, DISPLAY_LABEL_SHARE );
		addRowCombo( layout, screen, ANISOTROPY_SETTING, DISPLAY_LABEL_SHARE );
		checkFits( layout, screen );

		Column scaling = { audioX, audioW, layout.top, PAGE_DISPLAY };
		addHeader( layout, scaling, "GZH:OptionsHeaderScaling", "OptionsMenu.wnd:HeaderScaling" );
		addRowCombo( layout, scaling, RENDER_SCALE_SETTING, DISPLAY_LABEL_SHARE );
		addRowCombo( layout, scaling, RENDER_SCALE_FILTER_SETTING, DISPLAY_LABEL_SHARE );
		scaling.y += layout.gap;
		addHeader( layout, scaling, "GZH:OptionsHeaderContent", "OptionsMenu.wnd:HeaderContent" );
		addCheck( layout, scaling, FREE_CONTENT_SETTING );
		addCheck( layout, scaling, WATER_TEXTURE_SETTING );
		// Only with a choice: a program built with one renderer has nothing to switch.
		if( TheDisplay == nullptr || TheDisplay->offersRendererChoice() )
			addCheck( layout, scaling, RENDER_BACKEND_SETTING );
		checkFits( layout, scaling );
		addNote( layout, PAGE_DISPLAY, left, right - left, "OptionsMenu.wnd:NoteDisplay" );
	}

	// ---- details: the panel of the file, without its own accept and back buttons
	{
		Int advancedX, advancedY, advancedW, advancedH;
		framePosition( advanced, frame, &advancedX, &advancedY );
		advanced->winGetSize( &advancedW, &advancedH );
		if( advancedY + advancedH > layout.bottom )
			advanced->winSetSize( advancedW, layout.bottom - advancedY );
		if( GameWindow *button = findWindow( menuParent, "OptionsMenu.wnd:ButtonAdvanceAccept" ) )
			button->winHide( TRUE );
		if( GameWindow *button = findWindow( menuParent, "OptionsMenu.wnd:ButtonAdvanceBack" ) )
			button->winHide( TRUE );
		addToPage( PAGE_DETAILS, advanced );
		setGroupLevels( PAGE_DETAILS, advanced );
	}

	// ---- sound: one section at the top left; the group of the file is taken apart, two of its sliders sit next
	// to it in the file
	{
		audio->winHide( TRUE );
		if( GameWindow *line = findWindow( menuParent, "OptionsMenu.wnd:Line2" ) )
			line->winHide( TRUE );
		Column volume = { left, videoW, layout.top, PAGE_SOUND };
		addHeader( layout, volume, "GZH:OptionsHeaderVolume", "OptionsMenu.wnd:HeaderVolume" );
		addExistingSlider( layout, volume, takeOut( findWindow( menuParent, "OptionsMenu.wnd:MusicVolumeLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:SliderMusicVolume" ), frame ) );
		addExistingSlider( layout, volume, takeOut( findWindow( menuParent, "OptionsMenu.wnd:SFXVolumeLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:SliderSFXVolume" ), frame ) );
		addExistingSlider( layout, volume, takeOut( findWindow( menuParent, "OptionsMenu.wnd:VoiceVolumeLabel" ), frame ),
			takeOut( findWindow( menuParent, "OptionsMenu.wnd:SliderVoiceVolume" ), frame ) );
		checkFits( layout, volume );
	}

	// ---- controls: one column in three sections; the group of the file is taken apart, its box stays hidden
	{
		scroll->winHide( TRUE );
		if( GameWindow *line = findWindow( menuParent, "OptionsMenu.wnd:Line3" ) )
			line->winHide( TRUE );
		Column controls = { left, right - left, layout.top, PAGE_CONTROLS };
		addHeader( layout, controls, "GZH:OptionsHeaderCommands", "OptionsMenu.wnd:HeaderCommands" );
		addExistingCheck( layout, controls, takeOut( findWindow( scroll, "OptionsMenu.wnd:CheckAlternateMouse" ), frame ) );
		addExistingCheck( layout, controls, takeOut( findWindow( scroll, "OptionsMenu.wnd:Retaliation" ), frame ) );
		addExistingCheck( layout, controls, takeOut( findWindow( scroll, "OptionsMenu.wnd:CheckDoubleClickAttackMove" ), frame ) );

		controls.y += layout.gap;		// sections are set apart by twice the gap
		addHeader( layout, controls, "GZH:OptionsHeaderCamera", "OptionsMenu.wnd:HeaderCamera" );
		addExistingSlider( layout, controls, takeOut( findByText( scroll, "GUI:ScrollSpeed" ), frame ),
			takeOut( findWindow( scroll, "OptionsMenu.wnd:SliderScrollSpeed" ), frame ) );
		addCheck( layout, controls, EDGE_SCROLL_SETTING );
		addExistingCheck( layout, controls, findWindow( menuParent, "OptionsMenu.wnd:CheckBoxDrawAnchor" ) );
		addExistingCheck( layout, controls, findWindow( menuParent, "OptionsMenu.wnd:CheckBoxMoveAnchor" ) );
		addRowCombo( layout, controls, MAX_ZOOM_SETTING );

		controls.y += layout.gap;
		addHeader( layout, controls, "GZH:OptionsHeaderPointer", "OptionsMenu.wnd:HeaderPointer" );
		addRowCombo( layout, controls, CURSOR_SCALE_SETTING );
		addCheck( layout, controls, HIDE_CURSOR_SETTING );
		addCheck( layout, controls, CONFINE_CURSOR_SETTING );
		checkFits( layout, controls );
		addNote( layout, PAGE_CONTROLS, left, right - left, "OptionsMenu.wnd:NoteControls" );
	}

	// ---- game: replays and multiplayer on the left, system on the right
	{
		Column game = { left, videoW, layout.top, PAGE_GAME };
		addHeader( layout, game, "GZH:OptionsHeaderReplays", "OptionsMenu.wnd:HeaderReplays" );
		addExistingCheck( layout, game, findWindow( menuParent, "OptionsMenu.wnd:CheckBoxSaveCamera" ),
			"GZH:OptionsSaveCamera" );
		addExistingCheck( layout, game, findWindow( menuParent, "OptionsMenu.wnd:CheckBoxUseCamera" ),
			"GZH:OptionsUseCamera" );
		game.y += layout.gap;
		addHeader( layout, game, "GZH:OptionsHeaderMultiplayer", "OptionsMenu.wnd:HeaderMultiplayer" );
		addCheck( layout, game, ALLOW_SPECTATORS_SETTING );
		// long labels, short values
		addRowCombo( layout, game, SPECTATOR_DELAY_SETTING, 13 );
		addRowCombo( layout, game, RETURN_WINDOW_SETTING, 13 );
		checkFits( layout, game );

		Column system = { audioX, audioW, layout.top, PAGE_GAME };
		addHeader( layout, system, "GZH:OptionsHeaderSystem", "OptionsMenu.wnd:HeaderSystem" );
		addRowCombo( layout, system, WORKER_THREADS_SETTING, 10 );
		addCheck( layout, system, PREFETCH_SETTING );
		addCheck( layout, system, HARDWARE_HINT_SETTING );
		addCheck( layout, system, TEST_AIDS_SETTING );
		// the mods: a page of its own
		{
			Int buttonW, buttonH;
			defaults->winGetSize( &buttonW, &buttonH );
			system.y += layout.gap;
			addHeader( layout, system, "GZH:OptionsHeaderMods", "OptionsMenu.wnd:HeaderModsGame" );
			s_modsButton = GadgetClonePushButton( defaults, frame, "OptionsMenu.wnd:ButtonMods", audioX + layout.comboIndent,
				system.y, audioW - 2 * layout.comboIndent, buttonH, "GZH:OptionsModsButton" );
			addToPage( PAGE_GAME, s_modsButton, FontLevel::Title );
			system.y += buttonH + layout.gap;
			// how many mods there are and how many are on; filled once the mods page knows them
			s_modsSummary = GadgetCloneStaticText( layout.labelModel, frame, "OptionsMenu.wnd:LabelModsSummary",
				audioX + layout.labelIndent, system.y, audioW - layout.labelIndent, layout.labelHeight, nullptr );
			addToPage( PAGE_GAME, s_modsSummary, FontLevel::Text );
			system.y += layout.labelHeight + layout.gap;
			// about the project: version, source, licence, trademark notice
			addHeader( layout, system, "GZH:OptionsHeaderAbout", "OptionsMenu.wnd:HeaderAbout" );
			s_aboutButton = GadgetClonePushButton( defaults, frame, "OptionsMenu.wnd:ButtonAbout", audioX + layout.comboIndent,
				system.y, audioW - 2 * layout.comboIndent, buttonH, "GZH:OptionsAboutButton" );
			addToPage( PAGE_GAME, s_aboutButton, FontLevel::Title );
			system.y += buttonH + layout.gap;
		}
		checkFits( layout, system );
		addNote( layout, PAGE_GAME, left, videoW, "OptionsMenu.wnd:NoteGame" );
	}

	// ---- effects: Direct3D 11 effects as exceptions to the detail level
	{
		const Int firstEffect = s_controlCount;
		Column image = { left, videoW, layout.top, PAGE_EFFECTS };
		addHeader( layout, image, "GZH:OptionsHeaderEffectsImage", "OptionsMenu.wnd:HeaderEffectsImage" );
		for( const Setting &setting : EFFECTS_IMAGE )
			addRowCombo( layout, image, setting );
		Column world = { audioX, audioW, layout.top, PAGE_EFFECTS };
		addHeader( layout, world, "GZH:OptionsHeaderEffectsWorld", "OptionsMenu.wnd:HeaderEffectsWorld" );
		for( const Setting &setting : EFFECTS_WORLD )
			addRowCombo( layout, world, setting );

		// an old list in Settings.ini still applies under the exceptions; the button removes it
		if( UserSettings::isSetInFile( "Detail.D3D11Effects" ) )
		{
			Int buttonW, buttonH;
			defaults->winGetSize( &buttonW, &buttonH );
			s_removeListButton = GadgetClonePushButton( defaults, frame, "OptionsMenu.wnd:ButtonRemoveEffectList",
																									 left + layout.labelIndent, image.y + layout.gap, videoW - 2 * layout.labelIndent,
																									 buttonH, "GZH:OptionsEffectsRemoveList" );
			addToPage( PAGE_EFFECTS, s_removeListButton, FontLevel::Title );
			image.y += buttonH + 2 * layout.gap;
		}
		checkFits( layout, image );
		checkFits( layout, world );
		addNote( layout, PAGE_EFFECTS, left, right - left, "OptionsMenu.wnd:NoteEffects", "GZH:OptionsNextMapNote" );

		// -d3d11Effects decides for this run
		if( UserSettings::isForced( "Detail.D3D11Effects" ) )
		{
			for( Int i = firstEffect; i < s_controlCount; ++i )
				s_controls[i].window->winEnable( FALSE );
			if( s_removeListButton )
				s_removeListButton->winEnable( FALSE );
		}
	}

	// ---- mods: reached from the game tab, the whole width
	{
		const OptionsMenuMods::Geometry geometry = { frame, left, right, layout.top, layout.bottom, layout.gap,
			layout.comboHeight, layout.headerHeight, layout.checkModel, layout.labelModel, layout.headerModel, defaults };
		OptionsMenuMods::build( geometry, []( GameWindow *window, FontLevel level ) { addToPage( PAGE_MODS, window, level ); } );
	}

	// ---- about: reached from the game tab, the whole width
	{
		Int buttonW, buttonH;
		defaults->winGetSize( &buttonW, &buttonH );
		Int aboutY = layout.top;
		UnicodeString aboutTitle;
		aboutTitle.format( L"GZH Neo %ls \x2013 %ls", TheVersion ? TheVersion->getUnicodeVersion().str() : L"",
			isGameEditionZeroHour() ? L"Zero Hour" : L"Generals" );
		GameWindow *header = GadgetCloneStaticText( layout.headerModel, frame, "OptionsMenu.wnd:HeaderAboutPage", left, aboutY,
			right - left, layout.headerHeight, nullptr );
		GadgetStaticTextSetText( header, aboutTitle );
		addToPage( PAGE_ABOUT, header, FontLevel::Section );
		aboutY += layout.headerHeight + 2 * layout.gap;
		const Int backY = layout.bottom - buttonH;
		GameWindow *text = GadgetCloneStaticText( layout.labelModel, frame, "OptionsMenu.wnd:TextAbout", left + layout.labelIndent,
			aboutY, right - left - 2 * layout.labelIndent, backY - 2 * layout.gap - aboutY, "GZH:AboutText" );
		addToPage( PAGE_ABOUT, text, FontLevel::Text );
		s_aboutBack = GadgetClonePushButton( defaults, frame, "OptionsMenu.wnd:ButtonAboutBack", right - buttonW, backY,
			buttonW, buttonH, "GZH:ModsBack" );
		addToPage( PAGE_ABOUT, s_aboutBack, FontLevel::Title );
	}

	// the tab row first: it sets the largest size of the buttons on the pages
	applyLevels( GROUP_TABS );
	for( Int group = 0; group < PAGE_COUNT; ++group )
		applyLevels( group );
	s_built = TRUE;
	showPage( PAGE_DISPLAY );
	return TRUE;
}

void fitPage( Int page )
{
	if( s_built && page >= 0 && page < PAGE_COUNT )
		applyLevels( page );
}

Bool isBuilt()
{
	return s_built;
}

void showPage( Int page )
{
	if( !s_built || page < 0 || page >= PAGE_COUNT )
		return;
	s_page = page;
	for( Int p = 0; p < PAGE_COUNT; ++p )
		for( Int i = 0; i < s_pageWindowCount[p]; ++i )
			s_pageWindows[p][i]->winHide( TRUE );
	for( Int i = 0; i < s_pageWindowCount[page]; ++i )
		s_pageWindows[page][i]->winHide( FALSE );
	if( page == PAGE_MODS )
		OptionsMenuMods::refresh();
	// the mods page may have switched mods on or off
	if( page == PAGE_GAME && s_modsSummary )
		GadgetStaticTextSetText( s_modsSummary, OptionsMenuMods::summaryText() );
	// a check-like button toggles itself on a click; the current page decides
	for( Int p = 0; p < PAGE_TAB_COUNT; ++p )
		GadgetCheckLikeButtonSetVisualCheck( s_tabs[p], p == currentTab() );
}

Bool handleButton( GameWindow *control )
{
	if( !s_built || control == nullptr )
		return FALSE;

	for( Int page = 0; page < PAGE_TAB_COUNT; ++page )
	{
		if( control == s_tabs[page] )
		{
			showPage( page );
			return TRUE;
		}
	}
	if( control == s_modsButton )
	{
		showPage( PAGE_MODS );
		return TRUE;
	}
	if( control == s_aboutButton )
	{
		showPage( PAGE_ABOUT );
		return TRUE;
	}
	if( control == s_aboutBack )
	{
		showPage( PAGE_GAME );
		return TRUE;
	}
	switch( OptionsMenuMods::handleButton( control ) )
	{
		case OptionsMenuMods::Handled::Back:
			showPage( PAGE_GAME );
			return TRUE;
		case OptionsMenuMods::Handled::Yes:
			return TRUE;
		case OptionsMenuMods::Handled::No:
			break;
	}
	if( control == s_removeListButton )
	{
		s_removeList = TRUE;
		s_removeListButton->winEnable( FALSE );
		return TRUE;
	}
	return FALSE;
}

void stageChanges( StageFunc stage )
{
	OptionsMenuMods::stageChanges( stage );
	if( s_removeList && !UserSettings::isForced( "Detail.D3D11Effects" ) )
		stage( "Detail.D3D11Effects", AsciiString::TheEmptyString, TRUE );
	for( Int i = 0; i < s_controlCount; ++i )
	{
		const Control &control = s_controls[i];
		const Setting &setting = *control.setting;
		if( UserSettings::isForced( setting.id ) )
			continue;
		if( setting.kind == Kind::Check )
		{
			const Int on = GadgetCheckBoxIsChecked( control.window ) ? 1 : 0;
			if( on != control.initial )
				stage( setting.id, AsciiString( on ? setting.on : setting.off ), FALSE );
			continue;
		}
		Int pos = -1;
		GadgetComboBoxGetSelectedPos( control.window, &pos );
		if( pos < 0 || pos == control.initial )
			continue;
		const Int choice = static_cast<Int>( reinterpret_cast<intptr_t>( GadgetComboBoxGetItemData( control.window, pos ) ) );
		if( choice < 0 || choice >= setting.choiceCount )
			continue;
		const char *value = setting.choices[choice].value;
		if( value )
			stage( setting.id, AsciiString( value ), FALSE );
		else
			stage( setting.id, AsciiString::TheEmptyString, TRUE );
	}
}

void applySaved()
{
	// read into GlobalData at start; the others are read where they are used
	TheWritableGlobalData->m_edgeScrollInWindow = UserSettings::get( "Controls.EdgeScrollInWindow" ).compareNoCase( "yes" ) == 0;
	// the effects take effect at once: the display passes changed values to the render device
	if( TheGameLODManager )
		TheGameLODManager->applyRenderEffects( TheGameLODManager->getStaticLODLevel() );
}

void setDefaults()
{
	for( Int i = 0; i < s_controlCount; ++i )
	{
		const Control &control = s_controls[i];
		const Setting &setting = *control.setting;
		if( UserSettings::isForced( setting.id ) )
			continue;
		if( setting.kind == Kind::Check )
		{
			const Engine::Settings::Definition *definition = Engine::Settings::findDefinition( setting.id );
			const Bool on = definition && definition->defaultValue && sameValue( definition->defaultValue, setting.on );
			GadgetCheckBoxSetChecked( control.window, on );
		}
		else
		{
			const Int choice = defaultChoice( setting );
			if( choice >= 0 )
				selectChoice( control.window, choice );
		}
	}
}

void release()
{
	s_built = FALSE;
	s_page = PAGE_DISPLAY;
	s_controlCount = 0;
	s_removeListButton = nullptr;
	s_removeList = FALSE;
	s_modsButton = nullptr;
	s_modsSummary = nullptr;
	s_aboutButton = nullptr;
	s_aboutBack = nullptr;
	for( Int page = 0; page < PAGE_COUNT; ++page )
		s_pageWindowCount[page] = 0;
	for( Int group = 0; group < GROUP_COUNT; ++group )
		s_levelWindowCount[group] = 0;
	s_titlePoints = 0;
	for( Int page = 0; page < PAGE_TAB_COUNT; ++page )
		s_tabs[page] = nullptr;
	OptionsMenuMods::release();
}

}	// namespace OptionsMenuTabs
