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
 *                 Project Name : ww3d                                                         *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/vertexbuffer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                      $Author:: Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/26/02 5:06p                                             $*
 *                                                                                             *
 *                    $Revision:: 39                                                          $*
 *                                                                                             *
 * 06/26/02 KM VB Vertex format size update for shaders                                       *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define VERTEX_BUFFER_LOG

#include "vertexbuffer.h"
#include "WW3D2/Common/renderdevice.h"
#include "WW3D2/Common/renderfvf.h"
#include "WW3D2/Common/rendercaps.h"
#include "thread.h"

constexpr auto DEFAULT_VB_SIZE = 5000;

static bool _DynamicSortingVertexArrayInUse=false;
//static VertexFormatXYZNDUV2* _DynamicSortingVertexArray=NULL;
static SortingVertexBufferClass* _DynamicSortingVertexArray=nullptr;
static unsigned short _DynamicSortingVertexArraySize=0;
static unsigned short _DynamicSortingVertexArrayOffset=0;	

static bool _DynamicDeviceVertexBufferInUse=false;
static DeviceVertexBufferClass* _DynamicDeviceVertexBuffer=nullptr;
static unsigned short _DynamicDeviceVertexBufferSize=DEFAULT_VB_SIZE;
static unsigned short _DynamicDeviceVertexBufferOffset=0;

static const FVFInfoClass _DynamicFVFInfo(dynamic_fvf_type);

static int _DeviceVertexBufferCount=0;

static int _VertexBufferCount;
static int _VertexBufferTotalVertices;
static int _VertexBufferTotalSize;

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

VertexBufferClass::VertexBufferClass(unsigned type_, unsigned FVF, unsigned short vertex_count_, unsigned vertex_size)
	:
	VertexCount(vertex_count_),
	type(type_),
	engine_refs(0)
{
	WWASSERT(VertexCount);
	WWASSERT(type==BUFFER_TYPE_DEVICE || type==BUFFER_TYPE_SORTING);
	WWASSERT((FVF!=0 && vertex_size==0) || (FVF==0 && vertex_size!=0));
	fvf_info=W3DNEW FVFInfoClass(FVF,vertex_size);

	_VertexBufferCount++;
	_VertexBufferTotalVertices+=VertexCount;
	_VertexBufferTotalSize+=VertexCount*fvf_info->Get_FVF_Size();
#ifdef VERTEX_BUFFER_LOG
	WWDEBUG_SAY(("New VB, %d vertices, size %d bytes\n",VertexCount,VertexCount*fvf_info->Get_FVF_Size()));
	WWDEBUG_SAY(("Total VB count: %d, total %d vertices, total size %d bytes\n",
		_VertexBufferCount,
		_VertexBufferTotalVertices,
		_VertexBufferTotalSize));
#endif
}

// ----------------------------------------------------------------------------

VertexBufferClass::~VertexBufferClass()
{
	_VertexBufferCount--;
	_VertexBufferTotalVertices-=VertexCount;
	_VertexBufferTotalSize-=VertexCount*fvf_info->Get_FVF_Size();

#ifdef VERTEX_BUFFER_LOG
	WWDEBUG_SAY(("Delete VB, %d vertices, size %d bytes\n",VertexCount,VertexCount*fvf_info->Get_FVF_Size()));
	WWDEBUG_SAY(("Total VB count: %d, total %d vertices, total size %d bytes\n",
		_VertexBufferCount,
		_VertexBufferTotalVertices,
		_VertexBufferTotalSize));
#endif
	delete fvf_info;
}

unsigned VertexBufferClass::Get_Total_Buffer_Count()
{
	return _VertexBufferCount;
}

unsigned VertexBufferClass::Get_Total_Allocated_Vertices()
{
	return _VertexBufferTotalVertices;
}

unsigned VertexBufferClass::Get_Total_Allocated_Memory()
{
	return _VertexBufferTotalSize;
}


// ----------------------------------------------------------------------------

void VertexBufferClass::Add_Engine_Ref() const
{
	engine_refs++;
}

// ----------------------------------------------------------------------------

void VertexBufferClass::Release_Engine_Ref() const
{
	engine_refs--;
	WWASSERT(engine_refs>=0);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

VertexBufferClass::WriteLockClass::WriteLockClass(VertexBufferClass* VertexBuffer, int flags)
	:
	VertexBufferLockClass(VertexBuffer)
{
	RENDER_THREAD_ASSERT();
	WWASSERT(VertexBuffer);
	WWASSERT(!VertexBuffer->Engine_Refs());
	VertexBuffer->Add_Ref();
	switch (VertexBuffer->Type()) {
	case BUFFER_TYPE_DEVICE:
#ifdef VERTEX_BUFFER_LOG
		{
		StringClass fvf_name;
		VertexBuffer->FVF_Info().Get_FVF_Name(fvf_name);
		WWDEBUG_SAY(("VertexBuffer->Lock(start_index: 0, index_range: 0(%d), fvf_size: %d, fvf: %s)\n",
			VertexBuffer->Get_Vertex_Count(),
			VertexBuffer->FVF_Info().Get_FVF_Size(),
			fvf_name));
		}
#endif
		Device_Assert();
		Device_ErrorCode(RenderDevice::Lock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(VertexBuffer)->Get_Device_Vertex_Buffer(), 0, 0, static_cast<void**>(&Vertices), flags));	//flags
		break;
	case BUFFER_TYPE_SORTING:
		Vertices=static_cast<SortingVertexBufferClass*>(VertexBuffer)->VertexBuffer;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------

VertexBufferClass::WriteLockClass::~WriteLockClass()
{
	RENDER_THREAD_ASSERT();
	switch (VertexBuffer->Type()) {
	case BUFFER_TYPE_DEVICE:
#ifdef VERTEX_BUFFER_LOG
		WWDEBUG_SAY(("VertexBuffer->Unlock()\n"));
#endif
		Device_Assert();
		Device_ErrorCode(RenderDevice::Unlock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(VertexBuffer)->Get_Device_Vertex_Buffer()));
		break;
	case BUFFER_TYPE_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
	VertexBuffer->Release_Ref();
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

VertexBufferClass::AppendLockClass::AppendLockClass(VertexBufferClass* VertexBuffer,unsigned start_index, unsigned index_range)
	:
	VertexBufferLockClass(VertexBuffer)
{
	RENDER_THREAD_ASSERT();
	WWASSERT(VertexBuffer);
	WWASSERT(!VertexBuffer->Engine_Refs());
	WWASSERT(start_index+index_range<=VertexBuffer->Get_Vertex_Count());
	VertexBuffer->Add_Ref();
	switch (VertexBuffer->Type()) {
	case BUFFER_TYPE_DEVICE:
#ifdef VERTEX_BUFFER_LOG
		{
		StringClass fvf_name;
		VertexBuffer->FVF_Info().Get_FVF_Name(fvf_name);
		WWDEBUG_SAY(("VertexBuffer->Lock(start_index: %d, index_range: %d, fvf_size: %d, fvf: %s)\n",
			start_index,
			index_range,
			VertexBuffer->FVF_Info().Get_FVF_Size(),
			fvf_name));
		}
#endif
		Device_Assert();
		Device_ErrorCode(RenderDevice::Lock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(VertexBuffer)->Get_Device_Vertex_Buffer(), start_index*VertexBuffer->FVF_Info().Get_FVF_Size(), index_range*VertexBuffer->FVF_Info().Get_FVF_Size(), static_cast<void**>(&Vertices), 0));	// Default (no) flags
		break;
	case BUFFER_TYPE_SORTING:
		Vertices=static_cast<SortingVertexBufferClass*>(VertexBuffer)->VertexBuffer+start_index;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------

VertexBufferClass::AppendLockClass::~AppendLockClass()
{
	RENDER_THREAD_ASSERT();
	switch (VertexBuffer->Type()) {
	case BUFFER_TYPE_DEVICE:
		Device_Assert();
#ifdef VERTEX_BUFFER_LOG
		WWDEBUG_SAY(("VertexBuffer->Unlock()\n"));
#endif
		Device_ErrorCode(RenderDevice::Unlock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(VertexBuffer)->Get_Device_Vertex_Buffer()));
		break;
	case BUFFER_TYPE_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
	VertexBuffer->Release_Ref();
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

SortingVertexBufferClass::SortingVertexBufferClass(unsigned short VertexCount)
	:
	VertexBufferClass(BUFFER_TYPE_SORTING, dynamic_fvf_type, VertexCount)
{
	VertexBuffer=W3DNEWARRAY VertexFormatXYZNDUV2[VertexCount];
}

// ----------------------------------------------------------------------------

SortingVertexBufferClass::~SortingVertexBufferClass()
{
	delete[] VertexBuffer;
}


// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

//	bool dynamic=false,bool softwarevp=false);

DeviceVertexBufferClass::DeviceVertexBufferClass(unsigned FVF, unsigned short vertex_count_, UsageType usage, unsigned vertex_size)
	:
	VertexBufferClass(BUFFER_TYPE_DEVICE, FVF, vertex_count_, vertex_size),
	VertexBuffer(nullptr)
{
	Create_Vertex_Buffer(usage);
}

// ----------------------------------------------------------------------------

DeviceVertexBufferClass::DeviceVertexBufferClass(
	const Vector3* vertices,
	const Vector3* normals, 
	const Vector2* tex_coords, 
	unsigned short VertexCount,
	UsageType usage)
	:
	VertexBufferClass(BUFFER_TYPE_DEVICE, WW3D_FVF_XYZ|WW3D_FVF_TEX1|WW3D_FVF_NORMAL, VertexCount),
	VertexBuffer(nullptr)
{
	WWASSERT(vertices);
	WWASSERT(normals);
	WWASSERT(tex_coords);

	Create_Vertex_Buffer(usage);
	Copy(vertices,normals,tex_coords,0,VertexCount);
}

// ----------------------------------------------------------------------------

DeviceVertexBufferClass::DeviceVertexBufferClass(
	const Vector3* vertices,
	const Vector3* normals, 
	const Vector4* diffuse,
	const Vector2* tex_coords, 
	unsigned short VertexCount,
	UsageType usage)
	:
	VertexBufferClass(BUFFER_TYPE_DEVICE, WW3D_FVF_XYZ|WW3D_FVF_TEX1|WW3D_FVF_NORMAL|WW3D_FVF_DIFFUSE, VertexCount),
	VertexBuffer(nullptr)
{
	WWASSERT(vertices);
	WWASSERT(normals);
	WWASSERT(tex_coords);
	WWASSERT(diffuse);

	Create_Vertex_Buffer(usage);
	Copy(vertices,normals,tex_coords,diffuse,0,VertexCount);
}

// ----------------------------------------------------------------------------

DeviceVertexBufferClass::DeviceVertexBufferClass(
	const Vector3* vertices,
	const Vector4* diffuse,
	const Vector2* tex_coords, 
	unsigned short VertexCount,
	UsageType usage)
	:
	VertexBufferClass(BUFFER_TYPE_DEVICE, WW3D_FVF_XYZ|WW3D_FVF_TEX1|WW3D_FVF_DIFFUSE, VertexCount),
	VertexBuffer(nullptr)
{
	WWASSERT(vertices);
	WWASSERT(tex_coords);
	WWASSERT(diffuse);

	Create_Vertex_Buffer(usage);
	Copy(vertices,tex_coords,diffuse,0,VertexCount);
}

// ----------------------------------------------------------------------------

DeviceVertexBufferClass::DeviceVertexBufferClass(
	const Vector3* vertices,
	const Vector2* tex_coords, 
	unsigned short VertexCount,
	UsageType usage)
	:
	VertexBufferClass(BUFFER_TYPE_DEVICE, WW3D_FVF_XYZ|WW3D_FVF_TEX1, VertexCount),
	VertexBuffer(nullptr)
{
	WWASSERT(vertices);
	WWASSERT(tex_coords);

	Create_Vertex_Buffer(usage);
	Copy(vertices,tex_coords,0,VertexCount);
}

// ----------------------------------------------------------------------------

DeviceVertexBufferClass::~DeviceVertexBufferClass()
{
#ifdef VERTEX_BUFFER_LOG
	WWDEBUG_SAY(("VertexBuffer->Release()\n"));
	_DeviceVertexBufferCount--;
	WWDEBUG_SAY(("Current vertex buffer count: %d\n",_DeviceVertexBufferCount));
#endif
	RenderDevice::Release_Resource(VertexBuffer);
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Create_Vertex_Buffer(UsageType usage)
{
	RENDER_THREAD_ASSERT();
	WWASSERT(!VertexBuffer);

#ifdef VERTEX_BUFFER_LOG
	StringClass fvf_name;
	FVF_Info().Get_FVF_Name(fvf_name);
	WWDEBUG_SAY(("CreateVertexBuffer(fvfsize=%d, vertex_count=%d, WW3D_USAGE_WRITEONLY|%s|%s, fvf: %s, %s)\n",
		FVF_Info().Get_FVF_Size(),
		VertexCount,
		(usage&USAGE_DYNAMIC) ? "WW3D_USAGE_DYNAMIC" : "-",
		(usage&USAGE_SOFTWAREPROCESSING) ? "WW3D_USAGE_SOFTWAREPROCESSING" : "-",
		fvf_name,
		(usage&USAGE_DYNAMIC) ? "WW3D_POOL_DEFAULT" : "WW3D_POOL_MANAGED"));
	_DeviceVertexBufferCount++;
	WWDEBUG_SAY(("Current vertex buffer count: %d\n",_DeviceVertexBufferCount));
#endif

	unsigned usage_flags=
		WW3D_USAGE_WRITEONLY|
		((usage&USAGE_DYNAMIC) ? WW3D_USAGE_DYNAMIC : 0)|
		((usage&USAGE_NPATCHES) ? WW3D_USAGE_NPATCHES : 0)|
		((usage&USAGE_SOFTWAREPROCESSING) ? WW3D_USAGE_SOFTWAREPROCESSING : 0);
	if (!RenderCaps::Support_TnL()) {
		usage_flags|=WW3D_USAGE_SOFTWAREPROCESSING;
	}

	// New Code
	if (!RenderCaps::Support_TnL()) {
		usage_flags|=WW3D_USAGE_SOFTWAREPROCESSING;
	}

	HRESULT ret=RenderDevice::Create_Device_Vertex_Buffer(
		FVF_Info().Get_FVF_Size()*VertexCount,
		usage_flags,
		FVF_Info().Get_FVF(),
		(usage&USAGE_DYNAMIC) ? WW3D_POOL_DEFAULT : WW3D_POOL_MANAGED,
		&VertexBuffer);
	if (SUCCEEDED(ret)) {
		return;
	}

	WWDEBUG_SAY(("Vertex buffer creation failed, trying to release assets...\n"));

	// Vertex buffer creation failed, so try releasing least used textures and flushing the mesh cache.

	// Invalidate the mesh cache
	WW3D::_Invalidate_Mesh_Cache();

	//@todo: Find some way to invalidate the textures too
	// There is no ResourceManagerDiscardBytes; the runtime manages resources itself.

	// Try again...
	ret=RenderDevice::Create_Device_Vertex_Buffer(
		FVF_Info().Get_FVF_Size()*VertexCount,
		usage_flags,
		FVF_Info().Get_FVF(),
		(usage&USAGE_DYNAMIC) ? WW3D_POOL_DEFAULT : WW3D_POOL_MANAGED,
		&VertexBuffer);

	if (SUCCEEDED(ret)) {
		WWDEBUG_SAY(("...Vertex buffer creation succesful\n"));
	}

	// If it still fails it is fatal
	Device_ErrorCode(ret);
}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, const Vector3* norm, const Vector2* uv, unsigned first_vertex,unsigned count)
{
	WWASSERT(loc);
	WWASSERT(norm);
	WWASSERT(uv);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZNUV1);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZNUV1* verts=static_cast<VertexFormatXYZNUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZNUV1* verts=static_cast<VertexFormatXYZNUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
		}
	}
}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, unsigned first_vertex, unsigned count)
{
	WWASSERT(loc);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZ);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZ* verts=static_cast<VertexFormatXYZ*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZ* verts=static_cast<VertexFormatXYZ*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
		}
	}

}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, const Vector2* uv, unsigned first_vertex, unsigned count)
{
	WWASSERT(loc);
	WWASSERT(uv);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZUV1);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZUV1* verts=static_cast<VertexFormatXYZUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZUV1* verts=static_cast<VertexFormatXYZUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
		}
	}
}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, const Vector3* norm, unsigned first_vertex, unsigned count)
{
	WWASSERT(loc);
	WWASSERT(norm);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZN);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZN* verts=static_cast<VertexFormatXYZN*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZN* verts=static_cast<VertexFormatXYZN*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
		}
	}
}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, const Vector3* norm, const Vector2* uv, const Vector4* diffuse, unsigned first_vertex, unsigned count)
{
	WWASSERT(loc);
	WWASSERT(norm);
	WWASSERT(uv);
	WWASSERT(diffuse);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZNDUV1);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZNDUV1* verts=static_cast<VertexFormatXYZNDUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
			verts[v].diffuse=RenderDevice::Convert_Color(diffuse[v]);
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZNDUV1* verts=static_cast<VertexFormatXYZNDUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].nx=(*norm)[0];
			verts[v].ny=(*norm)[1];
			verts[v].nz=(*norm++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
			verts[v].diffuse=RenderDevice::Convert_Color(diffuse[v]);
		}
	}
}

// ----------------------------------------------------------------------------

void DeviceVertexBufferClass::Copy(const Vector3* loc, const Vector2* uv, const Vector4* diffuse, unsigned first_vertex, unsigned count)
{
	WWASSERT(loc);
	WWASSERT(uv);
	WWASSERT(diffuse);
	WWASSERT(count<=VertexCount);
	WWASSERT(FVF_Info().Get_FVF()==RENDER_FVF_XYZDUV1);

	if (first_vertex) {
		VertexBufferClass::AppendLockClass l(this,first_vertex,count);
		VertexFormatXYZDUV1* verts=static_cast<VertexFormatXYZDUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
			verts[v].diffuse=RenderDevice::Convert_Color(diffuse[v]);
		}
	}
	else {
		VertexBufferClass::WriteLockClass l(this);
		VertexFormatXYZDUV1* verts=static_cast<VertexFormatXYZDUV1*>(l.Get_Vertex_Array());
		for (unsigned v=0;v<count;++v) {
			verts[v].x=(*loc)[0];
			verts[v].y=(*loc)[1];
			verts[v].z=(*loc++)[2];
			verts[v].u1=(*uv)[0];
			verts[v].v1=(*uv++)[1];
			verts[v].diffuse=RenderDevice::Convert_Color(diffuse[v]);
		}
	}
}

// ----------------------------------------------------------------------------
//
//
//
// ----------------------------------------------------------------------------

DynamicVBAccessClass::DynamicVBAccessClass(unsigned t,unsigned fvf,unsigned short vertex_count_)
	:
	Type(t),
	FVFInfo(_DynamicFVFInfo),
	VertexCount(vertex_count_),
	VertexBuffer(0)
{
	WWASSERT(fvf==dynamic_fvf_type);
	WWASSERT(Type==BUFFER_TYPE_DYNAMIC_DEVICE || Type==BUFFER_TYPE_DYNAMIC_SORTING);

	if (Type==BUFFER_TYPE_DYNAMIC_DEVICE) {
		Allocate_Dynamic_Buffer();
	}
	else {
		Allocate_Sorting_Dynamic_Buffer();
	}
}

DynamicVBAccessClass::~DynamicVBAccessClass()
{
	if (Type==BUFFER_TYPE_DYNAMIC_DEVICE) {
		_DynamicDeviceVertexBufferInUse=false;
		_DynamicDeviceVertexBufferOffset+=static_cast<unsigned>(VertexCount);
	}
	else {
		_DynamicSortingVertexArrayInUse=false;
		_DynamicSortingVertexArrayOffset+=VertexCount;
	}
	
	REF_PTR_RELEASE (VertexBuffer);
}

// ----------------------------------------------------------------------------

void DynamicVBAccessClass::_Deinit()
{
	WWASSERT ((_DynamicDeviceVertexBuffer == nullptr) || (_DynamicDeviceVertexBuffer->Num_Refs() == 1));
	REF_PTR_RELEASE(_DynamicDeviceVertexBuffer);
	_DynamicDeviceVertexBufferInUse=false;
	_DynamicDeviceVertexBufferSize=DEFAULT_VB_SIZE;
	_DynamicDeviceVertexBufferOffset=0;

	WWASSERT ((_DynamicSortingVertexArray == nullptr) || (_DynamicSortingVertexArray->Num_Refs() == 1));
	REF_PTR_RELEASE(_DynamicSortingVertexArray);
	WWASSERT(!_DynamicSortingVertexArrayInUse);
	_DynamicSortingVertexArrayInUse=false;
	_DynamicSortingVertexArraySize=0;
	_DynamicSortingVertexArrayOffset=0;
}

void DynamicVBAccessClass::Allocate_Dynamic_Buffer()
{
	WWASSERT(!_DynamicDeviceVertexBufferInUse);
	_DynamicDeviceVertexBufferInUse=true;

	// If requesting more vertices than dynamic vertex buffer can fit, delete the vb
	// and adjust the size to the new count.
	if (VertexCount>_DynamicDeviceVertexBufferSize) {
		REF_PTR_RELEASE(_DynamicDeviceVertexBuffer);
		_DynamicDeviceVertexBufferSize=VertexCount;
		if (_DynamicDeviceVertexBufferSize<DEFAULT_VB_SIZE) _DynamicDeviceVertexBufferSize=DEFAULT_VB_SIZE;
	}

	// Create a new vb if one doesn't exist currently
	if (!_DynamicDeviceVertexBuffer) {
		unsigned usage=DeviceVertexBufferClass::USAGE_DYNAMIC;
		if (RenderCaps::Support_NPatches()) {
			usage|=DeviceVertexBufferClass::USAGE_NPATCHES;
		}

		_DynamicDeviceVertexBuffer=NEW_REF(DeviceVertexBufferClass,(
			dynamic_fvf_type, 
			_DynamicDeviceVertexBufferSize,
			(DeviceVertexBufferClass::UsageType)usage));
		_DynamicDeviceVertexBufferOffset=0;
	}

	// Any room at the end of the buffer?
	if ((static_cast<unsigned>(VertexCount)+_DynamicDeviceVertexBufferOffset)>_DynamicDeviceVertexBufferSize) {
		_DynamicDeviceVertexBufferOffset=0;
	}

	REF_PTR_SET(VertexBuffer,_DynamicDeviceVertexBuffer);
	VertexBufferOffset=_DynamicDeviceVertexBufferOffset;
}

void DynamicVBAccessClass::Allocate_Sorting_Dynamic_Buffer()
{
	WWASSERT(!_DynamicSortingVertexArrayInUse);
	_DynamicSortingVertexArrayInUse=true;

	unsigned new_vertex_count=_DynamicSortingVertexArrayOffset+VertexCount;
	WWASSERT(new_vertex_count<65536);
	if (new_vertex_count>_DynamicSortingVertexArraySize) {
		REF_PTR_RELEASE(_DynamicSortingVertexArray);
		_DynamicSortingVertexArraySize=static_cast<unsigned short>(new_vertex_count);
		if (_DynamicSortingVertexArraySize<DEFAULT_VB_SIZE) _DynamicSortingVertexArraySize=DEFAULT_VB_SIZE;
	}

	if (!_DynamicSortingVertexArray) {
		_DynamicSortingVertexArray=NEW_REF(SortingVertexBufferClass,(_DynamicSortingVertexArraySize));
		_DynamicSortingVertexArrayOffset=0;
	}

	REF_PTR_SET(VertexBuffer,_DynamicSortingVertexArray);
	VertexBufferOffset=_DynamicSortingVertexArrayOffset;
}

// ----------------------------------------------------------------------------
static int device_lock;
DynamicVBAccessClass::WriteLockClass::WriteLockClass(DynamicVBAccessClass* dynamic_vb_access_)
	:
	DynamicVBAccess(dynamic_vb_access_)
{
	RENDER_THREAD_ASSERT();
	switch (DynamicVBAccess->Get_Type()) {
	case BUFFER_TYPE_DYNAMIC_DEVICE:
#ifdef VERTEX_BUFFER_LOG
/*		{
		WWASSERT(!device_lock);
		device_lock++;
		StringClass fvf_name;
		DynamicVBAccess->VertexBuffer->FVF_Info().Get_FVF_Name(fvf_name);
		WWDEBUG_SAY(("DynamicVertexBuffer->Lock(start_index: %d, index_range: %d, fvf_size: %d, fvf: %s)\n",
			DynamicVBAccess->VertexBufferOffset,
			DynamicVBAccess->Get_Vertex_Count(),
			DynamicVBAccess->VertexBuffer->FVF_Info().Get_FVF_Size(),
			fvf_name));
		}
*/
#endif
		WWASSERT(_DynamicDeviceVertexBuffer);
//		WWASSERT(!_DynamicDeviceVertexBuffer->Engine_Refs());

		Device_Assert();
		// Lock with discard contents if the buffer offset is zero
		Device_ErrorCode(RenderDevice::Lock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(DynamicVBAccess->VertexBuffer)->Get_Device_Vertex_Buffer(), DynamicVBAccess->VertexBufferOffset*_DynamicDeviceVertexBuffer->FVF_Info().Get_FVF_Size(), DynamicVBAccess->Get_Vertex_Count()*DynamicVBAccess->VertexBuffer->FVF_Info().Get_FVF_Size(), reinterpret_cast<void**>(&Vertices), WW3D_LOCK_NOSYSLOCK | (!DynamicVBAccess->VertexBufferOffset ? WW3D_LOCK_DISCARD : WW3D_LOCK_NOOVERWRITE)));
		break;
	case BUFFER_TYPE_DYNAMIC_SORTING:
		Vertices=static_cast<SortingVertexBufferClass*>(DynamicVBAccess->VertexBuffer)->VertexBuffer;
		Vertices+=DynamicVBAccess->VertexBufferOffset;
//		vertices=_DynamicSortingVertexArray+_DynamicSortingVertexArrayOffset;
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------

DynamicVBAccessClass::WriteLockClass::~WriteLockClass()
{
	RENDER_THREAD_ASSERT();
	switch (DynamicVBAccess->Get_Type()) {
	case BUFFER_TYPE_DYNAMIC_DEVICE:
#ifdef VERTEX_BUFFER_LOG
/*		device_lock--;
		WWASSERT(!device_lock);
		WWDEBUG_SAY(("DynamicVertexBuffer->Unlock()\n"));
*/
#endif
		Device_Assert();
		Device_ErrorCode(RenderDevice::Unlock_Vertex_Buffer(static_cast<DeviceVertexBufferClass*>(DynamicVBAccess->VertexBuffer)->Get_Device_Vertex_Buffer()));
		break;
	case BUFFER_TYPE_DYNAMIC_SORTING:
		break;
	default:
		WWASSERT(0);
		break;
	}
}

// ----------------------------------------------------------------------------

void DynamicVBAccessClass::_Reset(bool frame_changed)
{
	_DynamicSortingVertexArrayOffset=0;
	if (frame_changed) _DynamicDeviceVertexBufferOffset=0;
}

unsigned short DynamicVBAccessClass::Get_Default_Vertex_Count(void)
{
	return _DynamicDeviceVertexBufferSize;
}

