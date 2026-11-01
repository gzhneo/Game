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
 *                     $Archive:: /Commando/Code/ww3d2/sortingrenderer.cpp                    $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 2                                                           $*
 *                                                                                             *
 * 06/26/02 KM Matrix name change to avoid MAX conflicts                                       *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "sortingrenderer.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "WW3D2/Common/renderdevice.h"
#include "vertmaterial.h"
#include "texture.h"
#include "wwd3dmath.h"	// replacement for the D3DX math helpers
#include "statistics.h"
#include <wwprofile.h>
#include <algorithm>
#include <vector>

bool SortingRendererClass::_EnableTriangleDraw=true;
static unsigned DEFAULT_SORTING_POLY_COUNT = 16384;	// (count * 3) must be less than 65536
static unsigned DEFAULT_SORTING_VERTEX_COUNT = 32768;	// count must be less than 65536

void SortingRendererClass::SetMinVertexBufferSize( unsigned val )
{
	DEFAULT_SORTING_VERTEX_COUNT = val;
	DEFAULT_SORTING_POLY_COUNT = val/2;	//typically have 2:1 vertex:triangle ratio.
}

struct ShortVectorIStruct
{
	unsigned short i;
	unsigned short j;
	unsigned short k;
};

struct TempIndexStruct
{
	ShortVectorIStruct tri;
	unsigned short idx;
	float z;
};

bool operator <(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z < r.z; }
bool operator <=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z <= r.z; }
bool operator >(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z > r.z; }
bool operator >=(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z >= r.z; }
bool operator ==(const TempIndexStruct &l, const TempIndexStruct &r) { return l.z == r.z; }
// ----------------------------------------------------------------------------
static
void InsertionSort(TempIndexStruct *begin, TempIndexStruct *end)
{
	for (TempIndexStruct *iter = begin + 1; iter < end; ++iter) {
		TempIndexStruct val = iter[0];
		TempIndexStruct *insert = iter;
		while (insert != begin && insert[-1] > val) {
			insert[0] = insert[-1];
			insert -= 1;
		}
		insert[0] = val;
	}
}

// ----------------------------------------------------------------------------
static
void Sort(TempIndexStruct *begin, TempIndexStruct *end)
{
	const int diff = static_cast<const int>(end - begin);
	if (diff <= 16) {
		// Insertion sort has less overhead for small arrays
		InsertionSort(begin, end);
	} else {
		// Choose the median of begin, mid, and (end - 1) as the partitioning element.
		// Rearrange so that *(begin + 1) <= *begin <= *(end - 1).  These will be guard
		// elements.
		TempIndexStruct *mid = begin + diff/2;
		std::swap(mid[0], begin[1]);
		if (begin[1] > end[-1]) {
			std::swap(begin[1], end[-1]);
		}
		if (begin[0] > end[-1]) {
			std::swap(begin[0], end[-1]);
		}																// end[-1] has the largest element
		if (begin[1] > begin[0]) {
			std::swap(begin[1], begin[0]);
		}																// begin[0] has the middle element and begin[1] has the smallest element

		// *begin is now the partitioning element
		TempIndexStruct *begin1 = begin + 1;	// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *end1 = end - 1;			// TODO: Temp fix until I find out who is passing me NaN
		TempIndexStruct *left = begin + 1;
		TempIndexStruct *right = end - 1;
		for (;;) {
			do ++left; while (left < end1 && left[0] < begin[0]);		// Scan up to find element >= than partition
			do --right; while (right > begin1 && right[0] > begin[0]);	// Scan down to find element <= than partition
			if (right < left) break;									// Pointers crossed.  Partitioning completed.
			std::swap(left[0], right[0]);							// Exchange elements.
		}
		std::swap(begin[0], right[0]);							// Insert partition element

		// Sort the smaller subarray first then the larger
		if (right - begin > end - (right + 1)) {
			Sort(right + 1, end);
			Sort(begin, right);
		} else {
			Sort(begin, right);
			Sort(right + 1, end);
		}
	}
}

// ----------------------------------------------------------------------------

class SortingNodeStruct : public DLNodeClass<SortingNodeStruct>
{
	W3DMPO_GLUE(SortingNodeStruct)

public:
	RenderStateStruct sorting_state;

	SphereClass bounding_sphere;

	Vector3 transformed_center;
	unsigned short start_index;			// First index used in the ib
	unsigned short polygon_count;			// Polygon count to process (3 indices = one polygon)
	unsigned short min_vertex_index;		// First index used in the vb
	unsigned short vertex_count;			// Number of vertices used in vb

	// Particles on the GPU (Insert_Particles): instances in _ParticlePool, one sorting element each.
	bool particles;
	unsigned particle_first;
	RenderParticleSetup particle_setup;
};

static DLListClass<SortingNodeStruct> sorted_list;
static DLListClass<SortingNodeStruct> clean_list;
static unsigned total_sorting_vertices;
static std::vector<RenderParticleInstance> _ParticlePool;		// instances of the particle nodes of this frame
static std::vector<RenderParticleInstance> _ParticleRun;		// one sorted run of a particle node

static SortingNodeStruct* Get_Sorting_Struct()
{

	SortingNodeStruct* state=clean_list.Head();
	if (state) {
		state->Remove();
	}
	else {
		state=W3DNEW SortingNodeStruct();
	}
	state->particles=false;
	return state;
}

// ----------------------------------------------------------------------------
//
// Temporary arrays for the sorting system
//
// ----------------------------------------------------------------------------

static TempIndexStruct* temp_index_array;
static unsigned temp_index_array_count;

static TempIndexStruct* Get_Temp_Index_Array(unsigned count)
{
	if (count < DEFAULT_SORTING_POLY_COUNT)
		count = DEFAULT_SORTING_POLY_COUNT;
	if (count>temp_index_array_count) {
		delete[] temp_index_array;
		temp_index_array=W3DNEWARRAY TempIndexStruct[count];
		temp_index_array_count=count;
	}
	return temp_index_array;
}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		RenderDevice::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	SNAPSHOT_SAY(("SortingRenderer::Insert(start_i: %d, polygons : %d, min_vi: %d, vertex_count: %d)\n",
		start_index,polygon_count,min_vertex_index,vertex_count));


	WW3D_RECORD_SORTING_RENDER(polygon_count,vertex_count);

	SortingNodeStruct* state=Get_Sorting_Struct();

	RenderDevice::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));


	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->polygon_count=polygon_count;
	state->min_vertex_index=min_vertex_index;
	state->vertex_count=vertex_count;

	[[maybe_unused]] SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	WWD3DMatrix mtx=reinterpret_cast<WWD3DMatrix&>(state->sorting_state.world)*reinterpret_cast<WWD3DMatrix&>(state->sorting_state.view);
	Vector3 vec=const_cast<Vector3&>(state->bounding_sphere.Center);
	Vector4 transformed_vec;
	WWD3D::Vec3Transform(
		&transformed_vec,
		&vec,
		&mtx);
	state->transformed_center=Vector3(transformed_vec.X,transformed_vec.Y,transformed_vec.Z);


	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);

}

// ----------------------------------------------------------------------------
//
// Insert triangles to the sorting system, with no bounding information.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Triangles(
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count)
{
	SphereClass sphere(Vector3(0.0f,0.0f,0.0f),0.0f);
	Insert_Triangles(sphere,start_index,polygon_count,min_vertex_index,vertex_count);
}

// ----------------------------------------------------------------------------
//
// Insert particles drawn on the GPU. Each particle sorts like the two triangles of its quad did.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_Particles(const RenderParticleInstance* particles, unsigned count, const RenderParticleSetup& setup)
{
	if (!particles || !count) return;
	if (!WW3D::Is_Sorting_Enabled()) {
		RenderDevice::Draw_Particles(particles,count,setup);
		return;
	}
	// polygon_count holds the element count; larger groups go in pieces
	while (count > 0) {
		const unsigned n = (count > 0x8000) ? 0x8000 : count;
		WW3D_RECORD_SORTING_RENDER(n*2,n*4);

		SortingNodeStruct* state=Get_Sorting_Struct();
		RenderDevice::Get_Render_State(state->sorting_state);
		state->particles=true;
		state->particle_first=static_cast<unsigned>(_ParticlePool.size());
		state->particle_setup=setup;
		_ParticlePool.insert(_ParticlePool.end(),particles,particles+n);
		state->start_index=0;
		state->polygon_count=static_cast<unsigned short>(n);
		state->min_vertex_index=0;
		state->vertex_count=0;

		// Node order (only used outside the sorting pool): centre of the particles in view space.
		Vector3 center(0.0f,0.0f,0.0f);
		for (unsigned i=0;i<n;++i) {
			center.X+=particles[i].x; center.Y+=particles[i].y; center.Z+=particles[i].z;
		}
		center/=static_cast<float>(n);
		state->bounding_sphere=SphereClass(center,0.0f);
		WWD3DMatrix mtx=reinterpret_cast<WWD3DMatrix&>(state->sorting_state.view);
		Vector4 transformed_vec;
		WWD3D::Vec3Transform(&transformed_vec,&center,&mtx);
		state->transformed_center=Vector3(transformed_vec.X,transformed_vec.Y,transformed_vec.Z);

		SortingNodeStruct* node=sorted_list.Head();
		while (node) {
			if (state->transformed_center.Z>node->transformed_center.Z) {
				if (sorted_list.Head()==sorted_list.Tail())
					sorted_list.Add_Head(state);
				else
					state->Insert_Before(node);
				break;
			}
			node=node->Succ();
		}
		if (!node) sorted_list.Add_Tail(state);

		particles+=n;
		count-=n;
	}
}

// ----------------------------------------------------------------------------
//
// Flush all sorting polygons.
//
// ----------------------------------------------------------------------------

void Release_Refs(SortingNodeStruct* state)
{
	int i;
	for (i=0;i<MAX_VERTEX_STREAMS;++i) {
		REF_PTR_RELEASE(state->sorting_state.vertex_buffers[i]);
	}
	REF_PTR_RELEASE(state->sorting_state.index_buffer);
	REF_PTR_RELEASE(state->sorting_state.material);
	for (i=0;i<RenderCaps::Get_Max_Textures_Per_Pass();++i) 
	{
		REF_PTR_RELEASE(state->sorting_state.Textures[i]);
	}
}

static unsigned overlapping_node_count;
static unsigned overlapping_polygon_count;
static unsigned overlapping_vertex_count;
static const unsigned MAX_OVERLAPPING_NODES=4096;
static SortingNodeStruct* overlapping_nodes[MAX_OVERLAPPING_NODES];

// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_To_Sorting_Pool(SortingNodeStruct* state)
{
	if (overlapping_node_count>=MAX_OVERLAPPING_NODES) {
		Release_Refs(state);
		WWASSERT(0);
		return;
	}

	overlapping_nodes[overlapping_node_count]=state;
	overlapping_vertex_count+=state->vertex_count;
	overlapping_polygon_count+=state->polygon_count;
	overlapping_node_count++;
}

// ----------------------------------------------------------------------------
//static unsigned prevLight = 0xffffffff;

static void Apply_Render_State(RenderStateStruct& render_state)
{



	RenderDevice::Set_Shader(render_state.shader);
	RenderDevice::Set_Soft_Particles(render_state.soft_particles);

	RenderDevice::Set_Material(render_state.material);

	for (int i=0;i<RenderCaps::Get_Max_Textures_Per_Pass();++i) 
	{
		RenderDevice::Set_Texture(i,render_state.Textures[i]);
	}

	RenderDevice::_Set_Device_Transform(WW3D_TS_WORLD,render_state.world);
	RenderDevice::_Set_Device_Transform(WW3D_TS_VIEW,render_state.view);



  if (!render_state.material->Get_Lighting())
    return;
  //prevLight = render_state.lightsHash;

	if (render_state.LightEnable[0]) 
  {
    
    RenderDevice::Set_Device_Light(0,&render_state.Lights[0]);
		if (render_state.LightEnable[1]) 
    {
			RenderDevice::Set_Device_Light(1,&render_state.Lights[1]);
			if (render_state.LightEnable[2]) 
      {
				RenderDevice::Set_Device_Light(2,&render_state.Lights[2]);
				if (render_state.LightEnable[3]) 
					RenderDevice::Set_Device_Light(3,&render_state.Lights[3]);
				else 
					RenderDevice::Set_Device_Light(3,nullptr);
			}
			else 
				RenderDevice::Set_Device_Light(2,nullptr);
		}
		else 
			RenderDevice::Set_Device_Light(1,nullptr);
	}
	else 
		RenderDevice::Set_Device_Light(0,nullptr);


}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush_Sorting_Pool()
{
	if (!overlapping_node_count) return;

	SNAPSHOT_SAY(("SortingSystem - Flush \n"));

	// Fill dynamic index buffer with sorting index buffer vertices
	TempIndexStruct* tis=Get_Temp_Index_Array(overlapping_polygon_count);

	unsigned vertexAllocCount = overlapping_vertex_count;
	if (DynamicVBAccessClass::Get_Default_Vertex_Count() < DEFAULT_SORTING_VERTEX_COUNT)
		vertexAllocCount = DEFAULT_SORTING_VERTEX_COUNT;	//make sure that we force the device dynamic vertex buffer to maximum size
	if (overlapping_vertex_count > vertexAllocCount)
		vertexAllocCount = overlapping_vertex_count;
	WWASSERT(DEFAULT_SORTING_VERTEX_COUNT == 1 || vertexAllocCount <= DEFAULT_SORTING_VERTEX_COUNT);
	DynamicVBAccessClass dyn_vb_access(BUFFER_TYPE_DYNAMIC_DEVICE,dynamic_fvf_type,static_cast<unsigned short>(vertexAllocCount)/*overlapping_vertex_count*/);
	{
		DynamicVBAccessClass::WriteLockClass lock(&dyn_vb_access);
		VertexFormatXYZNDUV2* dest_verts=static_cast<VertexFormatXYZNDUV2 *>(lock.Get_Formatted_Vertex_Array());

		unsigned polygon_array_offset=0;
		unsigned vertex_array_offset=0;
		for (unsigned node_id=0;node_id<overlapping_node_count;++node_id) {
			SortingNodeStruct* state=overlapping_nodes[node_id];
			if (state->particles) {
				// One element per particle, z of its centre in view space (the quad lies in the view plane).
				const Matrix4x4& mtx=reinterpret_cast<const Matrix4x4&>(state->sorting_state.view);
				const RenderParticleInstance* p=&_ParticlePool[state->particle_first];
				for (unsigned i=0;i<state->polygon_count;++i) {
					TempIndexStruct *tis_ptr = tis + polygon_array_offset + i;
					tis_ptr->tri.i = static_cast<unsigned short>(i);
					tis_ptr->tri.j = 0;
					tis_ptr->tri.k = 0;
					tis_ptr->idx = static_cast<unsigned short>(node_id);
					tis_ptr->z = mtx[0][2]*p[i].x + mtx[1][2]*p[i].y + mtx[2][2]*p[i].z + mtx[3][2];
				}
				polygon_array_offset+=state->polygon_count;
				continue;
			}
			VertexFormatXYZNDUV2* src_verts=nullptr;
			[[maybe_unused]] SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
			WWASSERT(vertex_buffer);
			src_verts=vertex_buffer->VertexBuffer;
			WWASSERT(src_verts);
			src_verts+=state->sorting_state.vba_offset;
			src_verts+=state->sorting_state.index_base_offset;
			src_verts+=state->min_vertex_index;

			// If you have a crash in here and "dest_verts" points to illegal memory area,
			// it is because D3D is in illegal state, and the only known cure is rebooting.
			// This illegal state is usually caused by Quake3-engine powered games such as MOHAA.
			memcpy(dest_verts, src_verts, sizeof(VertexFormatXYZNDUV2)*state->vertex_count);
			dest_verts += state->vertex_count;

			WWD3DMatrix d3d_mtx=reinterpret_cast<WWD3DMatrix&>(state->sorting_state.world)*reinterpret_cast<WWD3DMatrix&>(state->sorting_state.view);
			const Matrix4x4& mtx=reinterpret_cast<const Matrix4x4&>(d3d_mtx);

			unsigned short* indices=nullptr;
			SortingIndexBufferClass* index_buffer=static_cast<SortingIndexBufferClass*>(state->sorting_state.index_buffer);
			WWASSERT(index_buffer);
			indices=index_buffer->index_buffer;
			WWASSERT(indices);
			indices+=state->start_index;
			indices+=state->sorting_state.iba_offset;

			if (mtx[0][2] == 0.0f && mtx[1][2] == 0.0f && mtx[3][2] == 0.0f && mtx[2][2] == 1.0f) {
				// The common case for particle systems.
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = static_cast<unsigned short>(idx1 + vertex_array_offset);
					tis_ptr->tri.j = static_cast<unsigned short>(idx2 + vertex_array_offset);
					tis_ptr->tri.k = static_cast<unsigned short>(idx3 + vertex_array_offset);
					tis_ptr->idx = static_cast<unsigned short>(node_id);
					tis_ptr->z = (v1->z + v2->z + v3->z)/3.0f;
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			} else {
				for (int i=0;i<state->polygon_count;++i) {
					unsigned short idx1=indices[i*3]-state->min_vertex_index;
					unsigned short idx2=indices[i*3+1]-state->min_vertex_index;
					unsigned short idx3=indices[i*3+2]-state->min_vertex_index;
					WWASSERT(idx1<state->vertex_count);
					WWASSERT(idx2<state->vertex_count);
					WWASSERT(idx3<state->vertex_count);
					const VertexFormatXYZNDUV2 *v1 = src_verts + idx1;
					const VertexFormatXYZNDUV2 *v2 = src_verts + idx2;
					const VertexFormatXYZNDUV2 *v3 = src_verts + idx3;
					unsigned array_index=i+polygon_array_offset;
					WWASSERT(array_index<overlapping_polygon_count);
					TempIndexStruct *tis_ptr = tis + array_index;
					tis_ptr->tri.i = static_cast<unsigned short>(idx1 + vertex_array_offset);
					tis_ptr->tri.j = static_cast<unsigned short>(idx2 + vertex_array_offset);
					tis_ptr->tri.k = static_cast<unsigned short>(idx3 + vertex_array_offset);
					tis_ptr->idx = static_cast<unsigned short>(node_id);
					tis_ptr->z = (mtx[0][2]*(v1->x + v2->x + v3->x) +
												mtx[1][2]*(v1->y + v2->y + v3->y) +
												mtx[2][2]*(v1->z + v2->z + v3->z))/3.0f + mtx[3][2];
					DEBUG_ASSERTCRASH((! _isnan(tis_ptr->z) && _finite(tis_ptr->z)), ("Triangle has invalid center"));
				}
			}

			state->min_vertex_index=static_cast<unsigned short>(vertex_array_offset);

			polygon_array_offset+=state->polygon_count;
			vertex_array_offset+=state->vertex_count;
		}
	}

	Sort(tis, tis + overlapping_polygon_count);

/*	///@todo: Add code to break up rendering into multiple index buffer fills to allow more than 65536/3 triangles.  -MW
	int total_overlapping_polygon_count = overlapping_polygon_count;
	while (  > 0)
	{
		if ((total_overlapping_polygon_count*3) > 65535)
		{	//overflowed the index buffer, must break into multiple batches
			overlapping_polygon_count = 65535/3;
		}
		else
			overlapping_polygon_count = total_overlapping_polygon_count;

		//insert rendering code here!!

		total_overlapping_polygon_count -= overlapping_polygon_count;
	}
*/
	unsigned polygonAllocCount = overlapping_polygon_count;
	if (static_cast<unsigned>(DynamicIBAccessClass::Get_Default_Index_Count()/3) < DEFAULT_SORTING_POLY_COUNT)
		polygonAllocCount = DEFAULT_SORTING_POLY_COUNT;	//make sure that we force the device index buffer to maximum size
	if (overlapping_polygon_count > polygonAllocCount)
		polygonAllocCount = overlapping_polygon_count;
	WWASSERT(DEFAULT_SORTING_POLY_COUNT <= 1 || polygonAllocCount <= DEFAULT_SORTING_POLY_COUNT);

	DynamicIBAccessClass dyn_ib_access(BUFFER_TYPE_DYNAMIC_DEVICE,static_cast<unsigned short>(polygonAllocCount*3));
	{
		DynamicIBAccessClass::WriteLockClass lock(&dyn_ib_access);
		ShortVectorIStruct* sorted_polygon_index_array=reinterpret_cast<ShortVectorIStruct*>(lock.Get_Index_Array());

		try {
		for (unsigned a=0;a<overlapping_polygon_count;++a) {
			sorted_polygon_index_array[a]=tis[a].tri;
		}
		IndexBufferExceptionFunc();
		} catch(...) {
			IndexBufferExceptionFunc();
		}
	}

	// Set index buffer and render!

	RenderDevice::Set_Index_Buffer(dyn_ib_access,0); // Override with this buffer (do something to prevent need for this!)
	RenderDevice::Set_Vertex_Buffer(dyn_vb_access); // Override with this buffer (do something to prevent need for this!)

	RenderDevice::Apply_Render_State_Changes();

	// All particles of the pool in drawing order, uploaded once; each run then draws its part. Without the
	// batch (too many particles) every run uploads its own.
	_ParticleRun.clear();
	for (unsigned i=0;i<overlapping_polygon_count;++i) {
		const SortingNodeStruct* state=overlapping_nodes[tis[i].idx];
		if (state->particles) {
			_ParticleRun.push_back(_ParticlePool[state->particle_first+tis[i].tri.i]);
		}
	}
	const bool batch=!_ParticleRun.empty() &&
		RenderDevice::Upload_Particle_Batch(_ParticleRun.data(),static_cast<unsigned>(_ParticleRun.size()));
	unsigned particles_drawn=0;

	// A run of elements of one node: triangles from the index buffer, particles as instances in sorted order.
	auto render_run = [&particles_drawn,batch](unsigned run_node, unsigned run_start, unsigned run_count) {
		SortingNodeStruct* state=overlapping_nodes[run_node];
		Apply_Render_State(state->sorting_state);
		if (state->particles) {
			if (batch) {
				RenderDevice::Draw_Particle_Batch(particles_drawn,run_count,state->particle_setup);
			}
			else {
				RenderDevice::Draw_Particles(_ParticleRun.data()+particles_drawn,run_count,state->particle_setup);
			}
			particles_drawn+=run_count;
		}
		else {
			RenderDevice::Draw_Triangles(
				static_cast<unsigned short>(run_start*3),
				static_cast<unsigned short>(run_count),
				state->min_vertex_index,
				state->vertex_count);
		}
	};

	unsigned count_to_render=1;
	unsigned start_index=0;
	unsigned node_id=tis[0].idx;
	for (unsigned i=1;i<overlapping_polygon_count;++i) {
		if (node_id!=tis[i].idx) {
			render_run(node_id,start_index,count_to_render);

			count_to_render=0;
			start_index=i;
			node_id=tis[i].idx;
		}
		count_to_render++;	//keep track of number of polygons of same kind
	}

	// Render any remaining polygons...
	if (count_to_render) {
		render_run(node_id,start_index,count_to_render);
	}

	// Release all references and return nodes back to the clean list for the frame...
	for (node_id=0;node_id<overlapping_node_count;++node_id) {
		SortingNodeStruct* state=overlapping_nodes[node_id];
		Release_Refs(state);
		clean_list.Add_Head(state);
	}
	overlapping_node_count=0;
	overlapping_polygon_count=0;
	overlapping_vertex_count=0;
	_ParticlePool.clear();

	SNAPSHOT_SAY(("SortingSystem - Done flushing\n"));

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Flush()
{
	WWPROFILE("SortingRenderer::Flush");
	Matrix4x4 old_view;
	Matrix4x4 old_world;
	RenderDevice::Get_Transform(WW3D_TS_VIEW,old_view);
	RenderDevice::Get_Transform(WW3D_TS_WORLD,old_world);

	while (SortingNodeStruct* state=sorted_list.Head()) {
		state->Remove();
		
		if (state->particles ||
			((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
			(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING))) {
			Insert_To_Sorting_Pool(state);
		}
		else {
			RenderDevice::Set_Render_State(state->sorting_state);
			RenderDevice::Draw_Triangles(state->start_index,state->polygon_count,state->min_vertex_index,state->vertex_count);
			RenderDevice::Release_Render_State();
			Release_Refs(state);
			clean_list.Add_Head(state);
		}
	}

	bool old_enable=RenderDevice::_Is_Triangle_Draw_Enabled();
	RenderDevice::_Enable_Triangle_Draw(_EnableTriangleDraw);
	Flush_Sorting_Pool();
	RenderDevice::_Enable_Triangle_Draw(old_enable);
	RenderDevice::Set_Soft_Particles(false);	// the last node may have been a particle

	RenderDevice::Set_Index_Buffer(0,0);
	RenderDevice::Set_Vertex_Buffer(0);
	total_sorting_vertices=0;

	DynamicIBAccessClass::_Reset(false);
	DynamicVBAccessClass::_Reset(false);


	RenderDevice::Set_Transform(WW3D_TS_VIEW,old_view);
	RenderDevice::Set_Transform(WW3D_TS_WORLD,old_world);

}

// ----------------------------------------------------------------------------

void SortingRendererClass::Deinit()
{
	SortingNodeStruct *head = nullptr;

	//
	//	Flush the sorted list
	//
	while ((head = sorted_list.Head ()) != nullptr) {
		sorted_list.Remove_Head ();
		delete head;
	}

	//
	//	Flush the clean list
	//
	while ((head = clean_list.Head ()) != nullptr) {
		clean_list.Remove_Head ();
		delete head;
	}

	delete[] temp_index_array;
	temp_index_array=nullptr;
	temp_index_array_count=0;
	_ParticlePool.clear(); _ParticlePool.shrink_to_fit();
	_ParticleRun.clear(); _ParticleRun.shrink_to_fit();
}


// ----------------------------------------------------------------------------
//
// Insert a VolumeParticle triangle into the sorting system.
//
// ----------------------------------------------------------------------------

void SortingRendererClass::Insert_VolumeParticle(
	const SphereClass& bounding_sphere,
	unsigned short start_index, 
	unsigned short polygon_count,
	unsigned short min_vertex_index,
	unsigned short vertex_count,
	unsigned short layerCount)
{
	if (!WW3D::Is_Sorting_Enabled()) {
		RenderDevice::Draw_Triangles(start_index,polygon_count,min_vertex_index,vertex_count);
		return;
	}

	//FOR VOLUME_PARTICLE LOGIC:
	// WE MUST MULTIPLY THE VERTCOUNT AND POLYCOUNT BY THE VOLUME_PARTICLE DEPTH
	WW3D_RECORD_SORTING_RENDER( polygon_count * layerCount,vertex_count * layerCount);//THIS IS VOLUME_PARTICLE SPECIFIC

	SortingNodeStruct* state=Get_Sorting_Struct();
	RenderDevice::Get_Render_State(state->sorting_state);

 	WWASSERT(
		((state->sorting_state.index_buffer_type==BUFFER_TYPE_SORTING || state->sorting_state.index_buffer_type==BUFFER_TYPE_DYNAMIC_SORTING) &&
		(state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_SORTING || state->sorting_state.vertex_buffer_types[0]==BUFFER_TYPE_DYNAMIC_SORTING)));

	state->bounding_sphere=bounding_sphere;
	state->start_index=start_index;
	state->min_vertex_index=min_vertex_index;
	state->polygon_count=polygon_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC
	state->vertex_count=vertex_count * layerCount;//THIS IS VOLUME_PARTICLE SPECIFIC

	[[maybe_unused]] SortingVertexBufferClass* vertex_buffer=static_cast<SortingVertexBufferClass*>(state->sorting_state.vertex_buffers[0]);
	WWASSERT(vertex_buffer);
	WWASSERT(state->vertex_count<=vertex_buffer->Get_Vertex_Count());

	// Transform the center point to view space for sorting

	WWD3DMatrix mtx=reinterpret_cast<WWD3DMatrix&>(state->sorting_state.world)*reinterpret_cast<WWD3DMatrix&>(state->sorting_state.view);
	Vector3 vec=const_cast<Vector3&>(state->bounding_sphere.Center);
	Vector4 transformed_vec;
	WWD3D::Vec3Transform(
		&transformed_vec,
		&vec,
		&mtx);
	state->transformed_center=Vector3(transformed_vec.X,transformed_vec.Y,transformed_vec.Z);


	// BUT WHAT IS THE DEAL WITH THE VERTCOUNT AND POLYCOUNT BEING N BUT TRANSFORMED CENTER COUNT == 1

	//THE TRANSFORMED CENTER[2] IS THE ZBUFFER DEPTH
	
	/// @todo lorenzen sez use a bucket sort here... and stop copying so much data so many times

	SortingNodeStruct* node=sorted_list.Head();
	while (node) {
		if (state->transformed_center.Z>node->transformed_center.Z) {
			if (sorted_list.Head()==sorted_list.Tail())
				sorted_list.Add_Head(state);
			else
				state->Insert_Before(node);
			break;
		}
		node=node->Succ();
	}
	if (!node) sorted_list.Add_Tail(state);
}
