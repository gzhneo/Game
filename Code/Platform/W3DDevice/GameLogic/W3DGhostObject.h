/*
**	Command & Conquer Generals(tm) / Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**	Modified by GZH Neo, Copyright (C) 2026 GZH Neo
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DGhostObject.h ////////////////////////////////////////////////////////////
// Placeholder for objects that have been deleted but need to be maintained because
// a player can see them fogged.
// Author: Mark Wilczynski, August 2002

#pragma once

#ifndef _W3DGHOSTOBJECT_H_
#define _W3DGHOSTOBJECT_H_

#include "GameLogic/GhostObject.h"
#include "Lib/BaseType.h"
#include "Common/GameCommon.h"
#include "GameClient/DrawableInfo.h"

class Object;
class W3DGhostObjectManager;
class W3DRenderObjectSnapshot;
class PartitionData;

class W3DGhostObject: public GhostObject
{
	friend W3DGhostObjectManager;
public:
	W3DGhostObject();
	virtual ~W3DGhostObject() override;
	virtual void snapShot(int playerIndex) override;
	virtual void updateParentObject(Object *object, PartitionData *mod) override;
	virtual void freeSnapShot(int playerIndex) override;
protected:
	virtual void crc( Xfer *xfer) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess( void ) override;
	void removeParentObject(void);
	void restoreParentObject(void);	///< restore the original non-ghosted object to scene.
	void addToScene(int playerIndex);
	void removeFromScene(int playerIndex);
	void release(void);			///< used by manager to return object to free store.
	void getShroudStatus(int playerIndex);	///< used to get the partition manager to update ghost objects without parent objects.
	void freeAllSnapShots(void);				///< used to free all snapshots from all players.
	W3DRenderObjectSnapshot *m_parentSnapshots[MAX_PLAYER_COUNT];
	DrawableInfo	m_drawableInfo;

	///@todo this list should really be part of the device independent base class (CBD 12-3-2002)
	W3DGhostObject *m_nextSystem;
	W3DGhostObject *m_prevSystem;
};

class W3DGhostObjectManager : public GhostObjectManager
{
public:
	W3DGhostObjectManager();
	virtual ~W3DGhostObjectManager() override;
	virtual void reset(void) override;
	virtual GhostObject *addGhostObject(Object *object, PartitionData *pd) override;
	virtual void removeGhostObject(GhostObject *mod) override;
	virtual void setLocalPlayerIndex(int index) override;
	virtual void updateOrphanedObjects(int *playerIndexList, int numNonLocalPlayers) override;
	virtual void releasePartitionData(void) override;
	virtual void restorePartitionData(void) override;

protected:
	virtual void crc( Xfer *xfer ) override;
	virtual void xfer( Xfer *xfer ) override;
	virtual void loadPostProcess( void ) override;

	///@todo this list should really be part of the device independent base class (CBD 12-3-2002)
	W3DGhostObject	*m_freeModules;
	W3DGhostObject	*m_usedModules;
};

#endif // _W3DGHOSTOBJECT_H_
