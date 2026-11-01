/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// GadgetClone.h -- gadgets created at run time that look like a gadget from a window file (font, colours,
// images). Menus use them for settings the window files of the game data do not have. Position and size
// are relative to the parent; the new gadget starts visible and enabled.

#pragma once

#include "Lib/BaseType.h"

class GameWindow;

/// Copies the enabled, disabled and highlight draw data of one window to another.
void GadgetCopyDrawData( GameWindow *from, GameWindow *to );

GameWindow *GadgetCloneCheckBox( GameWindow *model, GameWindow *parent, const char *decoratedName,
																 Int x, Int y, Int width, Int height, const char *textLabel );
GameWindow *GadgetCloneComboBox( GameWindow *model, GameWindow *parent, const char *decoratedName,
																 Int x, Int y, Int width, Int height );
GameWindow *GadgetCloneStaticText( GameWindow *model, GameWindow *parent, const char *decoratedName,
																	 Int x, Int y, Int width, Int height, const char *textLabel );
GameWindow *GadgetClonePushButton( GameWindow *model, GameWindow *parent, const char *decoratedName,
																	 Int x, Int y, Int width, Int height, const char *textLabel );
