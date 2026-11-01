/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// OptionsMenuMods.cpp -- the mods page of the options menu, see OptionsMenuMods.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/ArchiveFileSystem.h"
#include "Common/GameEdition.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/UserSettings.h"
#include "Engine/ModCatalog.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/GadgetClone.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GameText.h"
#include "GameClient/GameWindow.h"
#include "GameClient/MessageBox.h"
#include "GameClient/OptionsMenuMods.h"
#include "GameClient/WindowLayout.h"

#include <shellapi.h>

#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace OptionsMenuMods
{

namespace
{

struct Entry
{
	AsciiString name;			///< as Settings.ini lists it
	UnicodeString label;		///< title and version
	UnicodeString state;		///< empty when all is well
	UnicodeString description;
	Bool active;
	Bool usable;				///< can be switched on (not made for the other game, not locked by -mod)
};

constexpr Int MAX_ROWS = 9;

// On the heap and freed in release: a static vector would keep its debug-STL container proxy until after
// the leak report.
std::unique_ptr<std::vector<Entry>> s_entries;
AsciiString s_initialList;
Bool s_forced = FALSE;
Int s_rows = 0;
Int s_first = 0;		///< entry in the top row
Int s_current = -1;		///< entry whose description is shown
Bool s_refreshing = FALSE;	///< setting a check box sends a selection message of its own
Int s_listTop = 0;		///< y of the first row in the frame
Int s_rowStep = 0;		///< from one row to the next

GameWindow *s_check[MAX_ROWS];
GameWindow *s_state[MAX_ROWS];
GameWindow *s_up[MAX_ROWS];
GameWindow *s_down[MAX_ROWS];
GameWindow *s_previous = nullptr;
GameWindow *s_next = nullptr;
GameWindow *s_description = nullptr;
GameWindow *s_folder = nullptr;
GameWindow *s_back = nullptr;

// The texts carry a count as %d; the format check needs a fixed format, so it is put in by hand.
UnicodeString withCount( const UnicodeString &text, Int count )
{
	std::wstring result;
	for( const WideChar *p = text.str(); *p; )
	{
		if( p[0] == L'%' && p[1] == L'd' )
		{
			UnicodeString number;
			number.format( L"%d", count );
			result += number.str();
			p += 2;
		}
		else
			result += *p++;
	}
	return UnicodeString( result.c_str() );
}

UnicodeString withName( const UnicodeString &text, const UnicodeString &name )
{
	std::wstring result;
	for( const WideChar *p = text.str(); *p; )
	{
		if( p[0] == L'%' && p[1] == L's' )
		{
			result += name.str();
			p += 2;
		}
		else
			result += *p++;
	}
	return UnicodeString( result.c_str() );
}

AsciiString modsFolder()
{
	AsciiString folder;
	folder.format( "%sMods\\", TheGlobalData->getPath_UserData().str() );
	return folder;
}

// What went wrong with this mod at the start of this run, if anything.
UnicodeString problemText( const AsciiString &name )
{
	static const char *const STATE_KEYS[] = { "GZH:ModsStateMissing", "GZH:ModsStateOtherGame", "GZH:ModsStateUnreadable",
		"GZH:ModsStatePartial", "GZH:ModsStatePackage", "GZH:ModsStateManifest" };
	if( TheArchiveFileSystem == nullptr )
		return UnicodeString::TheEmptyString;
	for( const ArchiveFileSystem::ModProblem &problem : TheArchiveFileSystem->getModProblems() )
		if( problem.mod.compareNoCase( name ) == 0 )
			return withCount( TheGameText->fetch( STATE_KEYS[static_cast<Int>( problem.kind )] ), problem.count );
	return UnicodeString::TheEmptyString;
}

Entry entryFor( const AsciiString &name, const Engine::ModInfo *info )
{
	Entry entry;
	entry.name = name;
	entry.active = FALSE;
	entry.usable = TRUE;
	if( info == nullptr )
	{
		entry.label.translate( name );
		entry.state = TheGameText->fetch( "GZH:ModsStateMissing" );
		return entry;
	}
	std::string label = info->title;
	if( !info->version.empty() )
		label += " " + info->version;
	entry.label.translate( AsciiString( label.c_str() ) );
	const Engine::ModGame game = isGameEditionZeroHour() ? Engine::ModGame::ZeroHour : Engine::ModGame::Generals;
	if( info->game != Engine::ModGame::Any && info->game != game )
	{
		entry.usable = FALSE;
		entry.state = TheGameText->fetch( "GZH:ModsStateOtherGame" );
	}
	else
	{
		entry.state = problemText( name );
		if( entry.state.isEmpty() && !info->manifestError.empty() )
			entry.state = TheGameText->fetch( "GZH:ModsStateManifest" );
	}
	if( !info->description.empty() )
		entry.description.translate( AsciiString( info->description.c_str() ) );
	if( !info->author.empty() )
	{
		UnicodeString author;
		author.translate( AsciiString( info->author.c_str() ) );
		if( !entry.description.isEmpty() )
			entry.description.concat( L"\n" );
		entry.description.concat( withName( TheGameText->fetch( "GZH:ModsAuthor" ), author ) );
	}
	return entry;
}

AsciiString activeList()
{
	std::vector<std::string> names;
	for( const Entry &entry : *s_entries )
		if( entry.active )
			names.push_back( entry.name.str() );
	return AsciiString( Engine::joinModList( names ).c_str() );
}

// Active mods first in their order, then the others by name.
void collectEntries()
{
	s_entries = std::make_unique<std::vector<Entry>>();
	s_forced = UserSettings::isForced( "Mods.Active" );
	const std::vector<std::string> names = Engine::splitModList( UserSettings::get( "Mods.Active" ).str() );
	if( s_forced )
	{
		// -mod names paths; they are shown as they are, locked
		for( const std::string &path : names )
		{
			std::string name = path;
			while( !name.empty() && ( name.back() == '\\' || name.back() == '/' ) )
				name.pop_back();
			const std::size_t slash = name.find_last_of( "\\/" );
			Entry entry = entryFor( AsciiString( name.c_str() + ( slash == std::string::npos ? 0 : slash + 1 ) ), nullptr );
			entry.state = problemText( entry.name );
			entry.active = TRUE;
			entry.usable = FALSE;
			s_entries->push_back( entry );
		}
		s_initialList = UserSettings::get( "Mods.Active" );
		return;
	}
	const std::vector<Engine::ModInfo> catalog = Engine::scanModFolder( modsFolder().str() );
	const std::vector<Engine::ActiveMod> active = Engine::resolveModList( catalog, names, Engine::ModGame::Any );
	for( const Engine::ActiveMod &mod : active )
	{
		Entry entry = entryFor( AsciiString( mod.name.c_str() ), mod.info );
		entry.active = entry.usable;
		s_entries->push_back( entry );
	}
	for( const Engine::ModInfo &info : catalog )
	{
		Bool listed = FALSE;
		for( const Engine::ActiveMod &mod : active )
			if( mod.info == &info )
				listed = TRUE;
		if( !listed )
			s_entries->push_back( entryFor( AsciiString( info.name.c_str() ), &info ) );
	}
	// compared on "Accept" with what the page showed at first, so a listed mod that cannot be switched on
	// (other game) does not count as a change
	s_initialList = activeList();
}

Int entryCount()
{
	return s_entries ? static_cast<Int>( s_entries->size() ) : 0;
}

// The check boxes toggle themselves; the entries follow them before anything moves.
void syncChecks()
{
	for( Int row = 0; row < s_rows; ++row )
	{
		const Int index = s_first + row;
		if( index < entryCount() && s_check[row] && ( *s_entries )[index].usable && !s_forced )
			( *s_entries )[index].active = GadgetCheckBoxIsChecked( s_check[row] );
	}
}


void showRow( Int row, Bool show )
{
	GameWindow *windows[] = { s_check[row], s_state[row], s_up[row], s_down[row] };
	for( GameWindow *window : windows )
		if( window )
			window->winHide( !show );
}

void move( Int from, Int to )
{
	if( from < 0 || to < 0 || from >= entryCount() || to >= entryCount() )
		return;
	std::swap( ( *s_entries )[from], ( *s_entries )[to] );
	s_current = to;
	if( s_current < s_first )
		s_first = s_current;
	else if( s_current >= s_first + s_rows )
		s_first = s_current - s_rows + 1;
}

void openFolder()
{
	const AsciiString folder = modsFolder();
	::CreateDirectoryA( folder.str(), nullptr );
	::ShellExecuteA( nullptr, "open", folder.str(), nullptr, nullptr, SW_SHOWNORMAL );
}

}	// namespace

void build( const Geometry &g, AddWindow add )
{
	release();
	collectEntries();

	const Int width = g.right - g.left;
	Int y = g.top;
	add( GadgetCloneStaticText( g.headerModel, g.frame, "OptionsMenu.wnd:HeaderMods", g.left, y, width, g.headerHeight,
		"GZH:ModsTitle" ), OptionsMenuTabs::FontLevel::Section );
	y += g.headerHeight + g.gap;

	// rows: check box with the title, state, up, down
	const Int buttonW = width / 9;
	const Int stateW = width * 3 / 10;
	const Int checkW = width - stateW - 2 * buttonW - 3 * g.gap;
	// paging, description (two lines), buttons with a row of space above them
	const Int belowRows = 5 * g.rowHeight + 4 * g.gap;
	s_rows = ( g.bottom - y - belowRows ) / ( g.rowHeight + g.gap );
	if( s_rows > MAX_ROWS )
		s_rows = MAX_ROWS;
	if( s_rows < 1 )
		s_rows = 1;
	s_listTop = y;
	s_rowStep = g.rowHeight + g.gap;
	for( Int row = 0; row < s_rows; ++row )
	{
		AsciiString name;
		name.format( "OptionsMenu.wnd:CheckMod%d", row );
		s_check[row] = GadgetCloneCheckBox( g.checkModel, g.frame, name.str(), g.left, y, checkW, g.rowHeight, nullptr );
		name.format( "OptionsMenu.wnd:StateMod%d", row );
		s_state[row] = GadgetCloneStaticText( g.labelModel, g.frame, name.str(), g.left + checkW + g.gap, y, stateW,
			g.rowHeight, nullptr );
		name.format( "OptionsMenu.wnd:ButtonModUp%d", row );
		s_up[row] = GadgetClonePushButton( g.buttonModel, g.frame, name.str(), g.right - 2 * buttonW - g.gap, y, buttonW,
			g.rowHeight, "GZH:ModsUp" );
		name.format( "OptionsMenu.wnd:ButtonModDown%d", row );
		s_down[row] = GadgetClonePushButton( g.buttonModel, g.frame, name.str(), g.right - buttonW, y, buttonW, g.rowHeight,
			"GZH:ModsDown" );
		add( s_check[row], OptionsMenuTabs::FontLevel::Text );
		add( s_state[row], OptionsMenuTabs::FontLevel::Text );
		add( s_up[row], OptionsMenuTabs::FontLevel::Title );
		add( s_down[row], OptionsMenuTabs::FontLevel::Title );
		y += g.rowHeight + g.gap;
	}

	// paging when there are more mods than rows
	const Int pageW = width / 4;
	s_previous = GadgetClonePushButton( g.buttonModel, g.frame, "OptionsMenu.wnd:ButtonModsPrevious", g.right - 2 * pageW - g.gap,
		y, pageW, g.rowHeight, "GZH:ModsPrevious" );
	s_next = GadgetClonePushButton( g.buttonModel, g.frame, "OptionsMenu.wnd:ButtonModsNext", g.right - pageW, y, pageW,
		g.rowHeight, "GZH:ModsNext" );
	add( s_previous, OptionsMenuTabs::FontLevel::Title );
	add( s_next, OptionsMenuTabs::FontLevel::Title );
	y += g.rowHeight + g.gap;

	s_description = GadgetCloneStaticText( g.labelModel, g.frame, "OptionsMenu.wnd:DescriptionMod", g.left, y, width,
		2 * g.rowHeight, nullptr );
	add( s_description, OptionsMenuTabs::FontLevel::Text );

	const Int folderW = width / 3;
	// a row of space to the buttons below the pages
	const Int buttonsY = g.bottom - 2 * g.rowHeight;
	s_folder = GadgetClonePushButton( g.buttonModel, g.frame, "OptionsMenu.wnd:ButtonModsFolder", g.left, buttonsY, folderW,
		g.rowHeight, "GZH:ModsOpenFolder" );
	s_back = GadgetClonePushButton( g.buttonModel, g.frame, "OptionsMenu.wnd:ButtonModsBack", g.right - folderW, buttonsY,
		folderW, g.rowHeight, "GZH:ModsBack" );
	add( s_folder, OptionsMenuTabs::FontLevel::Title );
	add( s_back, OptionsMenuTabs::FontLevel::Title );
	if( s_forced && s_folder )
		s_folder->winEnable( FALSE );
}

void refresh()
{
	if( !s_entries || s_refreshing )
		return;
	s_refreshing = TRUE;
	const Int count = entryCount();
	if( s_first > 0 && s_first >= count )
		s_first = count > s_rows ? count - s_rows : 0;
	for( Int row = 0; row < s_rows; ++row )
	{
		const Int index = s_first + row;
		if( index >= count )
		{
			showRow( row, FALSE );
			continue;
		}
		const Entry &entry = ( *s_entries )[index];
		showRow( row, TRUE );
		if( s_check[row] )
		{
			GadgetCheckBoxSetText( s_check[row], entry.label );
			GadgetCheckBoxSetChecked( s_check[row], entry.active );
			s_check[row]->winEnable( entry.usable && !s_forced );
		}
		if( s_state[row] )
		{
			GadgetStaticTextSetText( s_state[row], entry.state );
		}
		if( s_up[row] )
			s_up[row]->winEnable( !s_forced && index > 0 );
		if( s_down[row] )
			s_down[row]->winEnable( !s_forced && index + 1 < count );
	}
	// paging and the description follow the last shown row, not the end of the space for rows
	const Bool paging = count > s_rows;
	const Int shown = count - s_first < s_rows ? count - s_first : s_rows;
	Int y = s_listTop + ( shown > 0 ? shown : 0 ) * s_rowStep;
	for( GameWindow *button : { s_previous, s_next } )
	{
		if( button == nullptr )
			continue;
		Int x, oldY;
		button->winGetPosition( &x, &oldY );
		button->winSetPosition( x, y );
	}
	if( paging )
		y += s_rowStep;
	if( s_description )
	{
		Int x, oldY;
		s_description->winGetPosition( &x, &oldY );
		s_description->winSetPosition( x, y );
	}
	if( s_previous )
	{
		s_previous->winHide( !paging );
		s_previous->winEnable( s_first > 0 );
	}
	if( s_next )
	{
		s_next->winHide( !paging );
		s_next->winEnable( s_first + s_rows < count );
	}
	if( s_description )
	{
		UnicodeString text;
		if( s_forced )
			text = TheGameText->fetch( "GZH:ModsForced" );
		else if( count == 0 )
			text = TheGameText->fetch( "GZH:ModsNone" );
		else if( s_current >= 0 && s_current < count )
		{
			text = ( *s_entries )[s_current].description;
			if( text.isEmpty() )
				text = TheGameText->fetch( "GZH:ModsNoDescription" );
		}
		else
			text = TheGameText->fetch( "GZH:ModsHint" );
		GadgetStaticTextSetText( s_description, text );
	}
	OptionsMenuTabs::fitPage( OptionsMenuTabs::PAGE_MODS );
	s_refreshing = FALSE;
}

Handled handleButton( GameWindow *control )
{
	if( control == nullptr || !s_entries )
		return Handled::No;
	if( s_refreshing )
	{
		for( Int row = 0; row < s_rows; ++row )
			if( control == s_check[row] )
				return Handled::Yes;
		return Handled::No;
	}
	if( control == s_back )
		return Handled::Back;
	if( control == s_folder )
	{
		openFolder();
		return Handled::Yes;
	}
	if( control == s_previous || control == s_next )
	{
		syncChecks();
		s_first += control == s_next ? s_rows : -s_rows;
		if( s_first < 0 )
			s_first = 0;
		refresh();
		return Handled::Yes;
	}
	for( Int row = 0; row < s_rows; ++row )
	{
		const Int index = s_first + row;
		if( control == s_check[row] )
		{
			syncChecks();
			s_current = index;
			refresh();
			return Handled::Yes;
		}
		if( control == s_up[row] || control == s_down[row] )
		{
			syncChecks();
			move( index, control == s_up[row] ? index - 1 : index + 1 );
			refresh();
			return Handled::Yes;
		}
	}
	return Handled::No;
}

void stageChanges( OptionsMenuTabs::StageFunc stage )
{
	if( !s_entries || s_forced )
		return;
	syncChecks();
	const AsciiString list = activeList();
	if( list.compare( s_initialList ) == 0 )
		return;
	stage( "Mods.Active", list, list.isEmpty() );
	s_initialList = list;
}

void release()
{
	s_entries.reset();
	s_initialList.clear();
	s_forced = FALSE;
	s_rows = 0;
	s_first = 0;
	s_current = -1;
	s_listTop = s_rowStep = 0;
	for( Int row = 0; row < MAX_ROWS; ++row )
		s_check[row] = s_state[row] = s_up[row] = s_down[row] = nullptr;
	s_previous = s_next = s_description = s_folder = s_back = nullptr;
}

UnicodeString summaryText()
{
	Int on = 0;
	if( s_entries )
		for( const Entry &entry : *s_entries )
			if( entry.active )
				++on;
	UnicodeString text;
	text.format( TheGameText->fetch( "GZH:ModsSummary" ), entryCount(), on );
	return text;
}

UnicodeString listText( const AsciiString &tag )
{
	const std::string text = Engine::describeModTag( tag.str() );
	if( text.empty() )
		return TheGameText->fetch( "GZH:ModsNoneShort" );
	UnicodeString result;
	result.translate( AsciiString( text.c_str() ) );
	return result;
}

}	// namespace OptionsMenuMods
