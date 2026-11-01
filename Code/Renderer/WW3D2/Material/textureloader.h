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

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : DX8 Texture Manager                                          *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/textureloader.h                            $*
 *                                                                                             *
 *              Original Author:: vss_sync                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#ifndef TEXTURELOADER_H
#define TEXTURELOADER_H

#if defined(_MSC_VER)
#pragma once
#endif

#include "always.h"
#include "ww3dtypes.h"	// forward declarations only
#include "texture.h"

class StringClass;
class TextureLoadTaskClass;
class TextureLoadTaskListClass;

class TextureLoader
{
public:
	static void Init(void);
	static void Deinit(void);

	// Modify given texture size to nearest valid size on current hardware.
	static void Validate_Texture_Size(unsigned& width, unsigned& height, unsigned& depth);

	static WW3DSurfaceHandle 		Load_Surface_Immediate(
		const StringClass& filename,
		WW3DFormat surface_format,		// Pass WW3D_FORMAT_UNKNOWN if you don't care
		bool allow_compression);

	// Adds a loading task to the system. The task if processed in a separate
	// thread as soon as possible. The task will appear in finished tasks list
	// when it's been completed. The texture will be refreshed on the next
	// update call after appearing to the finished tasks list.
	static void Request_Background_Loading(TextureBaseClass* tc);

	// Textures can only be created and locked by the main thread so this function sends a request to the texture
	// handling system to load the texture immediatelly next time it enters the main thread. If this function
	// is called from the main thread the texture is loaded immediatelly.
	static void Request_Foreground_Loading(TextureBaseClass* tc);

	static void	Flush_Pending_Load_Tasks(void);
	static void Update(void(*network_callback)(void) = nullptr);

	// returns true if current thread of execution is allowed to make device calls.
	static bool Is_Render_Thread(void);

	static void Suspend_Texture_Load();
	static void Continue_Texture_Load();

	// Texture package: mip levels to skip so a texture loads at the size of the chosen tier. Set by
	// the game; nullptr (no package) skips nothing. Applies to every texture the function knows,
	// also to single-level and non-reducible ones; the user reduction comes on top.
	typedef unsigned (*PackageMipOffsetFunction)(const char *name, unsigned width, unsigned height, unsigned mip_count);
	static void Set_Package_Mip_Offset_Function(PackageMipOffsetFunction function);
	static unsigned Package_Mip_Offset(const char *name, unsigned width, unsigned height, unsigned mip_count);


private:
	static void Process_Foreground_Load			(TextureLoadTaskClass *task);

	static void Begin_Load_And_Queue				(TextureLoadTaskClass *task);

	static bool TextureLoadSuspended;
};

class TextureLoadTaskListNodeClass
{
	friend class TextureLoadTaskListClass;

	public:
		TextureLoadTaskListNodeClass(void) : Next(0), Prev(0) { }

		TextureLoadTaskListClass *Get_List(void)		{ return List; }

		TextureLoadTaskListNodeClass *Next;
		TextureLoadTaskListNodeClass *Prev;
		TextureLoadTaskListClass *		List;
};


class TextureLoadTaskListClass
{
	// This class implements an unsynchronized, double-linked list of TextureLoadTaskClass 
	// objects, using an embedded list node.

	public:
		TextureLoadTaskListClass(void);

		// Returns true if list is empty, false otherwise.
		bool									Is_Empty		(void) const		{ return (Root.Next == &Root); }

		// Add a task to beginning of list
		void									Push_Front	(TextureLoadTaskClass *task);

		// Add a task to end of list
		void									Push_Back	(TextureLoadTaskClass *task);

		// Remove and return a task from beginning of list, or NULL if list is empty.
		TextureLoadTaskClass *			Pop_Front	(void);

		// Remove and return a task from end of list, or NULL if list is empty
		TextureLoadTaskClass *			Pop_Back		(void);

		// Remove specified task from list, if present
		void									Remove		(TextureLoadTaskClass *task);

	private:
		// This list is implemented using a sentinel node.
		TextureLoadTaskListNodeClass	Root;
};


class SynchronizedTextureLoadTaskListClass : public TextureLoadTaskListClass
{
	// This class added thread-safety to the basic TextureLoadTaskListClass.

	public:
		SynchronizedTextureLoadTaskListClass(void);

		// See comments above for description of member functions.
		void									Push_Front	(TextureLoadTaskClass *task);
		void									Push_Back	(TextureLoadTaskClass *task);
		TextureLoadTaskClass *			Pop_Front	(void);
		TextureLoadTaskClass *			Pop_Back		(void);
		void									Remove		(TextureLoadTaskClass *task);

	private:
		FastCriticalSectionClass		CriticalSection;
};

/*
** (gth) The allocation system we're using for TextureLoadTaskClass has gotten a little
** complicated since Kenny added the new task types for Cube and Volume textures.  The
** ::Destroy member is used to return a task to the pool now and must be over-ridden in
** each derived class to put the task back into the correct free list.  
*/


// Size of the per-level lock arrays of a load task. 16 levels cover 32768x32768; Direct3D 11 allows
// at most 16384 (15 levels), hence every full mip chain fits. Lock_Surfaces also clamps to this size.
enum { TEXTURE_LOAD_MAX_LEVELS = 16 };

class TextureLoadTaskClass : public TextureLoadTaskListNodeClass
{
	public:
		enum TaskType {
			TASK_NONE,
			TASK_LOAD,
		};

		enum PriorityType {
			PRIORITY_LOW,
			PRIORITY_HIGH,
		};

		enum StateType {
			STATE_NONE,

			STATE_LOAD_BEGUN,
			STATE_LOAD_MIPMAP,
			STATE_LOAD_COMPLETE,

			STATE_COMPLETE,
		};


		TextureLoadTaskClass(void);
		virtual ~TextureLoadTaskClass(void);

		static TextureLoadTaskClass *	Create			(TextureBaseClass *tc, TaskType type, PriorityType priority);
		static void				Delete_Free_Pool			(void);

		virtual void			Destroy						(void);
		void					Abort_Load					(void);	///< discard a started load (unlock, release the D3D texture), used at shutdown
		virtual void			Init							(TextureBaseClass *tc, TaskType type, PriorityType priority);
		virtual void			Deinit						(void);

		TaskType					Get_Type						(void) const		{ return Type;				}
		PriorityType			Get_Priority				(void) const		{ return Priority;		}
		StateType				Get_State					(void) const		{ return State;			}

		WW3DFormat				Get_Format					(void) const		{ return Format;			}
		unsigned int			Get_Width					(void) const		{ return Width;			}
		unsigned int			Get_Height					(void) const		{ return Height;			}
		unsigned int			Get_Mip_Level_Count		(void) const		{ return MipLevelCount; }
		unsigned int			Get_Reduction				(void) const		{ return Reduction;		}

		unsigned char *		Get_Locked_Surface_Ptr	(unsigned int level);
		unsigned int			Get_Locked_Surface_Pitch(unsigned int level) const;

		TextureBaseClass *	Peek_Texture				(void)				{ return Texture;			}
		WW3DTextureHandle 	Peek_D3D_Texture			(void)				{ return static_cast<WW3DTextureHandle>(D3DTexture);		}

		void						Set_Type						(TaskType t)		{ Type		= t;			}
		void						Set_Priority				(PriorityType p)	{ Priority	= p;			}
		void						Set_State					(StateType s)		{ State		= s;			}

		bool						Begin_Load					(void);
		bool						Load							(void);
		void						End_Load						(void);
		void						Finish_Load					(void);
		void						Apply_Missing_Texture	(void);						

	protected:
		virtual bool			Begin_Compressed_Load	(void);
		virtual bool			Begin_Uncompressed_Load	(void);

		virtual bool			Load_Compressed_Mipmap	(void);
		virtual bool			Load_Uncompressed_Mipmap(void);

		virtual void			Lock_Surfaces				(void);
		virtual void			Unlock_Surfaces			(void);

		void						Apply							(bool initialize);
		
		TextureBaseClass*		Texture;
		WW3DTextureHandle 	D3DTexture;
		WW3DFormat				Format;

		unsigned int			Width;
		unsigned	int			Height;
		unsigned	int			MipLevelCount;
		unsigned	int			Reduction;
		Vector3					HSVShift;

		unsigned char *		LockedSurfacePtr[TEXTURE_LOAD_MAX_LEVELS];
		unsigned	int			LockedSurfacePitch[TEXTURE_LOAD_MAX_LEVELS];

		TaskType					Type;
		PriorityType			Priority;
		StateType				State;
};

#endif
