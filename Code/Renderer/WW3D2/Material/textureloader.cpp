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
 *								$Modtime:: 08/05/02 10:03a                                             $*
 *                                                                                             *
 *                    $Revision:: 3                                                           $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 * 08/05/02 KM Texture class redesign (revisited)
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "textureloader.h"
#include "mutex.h"
#include "thread.h"
#include "wwdebug.h"
#include "texture.h"
#include "ffactory.h"
#include "wwstring.h"
#include	"bufffile.h"
#include "ww3d.h"
#include "assetmgr.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/rendercaps.h"
#include "missingtexture.h"
#include "TARGA.H"
#include <cstdio>
#include "texture.h"
#include "formconv.h"
#include "ddsfile.h"
#include "bitmaphandler.h"
#include "wwprofile.h"

//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")

bool TextureLoader::TextureLoadSuspended;
static TextureLoader::PackageMipOffsetFunction PackageMipOffset = nullptr;

void TextureLoader::Set_Package_Mip_Offset_Function(PackageMipOffsetFunction function)
{
	PackageMipOffset = function;
}

unsigned TextureLoader::Package_Mip_Offset(const char *name, unsigned width, unsigned height, unsigned mip_count)
{
	if (PackageMipOffset == nullptr || name == nullptr || mip_count < 2)
		return 0;
	const unsigned offset = PackageMipOffset(name, width, height, mip_count);
	return offset < mip_count ? offset : mip_count - 1;
}

////////////////////////////////////////////////////////////////////////////////
// 
// TextureLoadTaskListClass implementation
// 
////////////////////////////////////////////////////////////////////////////////

TextureLoadTaskListClass::TextureLoadTaskListClass(void)
: Root()
{
	Root.Next = Root.Prev = &Root;
}

void TextureLoadTaskListClass::Push_Front	(TextureLoadTaskClass *task)
{
	// task should non-null and not on any list
	WWASSERT(task != nullptr && task->Next == nullptr && task->Prev == nullptr);

	// update inserted task to point to list
	task->Next			= Root.Next;
	task->Prev			= &Root;
	task->List			= this;

	// update list to point to inserted task
	Root.Next->Prev	= task;
	Root.Next			= task;
}

void TextureLoadTaskListClass::Push_Back(TextureLoadTaskClass *task)
{
	// task should be non-null and not on any list
	WWASSERT(task != nullptr && task->Next == nullptr && task->Prev == nullptr);

	// update inserted task to point to list
	task->Next			= &Root;
	task->Prev			= Root.Prev;
	task->List			= this;

	// update list to point to inserted task
	Root.Prev->Next	= task;
	Root.Prev			= task;
}

TextureLoadTaskClass *TextureLoadTaskListClass::Pop_Front(void)
{
	// exit early if list is empty
	if (Is_Empty()) {
		return 0;
	}

	// otherwise, grab first task and remove it.
	TextureLoadTaskClass *task = static_cast<TextureLoadTaskClass *>(Root.Next);
	Remove(task);
	return task;

}

TextureLoadTaskClass *TextureLoadTaskListClass::Pop_Back(void)
{
	// exit early if list is empty
	if (Is_Empty()) { 
		return 0;
	}

	// otherwise, grab last task and remove it.
	TextureLoadTaskClass *task = static_cast<TextureLoadTaskClass *>(Root.Prev);
	Remove(task);
	return task;
}

void TextureLoadTaskListClass::Remove(TextureLoadTaskClass *task)
{
	// exit early if task is not on this list.
	if (task->List != this) {
		return;
	}

	// update list to skip task
	task->Prev->Next = task->Next;
	task->Next->Prev = task->Prev;

	// update task to no longer point at list
	task->Prev	= 0;
	task->Next	= 0;
	task->List	= 0;
}


////////////////////////////////////////////////////////////////////////////////
// 
// SynchronizedTextureLoadTaskListClass implementation
// 
////////////////////////////////////////////////////////////////////////////////

SynchronizedTextureLoadTaskListClass::SynchronizedTextureLoadTaskListClass(void)
:	TextureLoadTaskListClass(),
	CriticalSection()
{
}

void SynchronizedTextureLoadTaskListClass::Push_Front(TextureLoadTaskClass *task)
{
	FastCriticalSectionClass::LockClass lock(CriticalSection);
	TextureLoadTaskListClass::Push_Front(task);
}

void SynchronizedTextureLoadTaskListClass::Push_Back(TextureLoadTaskClass *task)
{
	FastCriticalSectionClass::LockClass lock(CriticalSection);
	TextureLoadTaskListClass::Push_Back(task);
}

TextureLoadTaskClass *SynchronizedTextureLoadTaskListClass::Pop_Front(void)
{
	// this duplicates code inside base class, but saves us an unnecessary lock.
	if (Is_Empty()) {
		return 0;
	}

	FastCriticalSectionClass::LockClass lock(CriticalSection);
	return TextureLoadTaskListClass::Pop_Front();

}

TextureLoadTaskClass *SynchronizedTextureLoadTaskListClass::Pop_Back(void)
{
	// this duplicates code inside base class, but saves us an unnecessary lock.
	if (Is_Empty()) {
		return 0;
	}

	FastCriticalSectionClass::LockClass lock(CriticalSection);
	return TextureLoadTaskListClass::Pop_Back();
}

void SynchronizedTextureLoadTaskListClass::Remove(TextureLoadTaskClass *task)
{
	FastCriticalSectionClass::LockClass lock(CriticalSection);
	TextureLoadTaskListClass::Remove(task);
}


// Locks

// To prevent deadlock, threads should acquire locks in the order in which
// they are defined below. No ordering is necessary for the task list locks,
// since one thread can never hold two at once.

static FastCriticalSectionClass					_ForegroundCriticalSection;
static FastCriticalSectionClass					_BackgroundCriticalSection;

// Lists

static SynchronizedTextureLoadTaskListClass	_ForegroundQueue;
static SynchronizedTextureLoadTaskListClass	_BackgroundQueue;

static TextureLoadTaskListClass					_TexLoadFreeList;


// The background texture loading thread.
static class LoaderThreadClass : public ThreadClass
{
public:
#ifdef Exception_Handler
	LoaderThreadClass(const char *thread_name = "Texture loader thread") : ThreadClass(thread_name, &Exception_Handler) {}
#else
	LoaderThreadClass(const char *thread_name = "Texture loader thread") : ThreadClass(thread_name) {}
#endif

	void Thread_Function() override;
} _TextureLoadThread;


// TODO: Legacy - remove this call!
WW3DTextureHandle Load_Compressed_Texture(
	const StringClass& filename,
	unsigned reduction_factor,
	MipCountType mip_level_count,
	WW3DFormat dest_format)
{
	// If DDS file isn't available, use TGA file to convert to DDS.

	DDSFileClass dds_file(filename,reduction_factor);
	if (!dds_file.Is_Available()) return nullptr;
	if (!dds_file.Load()) return nullptr;

	unsigned width=dds_file.Get_Width(0);
	unsigned height=dds_file.Get_Height(0);
	unsigned mips=dds_file.Get_Mip_Level_Count();

	// If format isn't defined get the nearest valid texture format to the compressed file format
	// Note that the nearest valid format could be anything, even uncompressed.
	if (dest_format==WW3D_FORMAT_UNKNOWN) dest_format=Get_Valid_Texture_Format(dds_file.Get_Format(),true);

	WW3DTextureHandle d3d_texture = RenderDevice::_Create_Device_Texture
	(
		width,
		height,
		dest_format,
		static_cast<MipCountType>(mips)
	);

	for (unsigned level=0;level<mips;++level) {
		WW3DSurfaceHandle d3d_surface=nullptr;
		WWASSERT(d3d_texture);
		Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(d3d_texture, level/*-reduction_factor*/, &d3d_surface));
		dds_file.Copy_Level_To_Surface(level,d3d_surface);
		RenderDevice::Release_Resource(d3d_surface);
	}
	return d3d_texture;
}

static bool Is_Format_Compressed(WW3DFormat texture_format,bool allow_compression)
{
	// Verify that the user isn't requesting compressed texture without hardware support

	bool compressed=false;
	if (texture_format!=WW3D_FORMAT_UNKNOWN) {
		if (!RenderCaps::Support_DXTC() || !allow_compression) {
			WWASSERT(texture_format!=WW3D_FORMAT_DXT1);
			WWASSERT(texture_format!=WW3D_FORMAT_DXT2);
			WWASSERT(texture_format!=WW3D_FORMAT_DXT3);
			WWASSERT(texture_format!=WW3D_FORMAT_DXT4);
			WWASSERT(texture_format!=WW3D_FORMAT_DXT5);
			WWASSERT(texture_format!=WW3D_FORMAT_BC7);
		}
		if (texture_format==WW3D_FORMAT_DXT1 ||
			texture_format==WW3D_FORMAT_DXT2 ||
			texture_format==WW3D_FORMAT_DXT3 ||
			texture_format==WW3D_FORMAT_DXT4 ||
			texture_format==WW3D_FORMAT_DXT5 ||
			texture_format==WW3D_FORMAT_BC7) {
			compressed=true;
		}
	}

	// If hardware supports DXTC compression, load a compressed texture. Proceed only if the texture format hasn't been
	// defined as non-compressed.
	compressed|=(
		texture_format==WW3D_FORMAT_UNKNOWN &&
		RenderCaps::Support_DXTC() &&
		allow_compression);

	return compressed;
}


////////////////////////////////////////////////////////////////////////////////
// 
// TextureLoader implementation
// 
////////////////////////////////////////////////////////////////////////////////

void TextureLoader::Init()
{
	WWASSERT(!_TextureLoadThread.Is_Running());

	_TextureLoadThread.Execute();
	_TextureLoadThread.Set_Priority(-4);
}


void TextureLoader::Deinit()
{
	FastCriticalSectionClass::LockClass lock(_BackgroundCriticalSection);
	_TextureLoadThread.Stop();

	// Tasks still queued at shutdown each hold a texture reference (and maybe a half loaded D3D
	// texture); drain them before the leak report. The load thread is stopped; this runs on the
	// main thread only.
	for (;;) {
		TextureLoadTaskClass *task = _BackgroundQueue.Pop_Front();
		if (!task) task = _ForegroundQueue.Pop_Front();
		if (!task) break;
		task->Abort_Load();
		task->Destroy();
	}

	TextureLoadTaskClass::Delete_Free_Pool();
}


bool TextureLoader::Is_Render_Thread(void)
{
	return (ThreadClass::_Get_Current_Thread_ID() == RenderDevice::_Get_Main_Thread_ID());
}


// ----------------------------------------------------------------------------
//
// Modify given texture size to nearest valid size on current hardware.
//
// ----------------------------------------------------------------------------

void TextureLoader::Validate_Texture_Size
(
	unsigned& width, 
	unsigned& height,
	unsigned& depth
)
{
	const unsigned max_texture_width  = RenderCaps::Get_Max_Texture_Width();
	const unsigned max_texture_height = RenderCaps::Get_Max_Texture_Height();
	// No volume textures: depth is clamped to 0 (callers do not use it).
	const unsigned max_volume_extent  = 0;

	// Stop at the highest bit: shifting further would wrap to 0 and never end.
	unsigned poweroftwowidth = 1;
	while (poweroftwowidth < width && poweroftwowidth < 0x80000000u)
	{
		poweroftwowidth <<= 1;
	}

	unsigned poweroftwoheight = 1;
	while (poweroftwoheight < height && poweroftwoheight < 0x80000000u)
	{
		poweroftwoheight <<= 1;
	}

	unsigned poweroftwodepth = 1;
	while (poweroftwodepth< depth)
	{
		poweroftwodepth <<= 1;
	}

	if (poweroftwowidth>max_texture_width) 
	{
		poweroftwowidth=max_texture_width;
	}
	if (poweroftwoheight>max_texture_height) 
	{
		poweroftwoheight=max_texture_height;
	}
	if (poweroftwodepth>max_volume_extent)
	{
		poweroftwodepth=max_volume_extent;
	}

	if (poweroftwowidth>poweroftwoheight) 
	{
		while (poweroftwowidth/poweroftwoheight>8) 
		{
			poweroftwoheight*=2;
		}
	}
	else 
	{
		while (poweroftwoheight/poweroftwowidth>8) 
		{
			poweroftwowidth*=2;
		}
	}

	width=poweroftwowidth;
	height=poweroftwoheight;
	depth=poweroftwodepth;
}


// ----------------------------------------------------------------------------
//
// Load image to a surface. The function tries to create texture that matches
// targa format. If suitable format is not available, it selects closest matching
// format and performs color space conversion.
//
// ----------------------------------------------------------------------------
WW3DSurfaceHandle TextureLoader::Load_Surface_Immediate(
	const StringClass& filename,
	WW3DFormat texture_format,
	bool allow_compression)
{
	WWASSERT(Is_Render_Thread());

	bool compressed=Is_Format_Compressed(texture_format,allow_compression);

	if (compressed) {
		WW3DTextureHandle comp_tex=Load_Compressed_Texture(filename,0,MIP_LEVELS_1,WW3D_FORMAT_UNKNOWN);
		if (comp_tex) {
			WW3DSurfaceHandle d3d_surface=nullptr;
			Device_ErrorCode(RenderDevice::Get_Texture_Surface_Level(comp_tex, 0, &d3d_surface));
			RenderDevice::Release_Resource(comp_tex);
			return d3d_surface;
		}
	}

	// Make sure the file can be opened. If not, return missing texture.
	Targa targa;
	if (TARGA_ERROR_HANDLER(targa.Open(filename, TGA_READMODE),filename)) return MissingTexture::_Create_Missing_Surface();

	// Direct3D uses image upside down compared to TGA
	targa.Header.ImageDescriptor ^= TGAIDF_YORIGIN;

	WW3DFormat src_format,dest_format;
	unsigned src_bpp=0;
	Get_WW3D_Format(dest_format,src_format,src_bpp,targa);

	if (texture_format!=WW3D_FORMAT_UNKNOWN) {
		dest_format=texture_format;
	}

	// Destination size will be the next power of two square from the larger width and height...
	unsigned width, height;
	width=targa.Header.Width;
	height=targa.Header.Height;
	unsigned src_width=targa.Header.Width;
	unsigned src_height=targa.Header.Height;

	// NOTE: We load the palette but we do not yet support paletted textures!
	char palette[256*4];
	targa.SetPalette(palette);
	if (TARGA_ERROR_HANDLER(targa.Load(filename, TGAF_IMAGE, false),filename)) return MissingTexture::_Create_Missing_Surface();

	unsigned char* src_surface=reinterpret_cast<unsigned char*>(targa.GetImage());

	// No paletted destination format allowed
	unsigned char* converted_surface=nullptr;
	if (src_format==WW3D_FORMAT_A1R5G5B5 || src_format==WW3D_FORMAT_R5G6B5 || src_format==WW3D_FORMAT_A4R4G4B4 ||
		src_format==WW3D_FORMAT_P8 || src_format==WW3D_FORMAT_L8 || src_width!=width || src_height!=height) {
		converted_surface=W3DNEWARRAY unsigned char[width*height*4];
		dest_format=Get_Valid_Texture_Format(WW3D_FORMAT_A8R8G8B8,false);
		BitmapHandlerClass::Copy_Image(
			converted_surface,
			width,
			height,
			width*4,
			WW3D_FORMAT_A8R8G8B8,//dest_format,
			src_surface,
			src_width,
			src_height,
			src_width*src_bpp,
			src_format,
			reinterpret_cast<unsigned char*>(targa.GetPalette()),
			targa.Header.CMapDepth>>3,
			false);
		src_surface=converted_surface;
		src_format=WW3D_FORMAT_A8R8G8B8;//dest_format;
		src_width=width;
		src_height=height;
		src_bpp=Get_Bytes_Per_Pixel(src_format);
	}

	unsigned src_pitch=src_width*src_bpp;

	WW3DSurfaceHandle d3d_surface = RenderDevice::_Create_Device_Surface(width,height,dest_format);
	WWASSERT(d3d_surface);
	WW3DLockedRect locked_rect;
	Device_ErrorCode(
		RenderDevice::Lock_Surface(d3d_surface, &locked_rect, nullptr, 0));

	BitmapHandlerClass::Copy_Image(
		static_cast<unsigned char*>(locked_rect.pBits),
		width,
		height,
		locked_rect.Pitch,
		dest_format,
		src_surface,
		src_width,
		src_height,
		src_pitch,
		src_format,
		reinterpret_cast<unsigned char*>(targa.GetPalette()),
		targa.Header.CMapDepth>>3,
		false);	// No mipmap

	Device_ErrorCode(RenderDevice::Unlock_Surface(d3d_surface));

	if (converted_surface) delete[] converted_surface;

	return d3d_surface;
}


void TextureLoader::Request_Background_Loading(TextureBaseClass *tc)
{
	WWPROFILE(("TextureLoader::Request_Background_Loading()"));
	// Grab the foreground lock. This prevents the foreground thread
	// from retiring any tasks related to this texture. It also 
	// serializes calls to Request_Background_Loading from other
	// threads.
	FastCriticalSectionClass::LockClass foreground_lock(_ForegroundCriticalSection);

	// Has the texture already been loaded?
	if (tc->Is_Initialized()) {
		return;
	}

	TextureLoadTaskClass *task = tc->TextureLoadTask;

	// if texture already has a load task, we don't need to create another one.
	if (task) {
		return;
	}

	task = TextureLoadTaskClass::Create(tc, TextureLoadTaskClass::TASK_LOAD, TextureLoadTaskClass::PRIORITY_LOW);
	
	if (Is_Render_Thread()) {
		Begin_Load_And_Queue(task);
	} else {
		_ForegroundQueue.Push_Back(task);
	}
}


void TextureLoader::Request_Foreground_Loading(TextureBaseClass *tc)
{
	WWPROFILE(("TextureLoader::Request_Foreground_Loading()"));
	// Grab the foreground lock. This prevents the foreground thread
	// from retiring the load tasks for this texture. It also 
	// serializes calls to Request_Foreground_Loading from other
	// threads.
	FastCriticalSectionClass::LockClass foreground_lock(_ForegroundCriticalSection);

	// Has the texture already been loaded?
	if (tc->Is_Initialized()) {
		return;
	}

	TextureLoadTaskClass *task			= tc->TextureLoadTask;

	if (Is_Render_Thread()) {

		// since we're in the render thread, we can load the entire
		// texture right now.

		if (task) {
			// we need to remove the task from any queue, since we're going
			// to finish it up right now.

			// halt background thread. After we're holding this lock,
			// we know the background thread cannot begin loading
			// mipmap levels for this texture.
			FastCriticalSectionClass::LockClass background_lock(_BackgroundCriticalSection);
			_ForegroundQueue.Remove(task);
			_BackgroundQueue.Remove(task);
		} else {
			// Since the task manages all the state associated with loading
			// a texture, we temporarily create one.
			task = TextureLoadTaskClass::Create(tc, TextureLoadTaskClass::TASK_LOAD, TextureLoadTaskClass::PRIORITY_HIGH);
		}

		// finish loading the task and destroy it.
		task->Finish_Load();
		task->Destroy();

	} else {
		// we are not in the render thread. We need to add a high-priority loading
		// task to the foreground queue.

		// Grab the background lock. After we're holding this lock, we
		// know the background thread cannot begin loading mipmap levels 
		// for this texture.
		FastCriticalSectionClass::LockClass background_lock(_BackgroundCriticalSection);

		if (task) {
			// if a load task is waiting on the background queue, we need to 
			// move it to the foreground queue.
			if (task->Get_List() == &_BackgroundQueue) {

				// remove task from list
				_BackgroundQueue.Remove(task);

				// add to foreground queue.
				_ForegroundQueue.Push_Back(task);
			}

			// upgrade the task priority
			task->Set_Priority(TextureLoadTaskClass::PRIORITY_HIGH);

		} else {
			// allocate high priority load task
			task = TextureLoadTaskClass::Create(tc, TextureLoadTaskClass::TASK_LOAD, TextureLoadTaskClass::PRIORITY_HIGH);

			// add to back of foreground queue.
			_ForegroundQueue.Push_Back(task);
		}
	}
}


void TextureLoader::Flush_Pending_Load_Tasks(void)
{
	// This function can only be called from the main thread.
	// (Only the main thread can make the device calls necessary
	// to complete texture loading. If we wanted to flush
	// the pending tasks from another thread, we'd probably
	// want to set a bool that is checked by Update().
	WWASSERT(Is_Render_Thread());

	for (;;) {
		bool done = false;

		{
			// we have no pending load tasks when both queues are empty
			// and the background thread is not processing a texture.
			
			// Grab the background lock. Once we're holding it, we
			// know that the background thread is not processing any
			// textures.

			// NOTE: It's important that we do only hold on to the background
			// lock while we check for completion. Otherwise, we will either
			// violate the lock order when we call Update() (which grabs
			// the foreground lock) or never give the background thread
			// a chance to empty its queue.
			FastCriticalSectionClass::LockClass background_lock(_BackgroundCriticalSection);
			done = _BackgroundQueue.Is_Empty() && _ForegroundQueue.Is_Empty();
		}

		// exit loop if no entries in list
		if (done) {
			break;
		}

		Update();
		ThreadClass::Switch_Thread();
	}
}


// Nework update macro for texture loader.
#pragma warning(push)
#pragma warning(disable:4201) // warning C4201: nonstandard extension used : nameless struct/union
#include <mmsystem.h>
#pragma warning(pop)
#define UPDATE_NETWORK 											\
	if (network_callback) {                            \
		unsigned long time2 = timeGetTime();            \
		if (time2 - time > 20) {                        \
			network_callback();                          \
			time = time2;                                \
		}                                               \
	}                                                  \


void TextureLoader::Update(void (*network_callback)(void))
{
	WWASSERT_PRINT(Is_Render_Thread(), "TextureLoader::Update must be called from the main thread!");

	if (TextureLoadSuspended) {
		return;
	}

	// grab foreground lock to prevent any other thread from
	// modifying texture tasks.
	FastCriticalSectionClass::LockClass lock(_ForegroundCriticalSection);

	unsigned long time = timeGetTime();

	// while we have tasks on the foreground queue
	while (TextureLoadTaskClass *task = _ForegroundQueue.Pop_Front()) {
		UPDATE_NETWORK;
		Process_Foreground_Load(task);
	}
}

void TextureLoader::Suspend_Texture_Load()
{
	WWASSERT_PRINT(Is_Render_Thread(),"TextureLoader::Suspend_Texture_Load must be called from the main thread!");
	TextureLoadSuspended=true;
}

void TextureLoader::Continue_Texture_Load()
{
	WWASSERT_PRINT(Is_Render_Thread(),"TextureLoader::Continue_Texture_Load must be called from the main thread!");
	TextureLoadSuspended=false;
}


void TextureLoader::Process_Foreground_Load(TextureLoadTaskClass *task)
{
	// Is high-priority task?
	if (task->Get_Priority() == TextureLoadTaskClass::PRIORITY_HIGH) {
		task->Finish_Load();
		task->Destroy();
		return;
	}

	// otherwise, must be a low-priority task.

	switch (task->Get_State()) {
		case TextureLoadTaskClass::STATE_NONE:
			Begin_Load_And_Queue(task);
			break;

		case TextureLoadTaskClass::STATE_LOAD_MIPMAP:
			task->End_Load();
			task->Destroy();
			break;
	}
}


void TextureLoader::Begin_Load_And_Queue(TextureLoadTaskClass *task)
{
	// should only be called from the render thread.
	WWASSERT(Is_Render_Thread());

	if (task->Begin_Load()) {
		// add to front of background queue. This means the
		// background load thread will service tasks in LIFO
		// (last in, first out) order.

		// NOTE: this was how the old code did it, with a 
		// comment that mentioned good reasons for doing so,
		// without actually listing the reasons. I suspect 
		// it has something to do with visually important textures,
		// like those in the foreground, starting their load last.
		_BackgroundQueue.Push_Front(task);
	} else {
		// unable to load.
		task->Apply_Missing_Texture();
		task->Destroy();
	}
}


void LoaderThreadClass::Thread_Function(void)
{
	while (running) {
		// if there are no tasks on the background queue, no need to grab background lock.
		if (!_BackgroundQueue.Is_Empty()) {
			// Grab background load so other threads know we could be 
			// loading a texture.
			FastCriticalSectionClass::LockClass lock(_BackgroundCriticalSection);

			// try to remove a task from the background queue. This could fail
			// if another thread modified the queue between our test above and
			// grabbing the lock.
			TextureLoadTaskClass* task = _BackgroundQueue.Pop_Front();
			if (task) {
				// verify task is in proper state for background processing.
				WWASSERT(task->Get_Type() == TextureLoadTaskClass::TASK_LOAD);
				WWASSERT(task->Get_State() == TextureLoadTaskClass::STATE_LOAD_BEGUN);

				// load mip map levels and return to foreground queue for final step.
				task->Load();
				_ForegroundQueue.Push_Back(task);
			}
		}

		Switch_Thread();
	}
}


////////////////////////////////////////////////////////////////////////////////
// 
// TextureLoaderTaskClass implementation
// 
////////////////////////////////////////////////////////////////////////////////

TextureLoadTaskClass::TextureLoadTaskClass()
:	Texture			(0),
	D3DTexture		(0),
	Format			(WW3D_FORMAT_UNKNOWN),
	Width				(0),
	Height			(0),
	MipLevelCount	(0),
	Reduction		(0),
	Type				(TASK_NONE),
	Priority			(PRIORITY_LOW),
	State				(STATE_NONE),
	HSVShift			(0.0f,0.0f,0.0f)	
{
	// because texture load tasks are pooled, the constructor and destructor
	// don't need to do much. The work of attaching a task to a texture is
	// is done by Init() and Deinit().

	for (int i = 0; i < TEXTURE_LOAD_MAX_LEVELS; ++i) {
		LockedSurfacePtr[i]		= nullptr;
		LockedSurfacePitch[i]	= 0;
	}
}


TextureLoadTaskClass::~TextureLoadTaskClass(void)
{
	Deinit();
}


TextureLoadTaskClass *TextureLoadTaskClass::Create(TextureBaseClass *tc, TaskType type, PriorityType priority)
{
	// recycle or create a new texture load task with the given type 
	// and priority, then associate the texture with the task.

	// pull a load task from front of free list
	TextureLoadTaskClass *task = nullptr;
	switch (tc->Get_Asset_Type()) 
	{
		case TextureBaseClass::TEX_REGULAR : task=_TexLoadFreeList.Pop_Front(); break;
		default : WWASSERT(0);
	};

	// if no tasks on free list, allocate a new task
	if (!task) 
	{
		switch (tc->Get_Asset_Type())
		{
		case TextureBaseClass::TEX_REGULAR : task=new TextureLoadTaskClass; break;
		default : WWASSERT(0);
		}
	}
	task->Init(tc, type, priority);
	return task;
}


void TextureLoadTaskClass::Destroy(void)
{
	// detach the task from its texture, and return to free pool.
	Deinit();
	_TexLoadFreeList.Push_Front(this);
}


void TextureLoadTaskClass::Delete_Free_Pool(void)
{
	// (gth) We should probably just MEMPool these task objects...
	while (TextureLoadTaskClass *task = _TexLoadFreeList.Pop_Front()) {
		delete task;
	}
}


void TextureLoadTaskClass::Init(TextureBaseClass* tc, TaskType type, PriorityType priority)
{
	WWASSERT(tc);

	// NOTE: we must be in the main thread to avoid corrupting the texture's refcount.
	WWASSERT(TextureLoader::Is_Render_Thread());
	REF_PTR_SET(Texture, tc);

	// Make sure texture has a filename.
	WWASSERT(Texture->Get_Full_Path() != "");

	Type				= type;
	Priority			= priority;
	State				= STATE_NONE;

	D3DTexture		= 0;

	TextureClass* tex=Texture->As_TextureClass();

	if (tex)
	{
		Format			= tex->Get_Texture_Format(); // don't assume format yet KM
	}
	else
	{
		Format			= WW3D_FORMAT_UNKNOWN;
	}

	Width				= 0;
	Height			= 0;
	MipLevelCount	= Texture->MipLevelCount;
	Reduction		= Texture->Get_Reduction();
	HSVShift			= Texture->Get_HSV_Shift();


	for (int i = 0; i < TEXTURE_LOAD_MAX_LEVELS; ++i) 
	{
		LockedSurfacePtr[i]		= nullptr;
		LockedSurfacePitch[i]	= 0;
	}

	if (Type == TASK_LOAD)
	{
		WWASSERT(Texture->TextureLoadTask == nullptr);
		Texture->TextureLoadTask = this;
	}
}


void TextureLoadTaskClass::Deinit()
{
	// task should not be on any list when it is being detached from texture.
	WWASSERT(Next == nullptr);
	WWASSERT(Prev == nullptr);

	WWASSERT(D3DTexture == nullptr);

	for (int i = 0; i < TEXTURE_LOAD_MAX_LEVELS; ++i) {
		WWASSERT(LockedSurfacePtr[i] == nullptr);
	}

	if (Texture) {
		if (Type == TASK_LOAD) {
			WWASSERT(Texture->TextureLoadTask == this);
			Texture->TextureLoadTask = nullptr;
		}

		// NOTE: we must be in main thread to avoid corrupting Texture's refcount.
		WWASSERT(TextureLoader::Is_Render_Thread());
		REF_PTR_RELEASE(Texture);
	}
}


bool TextureLoadTaskClass::Begin_Load(void)
{
	WWASSERT(TextureLoader::Is_Render_Thread());

	bool loaded = false;

	// if allowed, begin a compressed load
	if (Texture->Is_Compression_Allowed()) {
		loaded = Begin_Compressed_Load();
	}

	// otherwise, begin an uncompressed load
	if (!loaded) {
		loaded = Begin_Uncompressed_Load();
	}

	// if not loaded, abort.
	if (!loaded) {
		return false;
	}

	// lock surfaces in preparation for copy
	Lock_Surfaces();

	State = STATE_LOAD_BEGUN;

	return true;
}


// ----------------------------------------------------------------------------
//
// Load mipmap levels to a pre-generated and locked texture object based on
// information in load task object. Try loading from a DDS file first and if
// that fails try a TGA.
//
// ----------------------------------------------------------------------------
bool TextureLoadTaskClass::Load(void)
{
	WWASSERT(Peek_D3D_Texture());

	bool loaded = false;

	// if allowed, try to load compressed mipmaps
	if (Texture->Is_Compression_Allowed()) {
		loaded = Load_Compressed_Mipmap();
	}

	// otherwise, load uncompressed mipmaps
	if (!loaded) {
		loaded = Load_Uncompressed_Mipmap();
	}

	State = STATE_LOAD_MIPMAP;

	return loaded;
}


void TextureLoadTaskClass::End_Load(void)
{
	WWASSERT(TextureLoader::Is_Render_Thread());

	Unlock_Surfaces();
	Apply(true);

	State = STATE_LOAD_COMPLETE;
}


void TextureLoadTaskClass::Finish_Load(void)
{
	switch (State) {
		// NOTE: fall-through below is intentional.

		case STATE_NONE:
			if (!Begin_Load()) {
				Apply_Missing_Texture();
				break;
			}
			[[fallthrough]];

		case STATE_LOAD_BEGUN:
			Load();
			[[fallthrough]];

		case STATE_LOAD_MIPMAP:
			End_Load();
			break;

		default:
			break;
	}
}


void TextureLoadTaskClass::Apply_Missing_Texture(void)
{
	WWASSERT(TextureLoader::Is_Render_Thread());
	WWASSERT(!D3DTexture);

	D3DTexture = MissingTexture::_Get_Missing_Texture();
	Apply(true);
}


void TextureLoadTaskClass::Apply(bool initialize)
{
	WWASSERT(D3DTexture);

	// Verify that none of the mip levels are locked
	for (unsigned i=0;i<MipLevelCount;++i) {
		WWASSERT(LockedSurfacePtr[i]==nullptr);
	}

	Texture->Apply_New_Surface(D3DTexture, initialize);

	RenderDevice::Release_Resource(D3DTexture);
	D3DTexture = nullptr;
}

static bool	Get_Texture_Information
(
	const char* filename,
	unsigned& reduction,
	unsigned& package_offset,
	unsigned& w,
	unsigned& h,
	unsigned& d,
	WW3DFormat& format,
	unsigned& mip_count,
	bool compressed
)
{
	if (compressed) 
	{
		DDSFileClass dds_file(filename, 0);
		if (!dds_file.Is_Available()) return false;

		// Destination size will be the next power of two square from the larger width and height...
		w = dds_file.Get_Width(0);
		h = dds_file.Get_Height(0);
		d = dds_file.Get_Depth(0);
		format = dds_file.Get_Format();
		mip_count = dds_file.Get_Mip_Level_Count();
		// texture package: the levels above the chosen tier are skipped first
		package_offset = TextureLoader::Package_Mip_Offset(filename, w, h, mip_count);
		//Figure out correct reduction
		int reqReduction=WW3D::Get_Texture_Reduction();	//requested reduction

		if (static_cast<unsigned>(reqReduction) >= mip_count - package_offset)	// unsigned compare (C4018)
			reqReduction=mip_count-package_offset-1;	//leave only the lowest level

		//Clamp reduction
		int curReduction=0;
		int curWidth=w>>package_offset;
		int curHeight=h>>package_offset;
		int minDim=WW3D::Get_Texture_Min_Dimension();

		while (curReduction < reqReduction && curWidth > minDim && curHeight > minDim)
		{	curWidth >>=1;	//keep dividing
			curHeight >>=1;
			curReduction++;
		}
		reduction=curReduction;
		return true;
	}

	Targa targa;
	if (TARGA_ERROR_HANDLER(targa.Open(filename, TGA_READMODE), filename)) 
	{
		return false;
	}

	unsigned int bpp;
	WW3DFormat dest_format;
	Get_WW3D_Format(dest_format,format,bpp,targa);

	mip_count = 0;

	//Figure out how many mip levels this texture will occupy
	for (int i=targa.Header.Width, j=targa.Header.Height; i > 0 && j > 0; i>>=1, j>>=1)
			mip_count++;

	package_offset = TextureLoader::Package_Mip_Offset(filename, targa.Header.Width, targa.Header.Height, mip_count);
	//Figure out correct reduction
	int reqReduction=WW3D::Get_Texture_Reduction();	//requested reduction

	if (static_cast<unsigned>(reqReduction) >= mip_count - package_offset)	// unsigned compare (C4018)
		reqReduction=mip_count-package_offset-1;	//leave only the lowest level

	//Clamp reduction
	int curReduction=0;
	int curWidth=targa.Header.Width>>package_offset;
	int curHeight=targa.Header.Height>>package_offset;
	int minDim=WW3D::Get_Texture_Min_Dimension();

	while (curReduction < reqReduction && curWidth > minDim && curHeight > minDim)
	{	curWidth >>=1;	//keep dividing
		curHeight >>=1;
		curReduction++;
	}
	reduction=curReduction;

	// Destination size will be the next power of two square from the larger width and height...
	w = targa.Header.Width;
	h = targa.Header.Height;
	d = 1;
	return true; 
}


bool TextureLoadTaskClass::Begin_Compressed_Load(void)
{
	unsigned orig_w,orig_h,orig_d,orig_mip_count,reduction,package_offset;
	WW3DFormat orig_format;
	if (!Get_Texture_Information
		  (
				Texture->Get_Full_Path(),
				reduction,
				package_offset,
				orig_w,
				orig_h,
				orig_d,
				orig_format,
				orig_mip_count,
				true
			)
		)
	{
		return false;
	}

	// Destination size will be the next power of two square from the larger width and height...
	unsigned int width	= orig_w;
	unsigned int height	= orig_h;
	TextureLoader::Validate_Texture_Size(width, height,orig_d);

	// If the size doesn't match, try and see if texture reduction would help... (mainly for
	// cases where loaded texture is larger than hardware limit)
	if (width != orig_w || height != orig_h) 
	{
		for (unsigned int i = 1; i < orig_mip_count; ++i) 
		{
			unsigned w=orig_w>>i;
			if (w<4) w=4;
			unsigned h=orig_h>>i;
			if (h<4) h=4;
			unsigned tmp_w=w;
			unsigned tmp_h=h;

			TextureLoader::Validate_Texture_Size(w,h,orig_d);

			if (w == tmp_w && h == tmp_h) 
			{
				Reduction	+= i;
				width			=	w;
				height		=	h;
				break;
			}
		}
	}

	Width		= width;
	Height	= height;
	Format	= Get_Valid_Texture_Format(orig_format, Texture->Is_Compression_Allowed());
	Reduction = reduction;


	if (!Texture->Is_Reducible() || Texture->MipLevelCount == MIP_LEVELS_1)
		Reduction = 0;	//app doesn't want this texture to ever be reduced.
	else
	//Make sure we don't reduce below the level requested by the app
	if (Texture->MipLevelCount != MIP_LEVELS_ALL && (Texture->MipLevelCount - Reduction) < 1)
		Reduction = Texture->MipLevelCount - 1;

	//Another sanity check
	if (Reduction >= orig_mip_count)
		Reduction = 0;	//should not be possible to get here, but check just in case.

	// The package levels are skipped in addition; the app's level count refers to what remains. A
	// texture the app does not allow to reduce (team colours: exact palette pixels) loads as it is.
	if (!Texture->Is_Reducible())
		package_offset = 0;
	const unsigned user_reduction = Reduction;
	Reduction += package_offset;
	if (Reduction >= orig_mip_count)
		Reduction = orig_mip_count - 1;

	unsigned int mip_level_count = Get_Mip_Level_Count();
	int reducedWidth=Width;
	int reducedHeight=Height;

	// If texture wants all mip levels, take as many as the file contains (not necessarily all)
	// Otherwise take as many mip levels as the texture wants, not to exceed the count in file...
	if (!mip_level_count) 
	{
		reducedWidth >>= Reduction;
		reducedHeight >>= Reduction;
		mip_level_count = orig_mip_count-Reduction;//dds_file.Get_Mip_Level_Count();
		if (mip_level_count < 1)
			mip_level_count = 1;	//sanity check to make sure something gets loaded.
	} 
	else
	{
		if (mip_level_count > orig_mip_count - package_offset) 
		{	//dds_file.Get_Mip_Level_Count()) {
			mip_level_count = orig_mip_count - package_offset;//dds_file.Get_Mip_Level_Count();
		}

		if (Reduction)
		{	reducedWidth >>= Reduction;
			reducedHeight >>= Reduction;
			//reduced requested number by those removed.
			mip_level_count = mip_level_count > user_reduction ? mip_level_count - user_reduction : 1;
		}
	}

	// Once more, verify that the mip level count is correct (in case it was changed here it might not
	// match the size...well actually it doesn't have to match but it can't be bigger than the size)
	unsigned int max_mip_level_count = 1;
	unsigned int w = 4;
	unsigned int h = 4;

	while (w < Width && h < Height) 
	{
		w += w;
		h += h;
		max_mip_level_count++;
	}

	if (mip_level_count > max_mip_level_count) 
	{
		mip_level_count = max_mip_level_count;
	}

	D3DTexture	= RenderDevice::_Create_Device_Texture
	(
		reducedWidth, 
		reducedHeight, 
		Format, 
		static_cast<MipCountType>(mip_level_count),
		WW3D_POOL_MANAGED
	);

	MipLevelCount = mip_level_count;

	return true;
}

bool TextureLoadTaskClass::Begin_Uncompressed_Load(void)
{
	unsigned width,height,depth,orig_mip_count,reduction,package_offset;
	WW3DFormat orig_format;
	if (!Get_Texture_Information
		  (
				Texture->Get_Full_Path(),
				reduction,
				package_offset,
				width,
				height,
				depth,
				orig_format,
				orig_mip_count,
				false
			)
		)
	{
		return false;
	}

	WW3DFormat src_format=orig_format;
	WW3DFormat dest_format=src_format;
	dest_format=Get_Valid_Texture_Format(dest_format,false);	// No compressed destination format if reading from targa...

   if (	src_format != WW3D_FORMAT_A8R8G8B8 
   	&&	src_format != WW3D_FORMAT_R8G8B8 
  		&&	src_format != WW3D_FORMAT_X8R8G8B8 ) 
	{
		WWDEBUG_SAY(("Invalid TGA format used in %s - only 24 and 32 bit formats should be used!\n", Texture->Get_Full_Path().Peek_Buffer()));
	}

	// Destination size will be the next power of two square from the larger width and height...
	unsigned ow = width;
	unsigned oh = height;
	TextureLoader::Validate_Texture_Size(width, height,depth);
	if (width != ow || height != oh) 
	{
		WWDEBUG_SAY(("Invalid texture size, scaling required. Texture: %s, size: %d x %d -> %d x %d\n", Texture->Get_Full_Path().Peek_Buffer(), ow, oh, width, height));
	}

	Width		= width;
	Height	= height;
	Reduction = reduction;

	if (!Texture->Is_Reducible() || Texture->MipLevelCount == MIP_LEVELS_1)
		Reduction = 0;	//app doesn't want this texture to ever be reduced.
	else
	//Make sure we don't reduce below the level requested by the app
	if (Texture->MipLevelCount != MIP_LEVELS_ALL && (Texture->MipLevelCount - Reduction) < 1)
		Reduction = Texture->MipLevelCount - 1;

	//Another sanity check
	if (Reduction >= orig_mip_count)
		Reduction = 0;	//should not be possible to get here, but check just in case.

	// The package levels are skipped in addition; the app's level count refers to what remains. A
	// texture the app does not allow to reduce (team colours: exact palette pixels) loads as it is.
	if (!Texture->Is_Reducible())
		package_offset = 0;
	const unsigned user_reduction = Reduction;
	Reduction += package_offset;
	if (Reduction >= orig_mip_count)
		Reduction = orig_mip_count - 1;

	if (Format == WW3D_FORMAT_UNKNOWN) 
	{
		Format=dest_format;
	//	Format = Get_Valid_Texture_Format(dest_format, false); validated above
	}
	else 
	{
		Format = Get_Valid_Texture_Format(Format, false);
	}

	int reducedWidth=Width;
	int reducedHeight=Height;
	int reducedMipCount=Texture->MipLevelCount;

	if (Reduction)
	{	//we don't care about specific levels so reduce them if needed.
		reducedWidth >>= Reduction;
		reducedHeight >>= Reduction;
		if (reducedMipCount != MIP_LEVELS_ALL)
			reducedMipCount -= static_cast<int>(user_reduction);
	}

	D3DTexture = RenderDevice::_Create_Device_Texture
	(
		reducedWidth, 
		reducedHeight, 
		Format, 
		static_cast<MipCountType>(reducedMipCount),
		WW3D_POOL_MANAGED
	);

	return true;
}

/*
bool TextureLoadTaskClass::Begin_Compressed_Load(void)
{
	DDSFileClass dds_file(Texture->Get_Full_Path(), Get_Reduction());
	if (!dds_file.Is_Available()) {
		return false; 
	}

	// Destination size will be the next power of two square from the larger width and height...
	unsigned int width	= dds_file.Get_Width(0);
	unsigned int height	= dds_file.Get_Height(0);
	TextureLoader::Validate_Texture_Size(width, height);

	// If the size doesn't match, try and see if texture reduction would help... (mainly for
	// cases where loaded texture is larger than hardware limit)
	if (width != dds_file.Get_Width(0) || height != dds_file.Get_Height(0)) {
		for (unsigned int i = 1; i < dds_file.Get_Mip_Level_Count(); ++i) {
			unsigned int w = dds_file.Get_Width(i);
			unsigned int h = dds_file.Get_Height(i);
			TextureLoader::Validate_Texture_Size(w,h);

			if (w == dds_file.Get_Width(i) && h == dds_file.Get_Height(i)) {
				Reduction	+= i;
				width			=	w;
				height		=	h;
				break;
			}
		}
	}

	Width		= width;
	Height	= height;
	Format	= Get_Valid_Texture_Format(dds_file.Get_Format(), Texture->Is_Compression_Allowed());

	unsigned int mip_level_count = Get_Mip_Level_Count();

	// If texture wants all mip levels, take as many as the file contains (not necessarily all)
	// Otherwise take as many mip levels as the texture wants, not to exceed the count in file...
	if (!mip_level_count) {
		mip_level_count = dds_file.Get_Mip_Level_Count();
	} else if (mip_level_count > dds_file.Get_Mip_Level_Count()) {
		mip_level_count = dds_file.Get_Mip_Level_Count();
	}

	// Once more, verify that the mip level count is correct (in case it was changed here it might not
	// match the size...well actually it doesn't have to match but it can't be bigger than the size)
	unsigned int max_mip_level_count = 1;
	unsigned int w = 4;
	unsigned int h = 4;

	while (w < Width && h < Height) {
		w += w;
		h += h;
		max_mip_level_count++;
	}

	if (mip_level_count > max_mip_level_count) {
		mip_level_count = max_mip_level_count;
	}

	D3DTexture	= RenderDevice::_Create_Device_Texture(
		Width, 
		Height, 
		Format, 
		(TextureBaseClass::MipCountType)mip_level_count,
		WW3D_POOL_MANAGED);
	MipLevelCount = mip_level_count;
	return true;
}


bool TextureLoadTaskClass::Begin_Uncompressed_Load(void)
{
	Targa targa;
	if (TARGA_ERROR_HANDLER(targa.Open(Texture->Get_Full_Path(), TGA_READMODE), Texture->Get_Full_Path())) {
		return false;
	}

	unsigned int bpp;
	WW3DFormat src_format, dest_format;
	Get_WW3D_Format(dest_format,src_format,bpp,targa);

	if (	src_format != WW3D_FORMAT_A8R8G8B8 
		&&	src_format != WW3D_FORMAT_R8G8B8 
		&&	src_format != WW3D_FORMAT_X8R8G8B8) {
		WWDEBUG_SAY(("Invalid TGA format used in %s - only 24 and 32 bit formats should be used!\n", Texture->Get_Full_Path().Peek_Buffer()));
	}

	// Destination size will be the next power of two square from the larger width and height...
	unsigned width=targa.Header.Width, height=targa.Header.Height;
	int ReductionFactor=Get_Reduction();
	int MipLevels=0;

	//Figure out how many mip levels this texture will occupy
	for (int i=width, j=height; i > 0 && j > 0; i>>=1, j>>=1)
		MipLevels++;

	//Adjust the reduction factor to keep textures above some minimum dimensions
	if (MipLevels <= WW3D::Get_Texture_Min_Mip_Levels())
		ReductionFactor=0;
	else
	{	int mipToDrop=MipLevels-WW3D::Get_Texture_Min_Mip_Levels();
		if (ReductionFactor >= mipToDrop)
		ReductionFactor=mipToDrop;
	}

	width=targa.Header.Width>>ReductionFactor;
	height=targa.Header.Height>>ReductionFactor;
	unsigned ow = width;
	unsigned oh = height;
	TextureLoader::Validate_Texture_Size(width, height);
	if (width != ow || height != oh) {
		WWDEBUG_SAY(("Invalid texture size, scaling required. Texture: %s, size: %d x %d -> %d x %d\n", Texture->Get_Full_Path().Peek_Buffer(), ow, oh, width, height));
	}

	Width		= width;
	Height	= height;

	// changed because format was being read from previous loading task?! KJM
	Format=dest_format;
	//if (Format == WW3D_FORMAT_UNKNOWN) {
	//	Format = Get_Valid_Texture_Format(dest_format, false);
	//} else {
	//	Format = Get_Valid_Texture_Format(Format, false);
	//}

	D3DTexture = RenderDevice::_Create_Device_Texture
	(
		Width, 
		Height, 
		Format, 
		Texture->MipLevelCount,
		WW3D_POOL_MANAGED);
	return true;
}
*/

void TextureLoadTaskClass::Lock_Surfaces(void)
{
	MipLevelCount = RenderDevice::Get_Texture_Level_Count(D3DTexture);
	// never lock more levels than the arrays hold (TEXTURE_LOAD_MAX_LEVELS)
	WWASSERT(MipLevelCount <= TEXTURE_LOAD_MAX_LEVELS);
	if (MipLevelCount > TEXTURE_LOAD_MAX_LEVELS) MipLevelCount = TEXTURE_LOAD_MAX_LEVELS;

	for (unsigned int i = 0; i < MipLevelCount; ++i) 
	{
		WW3DLockedRect locked_rect;
		Device_ErrorCode
		(
			RenderDevice::Lock_Texture(Peek_D3D_Texture(), i, &locked_rect, nullptr, 0)
		);
		LockedSurfacePtr[i]		= static_cast<unsigned char *>(locked_rect.pBits);
		LockedSurfacePitch[i]	= locked_rect.Pitch;
	}
}


// Discard the load: unlock, release an already created D3D texture without assigning it to the
// texture. Destroy() can then return the task to the pool as usual.
void TextureLoadTaskClass::Abort_Load(void)
{
	Unlock_Surfaces();
	if (D3DTexture) {
		RenderDevice::Release_Resource(D3DTexture);
		D3DTexture = nullptr;
	}
	State = STATE_NONE;
}

void TextureLoadTaskClass::Unlock_Surfaces(void)
{
	for (unsigned int i = 0; i < MipLevelCount; ++i) 
	{
		if (LockedSurfacePtr[i]) 
		{
			WWASSERT(ThreadClass::_Get_Current_Thread_ID() == RenderDevice::_Get_Main_Thread_ID());
			Device_ErrorCode(RenderDevice::Unlock_Texture(Peek_D3D_Texture(), i));
		}
		LockedSurfacePtr[i] = nullptr;
	}


}


bool TextureLoadTaskClass::Load_Compressed_Mipmap(void)
{
	DDSFileClass dds_file(Texture->Get_Full_Path(), Get_Reduction());

	// if we can't load from file, indicate rror.
	if (!dds_file.Is_Available() || !dds_file.Load()) 
	{
		return false;
	}

	// regular 2d texture
	unsigned int width	= Get_Width();
	unsigned int height	= Get_Height();

	if (Reduction)
	{	for (unsigned int level = 0; level < Reduction; ++level) {
			width		>>= 1;
			height		>>= 1;
		}
	}

	for (unsigned int level = 0; level < Get_Mip_Level_Count(); ++level) 
	{
		WWASSERT(width && height);
		dds_file.Copy_Level_To_Surface
		(
			level,
			Get_Format(),
			width,
			height,
			Get_Locked_Surface_Ptr(level),
			Get_Locked_Surface_Pitch(level),
			HSVShift
		);

		width		>>= 1;
		height	>>= 1;
	}

	return true;
}


bool TextureLoadTaskClass::Load_Uncompressed_Mipmap(void)
{
	if (!Get_Mip_Level_Count()) 
	{
		return false;
	}

	Targa targa;
	if (TARGA_ERROR_HANDLER(targa.Open(Texture->Get_Full_Path(), TGA_READMODE), Texture->Get_Full_Path())) {
		return false;
	}

	// Direct3D uses image upside down compared to TGA
	targa.Header.ImageDescriptor ^= TGAIDF_YORIGIN;

	WW3DFormat src_format;
	WW3DFormat dest_format;
	unsigned int src_bpp = 0;
	Get_WW3D_Format(dest_format,src_format,src_bpp,targa);
	if (src_format==WW3D_FORMAT_UNKNOWN) return false;

	dest_format = Get_Format();	// Texture can be requested in different format than the most obvious from the TGA

	char palette[256*4];
	targa.SetPalette(palette);

	unsigned int src_width	= targa.Header.Width;
	unsigned int src_height	= targa.Header.Height;
	unsigned int width		= Get_Width();
	unsigned int height		= Get_Height();

	// NOTE: We load the palette but we do not yet support paletted textures!
	if (TARGA_ERROR_HANDLER(targa.Load(Texture->Get_Full_Path(), TGAF_IMAGE, false), Texture->Get_Full_Path())) {
		return false;
	}

	unsigned char * src_surface			= reinterpret_cast<unsigned char*>(targa.GetImage());
	unsigned char * converted_surface	= nullptr;

	// No paletted format allowed when generating mipmaps
	Vector3 hsv_shift=HSVShift;
	if (	src_format	== WW3D_FORMAT_A1R5G5B5 
		|| src_format	== WW3D_FORMAT_R5G6B5 
		|| src_format	== WW3D_FORMAT_A4R4G4B4 
		||	src_format	== WW3D_FORMAT_P8 
		|| src_format	== WW3D_FORMAT_L8 
		|| src_width	!= width 
		|| src_height	!= height) {

		converted_surface = new unsigned char[width*height*4];
		dest_format = Get_Valid_Texture_Format(WW3D_FORMAT_A8R8G8B8, false);

		BitmapHandlerClass::Copy_Image(
			converted_surface,
			width,
			height,
			width*4,
			WW3D_FORMAT_A8R8G8B8,	//dest_format,
			src_surface,
			src_width,
			src_height,
			src_width*src_bpp,
			src_format,
			reinterpret_cast<unsigned char*>(targa.GetPalette()),
			targa.Header.CMapDepth>>3,
			false,
			hsv_shift);
		hsv_shift=Vector3(0.0f,0.0f,0.0f);

		src_surface	= converted_surface;
		src_format	= WW3D_FORMAT_A8R8G8B8;	//dest_format;
		src_width	= width;
		src_height	= height;
		src_bpp		= Get_Bytes_Per_Pixel(src_format);
	}

	unsigned src_pitch = src_width * src_bpp;

	if (Reduction)
	{	//texture needs to be reduced so allocate storage for full-sized version.
		unsigned char * destination_surface	= new unsigned char[width*height*4];
		//generate upper mip-levels that will be dropped in final texture
		for (unsigned int level = 0; level < Reduction; ++level) {
		BitmapHandlerClass::Copy_Image(
			static_cast<unsigned char *>(destination_surface),
			width,
			height,
			src_pitch,
			Get_Format(),
			src_surface,
			src_width,
			src_height,
			src_pitch,
			src_format,
			nullptr,
			0,
			true,
			hsv_shift);

			width			>>= 1;
			height		>>= 1;
			src_width	>>= 1;
			src_height	>>= 1;
		}
		delete [] destination_surface;
	}

	for (unsigned int level = 0; level < Get_Mip_Level_Count(); ++level) {
		WWASSERT(Get_Locked_Surface_Ptr(level));
		BitmapHandlerClass::Copy_Image(
			Get_Locked_Surface_Ptr(level),
			width,
			height,
			Get_Locked_Surface_Pitch(level),
			Get_Format(),
			src_surface,
			src_width,
			src_height,
			src_pitch,
			src_format,
			nullptr,
			0,
			true,
			hsv_shift);
		hsv_shift=Vector3(0.0f,0.0f,0.0f);

		width			>>= 1;
		height		>>= 1;
		src_width	>>= 1;
		src_height	>>= 1;

		if (!width || !height || !src_width || !src_height) {
			break;
		}
	}

	if (converted_surface) {
		delete[] converted_surface;
	}

	return true;
}


unsigned char * TextureLoadTaskClass::Get_Locked_Surface_Ptr(unsigned int level)
{
	WWASSERT(level<MipLevelCount);
	WWASSERT(LockedSurfacePtr[level]);
	return LockedSurfacePtr[level];
}

// ----------------------------------------------------------------------------
//
// Return locked surface pitch (in bytes) at a specific level. The call will
// assert if level is greater or equal to the number of mip levels or if the
// requested level has not been locked.
//
// ----------------------------------------------------------------------------

unsigned int TextureLoadTaskClass::Get_Locked_Surface_Pitch(unsigned int level) const
{
	WWASSERT(level<MipLevelCount);
	WWASSERT(LockedSurfacePtr[level]);
	return LockedSurfacePitch[level];
}




