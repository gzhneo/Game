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
 *                     $Archive:: /Commando/Code/ww3d2/meshrenderer.cpp                        $*
 *                                                                                             *
 *              Original Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 111                                                         $*
 *                                                                                             *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

//#define ENABLE_CATEGORY_LOG
//#define ENABLE_STRIPING

#include "meshrenderer.h"
#include "WW3D2/Common/renderdevice.h"
#include "polygonrenderer.h"
#include "vertexbuffer.h"
#include "indexbuffer.h"
#include "WW3D2/Common/renderfvf.h"
#include "WW3D2/Common/rendercaps.h"
#include "meshrendererdebugger.h"
#include "wwdebug.h"
#include "wwprofile.h"
#include "rinfo.h"
#include "statistics.h"
#include "meshmdl.h"
#include "vp.h"
#include "decalmsh.h"
#include "matpass.h"
#include "camera.h"
#include "stripoptimizer.h"
#include "meshgeometry.h"
#include <optional>
#include <algorithm>
#include <unordered_map>
#include <vector>

/*
** Global Instance of the MeshRenderer
*/
MeshRendererClass TheMeshRenderer;
bool TextureCategoryClass::m_gForceMultiply = false; // Forces opaque materials to use the multiply blend - pseudo transparent effect.  jba.
// ----------------------------------------------------------------------------

static DynamicVectorClass<Vector3>				_TempVertexBuffer;
static DynamicVectorClass<Vector3>				_TempNormalBuffer;

static MultiListClass<MeshModelClass>			_RegisteredMeshList;
static TextureCategoryList							texture_category_delete_list;
static FVFCategoryList								fvf_category_container_delete_list;

// helper data structure
class PolyRemover : public MultiListObjectClass
{
public:
	TextureCategoryClass *	src;
	TextureCategoryClass *	dest;
	PolygonRendererClass *  pr;
};

using PolyRemoverList = MultiListClass<PolyRemover>;
using PolyRemoverListIterator = MultiListIterator<PolyRemover>;

constexpr auto VERTEX_BUFFER_OVERFLOW = 0xffff;		//'Generals' flag to signal when a mesh didn't fit in streaming vertex buffer.

/**
** PolyRenderTaskClass
** This is a record of a polyrendere that needs to be rendered
** for this frame.  Since MeshClass instances can share meshmodels
** (and therefore their polygon renderers) this record contains
** a pointer to the polygon renderer and the MeshClass instance that
** it is being rendered for. 
*/
class PolyRenderTaskClass : public AutoPoolClass<PolyRenderTaskClass, 256>
{
public:
	PolyRenderTaskClass(PolygonRendererClass * p_renderer,MeshClass * p_mesh) :
		Renderer(p_renderer),
		Mesh(p_mesh),
		NextVisible(nullptr)
	{
		WWASSERT(Renderer != nullptr);
		WWASSERT(Mesh != nullptr);
		Mesh->Add_Ref();
	}

	~PolyRenderTaskClass(void)
	{
		Mesh->Release_Ref();
	}

	PolygonRendererClass *	Peek_Polygon_Renderer(void)							{ return Renderer; }
	MeshClass *						Peek_Mesh(void)											{ return Mesh; }

	PolyRenderTaskClass *		Get_Next_Visible(void)									{ return NextVisible; }
	void								Set_Next_Visible(PolyRenderTaskClass * prtc)		{ NextVisible = prtc; }

protected:

	PolygonRendererClass *	Renderer;
	MeshClass *						Mesh;
	PolyRenderTaskClass *		NextVisible;

};

DEFINE_AUTO_POOL(PolyRenderTaskClass, 256);

/**
** MatPassTaskClass
** This is the record of a material pass that needs to be rendered on
** a particular mesh.  These are linked into the FVF container which
** contains the mesh model.  They are also pooled to remove memory 
** allocation overhead.
*/
class MatPassTaskClass : public AutoPoolClass<MatPassTaskClass, 256>
{
public:
	MatPassTaskClass(MaterialPassClass * pass,MeshClass * mesh) :
		MaterialPass(pass),
		Mesh(mesh),
		NextVisible(nullptr)
	{
		WWASSERT(MaterialPass != nullptr);
		WWASSERT(Mesh != nullptr);
		MaterialPass->Add_Ref();
		Mesh->Add_Ref();
	}

	~MatPassTaskClass(void)
	{
		MaterialPass->Release_Ref();
		Mesh->Release_Ref();
	}
	
	MaterialPassClass *	Peek_Material_Pass(void)							{ return MaterialPass; }
	MeshClass *				Peek_Mesh(void)										{ return Mesh; }
	
	MatPassTaskClass *	Get_Next_Visible(void)								{ return NextVisible; }
	void						Set_Next_Visible(MatPassTaskClass * mpr)		{ NextVisible = mpr; }

private:

	MaterialPassClass *	MaterialPass;
	MeshClass *				Mesh;
	MatPassTaskClass *	NextVisible;
};

DEFINE_AUTO_POOL(MatPassTaskClass, 256);


// ----------------------------------------------------------------------------


inline static bool Equal_Material(const VertexMaterialClass* mat1,const VertexMaterialClass* mat2)
{
	int crc0 = mat1 ? mat1->Get_CRC() : 0;
	int crc1 = mat2 ? mat2->Get_CRC() : 0;
	return (crc0 == crc1);
}


TextureCategoryClass::TextureCategoryClass(
	FVFCategoryContainer* container_,
	TextureClass** texs,
	ShaderClass shd, 
	VertexMaterialClass* mat,
	int pass_)
	:
	pass(pass_),
	shader(shd),
	render_task_head(nullptr),
	material(mat),
	container(container_)
{
	WWASSERT(pass>=0);
	WWASSERT(pass<FVFCategoryContainer::MAX_PASSES);

	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a) 
	{
		textures[a]=nullptr;
		REF_PTR_SET(textures[a],texs[a]);
	}

	if (material) material->Add_Ref();
}

TextureCategoryClass::~TextureCategoryClass()
{
	// Unregistering the mesh where polygon renderers are connected to kills all polygon renderers
	while (PolygonRendererClass* p_renderer=PolygonRenderers.Get_Head()) {
		TheMeshRenderer.Unregister_Mesh_Type(p_renderer->Get_Mesh_Model_Class());
	}
	for (int a=0;a<MeshMatDescClass::MAX_TEX_STAGES;++a) 
	{
		REF_PTR_RELEASE(textures[a]);
	}

	REF_PTR_RELEASE(material);
}

void TextureCategoryClass::Add_Render_Task(PolygonRendererClass * p_renderer,MeshClass * p_mesh)
{
	PolyRenderTaskClass * new_prt = new PolyRenderTaskClass(p_renderer,p_mesh);
	new_prt->Set_Next_Visible(render_task_head);
	render_task_head = new_prt;

	container->Add_Visible_Texture_Category(this,pass);
}

void TextureCategoryClass::Add_Polygon_Renderer(PolygonRendererClass* p_renderer,PolygonRendererClass* add_after_this)
{
	WWASSERT(p_renderer!=nullptr);
	WWASSERT(!PolygonRenderers.Contains(p_renderer));

	if (add_after_this != nullptr) {
		[[maybe_unused]] bool res = PolygonRenderers.Add_After(p_renderer,add_after_this,false);
		WWASSERT(res);	// bool, not a pointer
	} else {
		PolygonRenderers.Add(p_renderer);
	}

	p_renderer->Set_Texture_Category(this);
}

void TextureCategoryClass::Remove_Polygon_Renderer(PolygonRendererClass* p_renderer)
{
	PolygonRenderers.Remove(p_renderer);
	p_renderer->Set_Texture_Category(nullptr);
	if (PolygonRenderers.Peek_Head() == nullptr) {
		container->Remove_Texture_Category(this);
		texture_category_delete_list.Add_Tail(this);
	}
}


void FVFCategoryContainer::Remove_Texture_Category(TextureCategoryClass* tex_category)
{
	unsigned pass;
	for (pass=0;pass<passes;++pass) {
		texture_category_list[pass].Remove(tex_category);
	}
	for (pass=0; pass<passes; pass++) {
		// If any of the texture category lists has anything in it, no need to delete this container
		if (texture_category_list[pass].Peek_Head() != nullptr) return;
	}
	fvf_category_container_delete_list.Add_Tail(this);
}

void FVFCategoryContainer::Add_Visible_Material_Pass(MaterialPassClass * pass,MeshClass * mesh)
{
	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (visible_matpass_head == nullptr) {
		WWASSERT(visible_matpass_tail == nullptr);
		visible_matpass_head = new_mpr;
	} else {
		WWASSERT(visible_matpass_tail != nullptr);
		visible_matpass_tail->Set_Next_Visible(new_mpr);
	}

	visible_matpass_tail = new_mpr;
	AnythingToRender=true;
}

void FVFCategoryContainer::Render_Procedural_Material_Passes(void)
{
	// additional passes
	MatPassTaskClass * mpr = visible_matpass_head;
	MatPassTaskClass * last_mpr = nullptr;
   	bool renderTasksRemaining=false;

	while (mpr != nullptr) {
		SNAPSHOT_SAY(("Render_Procedural_Material_Pass\n"));

   		MeshClass * mesh = mpr->Peek_Mesh();
   	
   		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
   		{	//skip this mesh so it gets rendered later after vertices are filled in.
	        last_mpr = mpr;
   			mpr = mpr->Get_Next_Visible();
   			renderTasksRemaining = true;
   			continue;
   		}
	
		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();
		
		// remove from list, then delete
		if (last_mpr == nullptr) {
			visible_matpass_head = next_mpr;
		} else {
	       last_mpr->Set_Next_Visible(next_mpr);
	    }

		delete mpr;
		mpr = next_mpr;
	}

	visible_matpass_tail = renderTasksRemaining ? last_mpr : nullptr;
}

void RigidFVFCategoryContainer::Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh)
{
	MatPassTaskClass * new_mpr = new MatPassTaskClass(pass,mesh);

	if (delayed_matpass_head == nullptr) {
		WWASSERT(delayed_matpass_tail == nullptr);
		delayed_matpass_head = new_mpr;
	} else {
		WWASSERT(delayed_matpass_tail != nullptr);
		delayed_matpass_tail->Set_Next_Visible(new_mpr);
	}

	delayed_matpass_tail = new_mpr;
	AnyDelayedPassesToRender=true;
}

void RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes(void)
{
	if (!Any_Delayed_Passes_To_Render()) return;
	AnyDelayedPassesToRender=false;

	RenderDevice::Set_Vertex_Buffer(vertex_buffer);
	RenderDevice::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("RigidFVFCategoryContainer::Render_Delayed_Procedural_Material_Passes()\n"));

	// additional passes
	MatPassTaskClass * mpr = delayed_matpass_head;
	while (mpr != nullptr) {
	
		mpr->Peek_Mesh()->Render_Material_Pass(mpr->Peek_Material_Pass(),index_buffer);
		MatPassTaskClass * next_mpr = mpr->Get_Next_Visible();
		
		delete mpr;
		mpr = next_mpr;
	}

	delayed_matpass_head = delayed_matpass_tail = nullptr;
}


void TextureCategoryClass::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("	TextureCategoryClass\n");
	WWDEBUG_SAY((work));

	StringClass work2(255,true);
	for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;++stage) {
		work2.Format("	texture[%d]: %x (%s)\n", stage, textures[stage], textures[stage] ? textures[stage]->Get_Name() : "-");
		work+=work2;
	}
	work2.Format("	material: %x (%s)\n	shader: %x\n", material, material ? material->Get_Name() : "-", shader);
	work+=work2;
	WWDEBUG_SAY((work));

	work.Format("	%8s %8s %6s %6s %6s %5s %s\n",
		"idx_cnt",
		"poly_cnt",
		"i_offs",
		"min_vi",
		"vi_rng",
		"ident",
		"name");
	WWDEBUG_SAY((work));

	PolygonRendererListIterator it(&PolygonRenderers);
	while (!it.Is_Done()) {
	
		PolygonRendererClass* p_renderer = it.Peek_Obj();

		PolyRenderTaskClass * prtc=render_task_head;
		while (prtc) {
			if (prtc->Peek_Polygon_Renderer()==p_renderer) break;
			prtc = prtc->Get_Next_Visible();
		}

		if (prtc != nullptr) {
			WWDEBUG_SAY(("+"));
			p_renderer->Log();
		} else {
			if (!only_visible) {
				WWDEBUG_SAY(("-"));
				p_renderer->Log();
			}
		}
		it.Next();
	}
#endif
}

// ----------------------------------------------------------------------------

FVFCategoryContainer::FVFCategoryContainer(unsigned FVF_,bool sorting_)
	:
	FVF(FVF_),
	sorting(sorting_),
	visible_matpass_head(nullptr),
	visible_matpass_tail(nullptr),
	index_buffer(0),
	used_indices(0),
	passes(MAX_PASSES),
	uv_coordinate_channels(0),
	AnythingToRender(false),
	AnyDelayedPassesToRender(false)
{
	if ((FVF&WW3D_FVF_TEX1)==WW3D_FVF_TEX1) uv_coordinate_channels=1;
	if ((FVF&WW3D_FVF_TEX2)==WW3D_FVF_TEX2) uv_coordinate_channels=2;
	if ((FVF&WW3D_FVF_TEX3)==WW3D_FVF_TEX3) uv_coordinate_channels=3;
	if ((FVF&WW3D_FVF_TEX4)==WW3D_FVF_TEX4) uv_coordinate_channels=4;
	if ((FVF&WW3D_FVF_TEX5)==WW3D_FVF_TEX5) uv_coordinate_channels=5;
	if ((FVF&WW3D_FVF_TEX6)==WW3D_FVF_TEX6) uv_coordinate_channels=6;
	if ((FVF&WW3D_FVF_TEX7)==WW3D_FVF_TEX7) uv_coordinate_channels=7;
	if ((FVF&WW3D_FVF_TEX8)==WW3D_FVF_TEX8) uv_coordinate_channels=8;
}

// ----------------------------------------------------------------------------

FVFCategoryContainer::~FVFCategoryContainer()
{
	REF_PTR_RELEASE(index_buffer);

	for (unsigned p=0;p<passes;++p) {
		while (TextureCategoryClass * tex = texture_category_list[p].Remove_Head()) {
			delete tex;
		}
	}
}

// ----------------------------------------------------------------------------

TextureCategoryClass* FVFCategoryContainer::Find_Matching_Texture_Category(
	TextureClass* texture,
	unsigned pass,
	unsigned stage,
	TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'texture' on given pass and stage.
	TextureCategoryClass* dest_tex_category=nullptr;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (dest_it.Peek_Obj()->Peek_Texture(stage)==texture) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++) {
				if (stage!=s) {
					all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));
				}
			}
			if (all_textures_same &&
				Equal_Material(dest_tex_category->Peek_Material(),ref_category->Peek_Material()) &&
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return nullptr;
}

TextureCategoryClass* FVFCategoryContainer::Find_Matching_Texture_Category(
		VertexMaterialClass* vmat,
		unsigned pass,		
		TextureCategoryClass* ref_category)
{
	// Find texture category which matches ref_category's properties but has 'vmat' on given pass
	TextureCategoryClass* dest_tex_category=nullptr;
	TextureCategoryListIterator dest_it(&texture_category_list[pass]);
	while (!dest_it.Is_Done()) {
		if (Equal_Material(dest_it.Peek_Obj()->Peek_Material(),vmat)) {
			// Compare all stage's textures
			dest_tex_category=dest_it.Peek_Obj();
			bool all_textures_same = true;
			for (unsigned int s = 0; s < MeshMatDescClass::MAX_TEX_STAGES; s++)
				all_textures_same = all_textures_same && (dest_tex_category->Peek_Texture(s) == ref_category->Peek_Texture(s));			
			if (all_textures_same &&				
				dest_tex_category->Get_Shader()==ref_category->Get_Shader()) {
				return dest_tex_category;
			}
		}
		dest_it.Next();
	}
	return nullptr;
}

void FVFCategoryContainer::Change_Polygon_Renderer_Texture(
	PolygonRendererList& polygon_renderer_list,
	TextureClass* texture,
	TextureClass* new_texture,
	unsigned pass,
	unsigned stage)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (texture==new_texture) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		TextureCategoryClass* src_tex_category=src_it.Peek_Obj();		
		if (src_tex_category->Peek_Texture(stage)==texture) {
			foundtexture=true;
			PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();

				if (prc==src_tex_category) {					
					TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_texture,pass,stage,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}
						tmp_textures[stage]=new_texture;

						TextureCategoryClass * new_tex_category=W3DNEW TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(src_tex_category->Peek_Material()),
							pass);
		
						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);					
				}
				poly_it.Next();
			} // while			
		} //if src_texture==texture
		else
			// quit loop if we've got a texture change
			if (foundtexture) break;
		src_it.Next();
	} // while

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);		
		prli.Remove_Current_Object();
		delete rem;
	}
}

void FVFCategoryContainer::Change_Polygon_Renderer_Material(
		PolygonRendererList& polygon_renderer_list,
		VertexMaterialClass* vmat,
		VertexMaterialClass* new_vmat,
		unsigned pass)
{
	WWASSERT(pass<passes);

	PolyRemoverList prl;

	bool foundtexture=false;

	if (vmat==new_vmat) return;

	// Find source texture category, then find all polygon renderers who belong to that category
	// and move them to destination category.
	TextureCategoryListIterator src_it(&texture_category_list[pass]);
	while (!src_it.Is_Done()) {
		TextureCategoryClass* src_tex_category=src_it.Peek_Obj();
		if (src_tex_category->Peek_Material()==vmat) {			
			PolygonRendererListIterator poly_it(&polygon_renderer_list);
			while (!poly_it.Is_Done()) {
				// If source texture category contains polygon renderer, move to destination category
				PolygonRendererClass* polygon_renderer=poly_it.Peek_Obj();
				TextureCategoryClass *prc=polygon_renderer->Get_Texture_Category();
				if (prc==src_tex_category) {
					foundtexture=true;
					TextureCategoryClass* dest_tex_category=Find_Matching_Texture_Category(new_vmat,pass,src_tex_category);

					if (!dest_tex_category) {
						TextureClass * tmp_textures[MeshMatDescClass::MAX_TEX_STAGES];
						for (int s=0;s<MeshMatDescClass::MAX_TEX_STAGES;++s) {
							tmp_textures[s]=src_tex_category->Peek_Texture(s);
						}						

						TextureCategoryClass * new_tex_category=W3DNEW TextureCategoryClass(
							this,
							tmp_textures,
							src_tex_category->Get_Shader(),
							const_cast<VertexMaterialClass*>(new_vmat),
							pass);
		
						/*
						** Add the texture category object into the list, immediately after any existing
						** texture category object which uses the same texture.  This will result in
						** the list always having matching texture categories next to each other.
						*/
						bool found_similar_category = false;
						TextureCategoryListIterator tex_it(&texture_category_list[pass]);
						while (!tex_it.Is_Done()) {
							// Categorize according to first stage's texture for now
							if (tex_it.Peek_Obj()->Peek_Texture(0) == tmp_textures[0]) {
								texture_category_list[pass].Add_After(new_tex_category,tex_it.Peek_Obj());
								found_similar_category = true;
								break;
							}
							tex_it.Next();
						}

						if (!found_similar_category) {
							texture_category_list[pass].Add_Tail(new_tex_category);
						}
						dest_tex_category=new_tex_category;
					}
					PolyRemover *rem=W3DNEW PolyRemover;
					rem->src=src_tex_category;
					rem->dest=dest_tex_category;
					rem->pr=polygon_renderer;
					prl.Add(rem);
				}
				poly_it.Next();
			} // while			
		} // if 
		else
			if (foundtexture) break;
		src_it.Next();
	} // while

	PolyRemoverListIterator prli(&prl);

	while (!prli.Is_Done())
	{
		PolyRemover *rem=prli.Peek_Obj();
		rem->src->Remove_Polygon_Renderer(rem->pr);
		rem->dest->Add_Polygon_Renderer(rem->pr);		
		prli.Remove_Current_Object();
		delete rem;
	}
}

// ----------------------------------------------------------------------------

unsigned FVFCategoryContainer::Define_FVF(MeshModelClass* mmc,bool enable_lighting)
{
	if ((!!mmc->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
		return dynamic_fvf_type;
	}

	unsigned fvf=WW3D_FVF_XYZ;

	int tex_coord_count=mmc->Get_UV_Array_Count();

	if (mmc->Get_Color_Array(0,false)) {
		fvf|=WW3D_FVF_DIFFUSE;
	}
	if (mmc->Get_Color_Array(1,false)) {
		fvf|=WW3D_FVF_SPECULAR;
	}
	
	switch (tex_coord_count) {
	default:
	case 0:
		break;
	case 1: fvf|=WW3D_FVF_TEX1; break;
	case 2: fvf|=WW3D_FVF_TEX2; break;
	case 3: fvf|=WW3D_FVF_TEX3; break;
	case 4: fvf|=WW3D_FVF_TEX4; break;
	case 5: fvf|=WW3D_FVF_TEX5; break;
	case 6: fvf|=WW3D_FVF_TEX6; break;
	case 7: fvf|=WW3D_FVF_TEX7; break;
	case 8: fvf|=WW3D_FVF_TEX8; break;
	}

	if (!mmc->Needs_Vertex_Normals()) {  //enable_lighting || mmc->Get_Flag(MeshModelClass::PRELIT_MASK)) {
		return fvf;
	}

	fvf|=WW3D_FVF_NORMAL;	// Realtime-lit
	return fvf;
}

// ----------------------------------------------------------------------------

RigidFVFCategoryContainer::RigidFVFCategoryContainer(unsigned FVF,bool sorting_)
	:
	FVFCategoryContainer(FVF,sorting_),
	vertex_buffer(0),
	used_vertices(0),
	delayed_matpass_head(nullptr),
	delayed_matpass_tail(nullptr)
{
}

// ----------------------------------------------------------------------------

RigidFVFCategoryContainer::~RigidFVFCategoryContainer()
{
	REF_PTR_RELEASE(vertex_buffer);
}

// ----------------------------------------------------------------------------

void RigidFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("RigidFVFCategoryContainer --------------\n");
	WWDEBUG_SAY((work));
	if (vertex_buffer) {
		StringClass fvfname(255,true);
		vertex_buffer->FVF_Info().Get_FVF_Name(fvfname);
		work.Format("VB size (used/total): %d/%d FVF: %s\n",used_vertices,vertex_buffer->Get_Vertex_Count(),fvfname);
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY VB\n"));
	}
	if (index_buffer) {
		work.Format("IB size (used/total): %d/%d\n",used_indices,index_buffer->Get_Index_Count());
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY IB\n"));
	}

	for (unsigned p=0;p<passes;++p) {
		WWDEBUG_SAY(("Pass: %d\n",p));

		TextureCategoryListIterator it(&texture_category_list[p]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------
//
// Generic render function for rigid meshes
//
// ----------------------------------------------------------------------------

void RigidFVFCategoryContainer::Render(void)
{
	if (!Anything_To_Render()) return;
	AnythingToRender=false;

	RenderDevice::Set_Vertex_Buffer(vertex_buffer);

	RenderDevice::Set_Index_Buffer(index_buffer,0);

	SNAPSHOT_SAY(("RigidFVFCategoryContainer::Render()\n"));
	// The Z-biasing was causing more problems than they solved.
	// Disabling it for now HY.
	//int zbias=0;
	//RenderDevice::Set_ZBias(zbias);
	for (unsigned p=0;p<passes;++p) {
		SNAPSHOT_SAY(("Pass: %d\n",p));
		while (TextureCategoryClass * tex = visible_texture_category_list[p].Remove_Head()) {
			tex->Render();
		}
		//zbias++;
		//if (zbias>15) zbias=15;
		//RenderDevice::Set_ZBias(zbias);
	}

	Render_Procedural_Material_Passes();

	//RenderDevice::Set_ZBias(0);
}

// ----------------------------------------------------------------------------

bool RigidFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!vertex_buffer) return true;	// No VB created - mesh will fit as a new vb will be created when inserting
	unsigned required_vertices=mmc->Get_Vertex_Count();
	unsigned available_vertices=vertex_buffer->Get_Vertex_Count()-used_vertices;
	unsigned required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	} 
	unsigned required_indices=required_polygons*3*mmc->Get_Pass_Count();
	unsigned available_indices=index_buffer->Get_Index_Count()-used_indices;
	if (
		required_vertices<=available_vertices &&
		(required_indices)<=available_indices) {
		return true;
	}
	return false;
}

// ----------------------------------------------------------------------------

class Vertex_Split_Table
{
	MeshModelClass* mmc;
	bool npatch_enable;
	unsigned polygon_count;
	TriIndex* polygon_array;

	bool allocated_polygon_array;

public:
	Vertex_Split_Table(MeshModelClass* mmc_)
		:
		mmc(mmc_),
		npatch_enable(false),
		allocated_polygon_array(false)
	{
		if (RenderCaps::Support_NPatches() && mmc->Needs_Vertex_Normals()) {
			if (mmc->Get_Flag(MeshGeometryClass::ALLOW_NPATCHES)) {
				npatch_enable=true;
			}
		}

		const GapFillerClass* gap_filler=mmc->Get_Gap_Filler();
		polygon_count=mmc->Get_Polygon_Count();
		if (gap_filler) polygon_count+=gap_filler->Get_Polygon_Count();
//		if (mmc->Get_Gap_Filler_Polygon_Count()) {
			allocated_polygon_array=true;
			polygon_array=W3DNEWARRAY TriIndex[polygon_count];
			memcpy(
				polygon_array,
				mmc->Get_Polygon_Array(),
				mmc->Get_Polygon_Count()*sizeof(TriIndex));
			if (gap_filler) {
				memcpy(
					polygon_array+mmc->Get_Polygon_Count(),
					gap_filler->Get_Polygon_Array(),
					gap_filler->Get_Polygon_Count()*sizeof(TriIndex));
			}
//		}
//		else {
//			polygon_array=const_cast<TriIndex*>(mmc->Get_Polygon_Array());
//		}

	}

	~Vertex_Split_Table()
	{
		if (allocated_polygon_array) {
			delete[] polygon_array;
		}
	}

	const Vector3* Get_Vertex_Array() const
	{
		return mmc->Get_Vertex_Array();
	}

	const Vector3* Get_Vertex_Normal_Array() const
	{
		return mmc->Get_Vertex_Normal_Array();
	}

	const unsigned* Get_Color_Array(unsigned index) const
	{
		return mmc->Get_Color_Array(index,false);
	}

	const Vector2* Get_UV_Array(unsigned uv_array_index) const
	{
		return mmc->Get_UV_Array_By_Index(uv_array_index);
	}

	unsigned Get_Vertex_Count() const
	{
		return mmc->Get_Vertex_Count();
	}

	unsigned Get_Polygon_Count() const
	{
		return polygon_count;
	}

	unsigned Get_Pass_Count() const
	{
		return mmc->Get_Pass_Count();
	}

	TextureClass* Peek_Texture(unsigned idx,unsigned pass,unsigned stage)
	{
		if (mmc->Has_Texture_Array(pass,stage)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Texture_Array(pass,stage)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Texture(idx,pass,stage);
		}
		return mmc->Peek_Single_Texture(pass,stage);
	}

	VertexMaterialClass* Peek_Material(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Material_Array(pass)) {
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				return mmc->Get_Gap_Filler()->Get_Material_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			return mmc->Peek_Material(mmc->Get_Polygon_Array()[idx][0],pass);
		}
		return mmc->Peek_Single_Material(pass);
	}

	ShaderClass Peek_Shader(unsigned idx,unsigned pass)
	{
		if (mmc->Has_Shader_Array(pass)) {
			ShaderClass shader;
			
			if (idx>=unsigned(mmc->Get_Polygon_Count())) {
				WWASSERT(mmc->Get_Gap_Filler());
				shader=mmc->Get_Gap_Filler()->Get_Shader_Array(pass)[idx-mmc->Get_Polygon_Count()];
			}
			else shader=mmc->Get_Shader(idx,pass);

			if (npatch_enable) {
				shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
			}

			return shader;
		}
		if (!npatch_enable) return mmc->Get_Single_Shader(pass);
		ShaderClass shader=mmc->Get_Single_Shader(pass);
		shader.Set_NPatch_Enable(ShaderClass::NPATCH_ENABLE);
		return shader;

	}

	MeshModelClass* Get_Mesh_Model_Class()
	{
		return mmc;
	}

	unsigned short* Get_Polygon_Array(unsigned pass)
	{
		return reinterpret_cast<unsigned short*>(polygon_array);
	}
};

// ----------------------------------------------------------------------------

void RigidFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc_)
{
	WWASSERT(Check_If_Mesh_Fits(mmc_));

	Vertex_Split_Table split_table(mmc_);
	int needed_vertices=split_table.Get_Vertex_Count();

	/*
	** This FVFCategoryContainer doesn't have a vertex buffer yet so allocate one big
	** enough to contain this mesh.
	*/
	if (!vertex_buffer) {
		int vb_size=4000;
		if (vb_size<needed_vertices) vb_size=needed_vertices;
		if (sorting) {
			vertex_buffer=NEW_REF(SortingVertexBufferClass,(static_cast<unsigned short>(vb_size)));
			WWASSERT(vertex_buffer->FVF_Info().Get_FVF()==FVF);	// Only one sorting FVF type!
		}
		else {
			vertex_buffer=NEW_REF(DeviceVertexBufferClass,(
				FVF,
				static_cast<unsigned short>(vb_size),
				(RenderCaps::Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DeviceVertexBufferClass::USAGE_NPATCHES : DeviceVertexBufferClass::USAGE_DEFAULT));
		}
	}

	/*
	** Append this mesh's vertices to the vertex buffer.
	*/

	VertexBufferClass::AppendLockClass l(vertex_buffer,used_vertices,split_table.Get_Vertex_Count());
	const FVFInfoClass fi=vertex_buffer->FVF_Info();
	unsigned char *vb=static_cast<unsigned char*>(l.Get_Vertex_Array());
	unsigned int i;
	const Vector3 *locs=split_table.Get_Vertex_Array();
	const Vector3 *norms=split_table.Get_Vertex_Normal_Array();
	const unsigned *diffuse=split_table.Get_Color_Array(0);
	const unsigned *specular=split_table.Get_Color_Array(1);
	for (i=0; i<split_table.Get_Vertex_Count(); i++)
	{
		*reinterpret_cast<Vector3*>(vb+fi.Get_Location_Offset())=locs[i];
		
		if ((FVF&WW3D_FVF_NORMAL)==WW3D_FVF_NORMAL && norms) {
			*reinterpret_cast<Vector3*>(vb+fi.Get_Normal_Offset())=norms[i];
		}

		if ((FVF&WW3D_FVF_DIFFUSE)==WW3D_FVF_DIFFUSE) {
			if (diffuse) {
				*reinterpret_cast<unsigned int*>(vb+fi.Get_Diffuse_Offset())=diffuse[i];
			} else {
				*reinterpret_cast<unsigned int*>(vb+fi.Get_Diffuse_Offset()) = 0xFFFFFFFF;
			}
		}
		
		if ((FVF&WW3D_FVF_SPECULAR)==WW3D_FVF_SPECULAR) {
			if (specular) {
				*reinterpret_cast<unsigned int*>(vb+fi.Get_Specular_Offset())=specular[i];
			} else {
				*reinterpret_cast<unsigned int*>(vb+fi.Get_Specular_Offset()) = 0xFFFFFFFF;
			}
		}

		vb+=fi.Get_FVF_Size();
	}
	

	/*
	** Append the UV coordinates to the vertex buffer
	*/
	int uvcount = 0;
	if ((FVF&WW3D_FVF_TEX1) == WW3D_FVF_TEX1) {
		uvcount = 1;
	}
	if ((FVF&WW3D_FVF_TEX2) == WW3D_FVF_TEX2) {
		uvcount = 2;
	}
	if ((FVF&WW3D_FVF_TEX3) == WW3D_FVF_TEX3) {
		uvcount = 3;
	}
	if ((FVF&WW3D_FVF_TEX4) == WW3D_FVF_TEX4) {
		uvcount = 4;
	}
	if ((FVF&WW3D_FVF_TEX5) == WW3D_FVF_TEX5) {
		uvcount = 5;
	}
	if ((FVF&WW3D_FVF_TEX6) == WW3D_FVF_TEX6) {
		uvcount = 6;
	}
	if ((FVF&WW3D_FVF_TEX7) == WW3D_FVF_TEX7) {
		uvcount = 7;
	}
	if ((FVF&WW3D_FVF_TEX8) == WW3D_FVF_TEX8) {
		uvcount = 8;
	}
	
	for (int j=0; j<uvcount; j++) {
		unsigned char *uv_dst=static_cast<unsigned char*>(l.Get_Vertex_Array());
		const Vector2*uvs=split_table.Get_UV_Array(j);
		if (uvs) {
			for (i=0; i<split_table.Get_Vertex_Count(); i++)
			{
				*reinterpret_cast<Vector2*>(uv_dst+fi.Get_Tex_Offset(j))=uvs[i];
				uv_dst+=fi.Get_FVF_Size();
			}		
		}
	}

	Generate_Texture_Categories(split_table,used_vertices);

	used_vertices+=needed_vertices;//vertex_count;
}

void FVFCategoryContainer::Insert_To_Texture_Category(
	Vertex_Split_Table& split_table,
	TextureClass** texs,
	VertexMaterialClass* mat,
	ShaderClass shader,
	int pass,
	unsigned vertex_offset)
{
	/*
	** Try to find a TextureCategoryClass in this FVF container which matches the
	** given textures(one per stage), material and shader combination.
	*/
	bool fit_in_existing_category = false;
	TextureCategoryListIterator it(&texture_category_list[pass]);
	while (!it.Is_Done()) {
		TextureCategoryClass * tex_category=it.Peek_Obj();
		// Compare all stage's textures
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (tex_category->Peek_Texture(stage) == texs[stage]);
		}
		if (all_textures_same && Equal_Material(tex_category->Peek_Material(),mat) && tex_category->Get_Shader()==shader) {
			used_indices+=tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);
			fit_in_existing_category = true;
			break;
		}
		it.Next();
	}

	if (!fit_in_existing_category) {
		
		TextureCategoryClass * new_tex_category=W3DNEW TextureCategoryClass(this,texs,shader,mat,pass);
		used_indices+=new_tex_category->Add_Mesh(split_table,vertex_offset,used_indices,index_buffer,pass);
		
		/*
		** Add the texture category object into the list, immediately after any existing
		** texture category object which uses the same texture.  This will result in
		** the list always having matching texture categories next to each other.
		*/
		bool found_similar_category = false;
		TextureCategoryListIterator cat_it(&texture_category_list[pass]);
		while (!cat_it.Is_Done()) {
			// Categorize according to first stage's texture for now
			if (cat_it.Peek_Obj()->Peek_Texture(0) == texs[0]) {
				texture_category_list[pass].Add_After(new_tex_category,cat_it.Peek_Obj());
				found_similar_category = true;
				break;
			}
			cat_it.Next();
		}

		if (!found_similar_category) {
			texture_category_list[pass].Add_Tail(new_tex_category);
		}
	}
}

const unsigned MAX_ADDED_TYPE_COUNT=64;
struct Textures_Material_And_Shader_Booking_Struct
{
	TextureClass* added_textures[MeshMatDescClass::MAX_TEX_STAGES][MAX_ADDED_TYPE_COUNT];
	VertexMaterialClass* added_materials[MAX_ADDED_TYPE_COUNT];
	ShaderClass added_shaders[MAX_ADDED_TYPE_COUNT];
	unsigned added_type_count;

	Textures_Material_And_Shader_Booking_Struct() : added_type_count(0) {}

	bool Add_Textures_Material_And_Shader(TextureClass** texs, VertexMaterialClass* mat, ShaderClass shd)
	{
		for (unsigned a=0;a<added_type_count;++a) {
			// Compare textures
			bool all_textures_same = true;
			for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
				all_textures_same = all_textures_same && (texs[stage] == added_textures[stage][a]);
			}
			if (all_textures_same && Equal_Material(mat,added_materials[a]) && shd==added_shaders[a]) {
				return false;
			}
		}
		WWASSERT(added_type_count<MAX_ADDED_TYPE_COUNT);
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			added_textures[stage][added_type_count]=texs[stage];
		}
		added_materials[added_type_count]=mat;
		added_shaders[added_type_count]=shd;
		added_type_count++;
		return true;
	}
};

void FVFCategoryContainer::Generate_Texture_Categories(Vertex_Split_Table& split_table,unsigned vertex_offset)
{
	int polygon_count=split_table.Get_Polygon_Count();
	int index_count=polygon_count*3*split_table.Get_Pass_Count(); 

	/*
	** If we don't have an index buffer yet, allocate one.  Make it hold at least 12000 entries,
	** more if the mesh requires it.  
	*/
	if (!index_buffer) {
		int ib_size=12000;  
		if (ib_size<index_count) ib_size=index_count;
		if (sorting) {
			index_buffer=NEW_REF(SortingIndexBufferClass,(static_cast<unsigned short>(ib_size)));
		}
		else {
			index_buffer=NEW_REF(DeviceIndexBufferClass,(
				static_cast<unsigned short>(ib_size),
				(RenderCaps::Support_NPatches() && WW3D::Get_NPatches_Level()>1) ? DeviceIndexBufferClass::USAGE_NPATCHES : DeviceIndexBufferClass::USAGE_DEFAULT));
		}
	}

	for (unsigned pass=0;pass<split_table.Get_Pass_Count();++pass) {
		Textures_Material_And_Shader_Booking_Struct textures_material_and_shader_booking;

		unsigned old_used_indices=used_indices;

		for (int i=0;i<polygon_count;++i) {
			TextureClass* textures[MeshMatDescClass::MAX_TEX_STAGES];
			// disabled this assert as MAX_TEXTURE_STAGES is now 8, but legacy MeshMat::MAX_TEX_STAGES is still 2
	//		WWASSERT(MAX_TEXTURE_STAGES==MeshMatDescClass::MAX_TEX_STAGES);
			for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;stage++) 
			{
				textures[stage]=split_table.Peek_Texture(i,pass,stage);
			}
			VertexMaterialClass* mat=split_table.Peek_Material(i,pass);
			ShaderClass shader=split_table.Peek_Shader(i,pass);
			if (!textures_material_and_shader_booking.Add_Textures_Material_And_Shader(textures,mat,shader)) continue;

			Insert_To_Texture_Category(split_table,textures,mat,shader,pass,vertex_offset);
		}

		[[maybe_unused]] int new_inds=used_indices-old_used_indices;
		WWASSERT(new_inds<=polygon_count*3);
	}
}

// ----------------------------------------------------------------------------

SkinFVFCategoryContainer::SkinFVFCategoryContainer(bool sorting)
	:
	FVFCategoryContainer(RENDER_FVF_XYZNUV1,sorting),
	VisibleVertexCount(0),
	VisibleSkinHead(nullptr),
	VisibleSkinTail(nullptr)
{
}

// ----------------------------------------------------------------------------

SkinFVFCategoryContainer::~SkinFVFCategoryContainer()
{
}

// ----------------------------------------------------------------------------

void SkinFVFCategoryContainer::Log(bool only_visible)
{
#ifdef ENABLE_CATEGORY_LOG
	StringClass work(255,true);
	work.Format("SkinFVFCategoryContainer --------------\n");
	WWDEBUG_SAY((work));

	if (index_buffer) {
		work.Format("IB size (used/total): %d/%d\n",used_indices,index_buffer->Get_Index_Count());
		WWDEBUG_SAY((work));
	}
	else {
		WWDEBUG_SAY(("EMPTY IB\n"));
	}

	for (unsigned pass=0;pass<passes;++pass) {
		TextureCategoryListIterator it(&texture_category_list[pass]);
		while (!it.Is_Done()) {
			it.Peek_Obj()->Log(only_visible);
			it.Next();
		}
	}
#endif
}

// ----------------------------------------------------------------------------

bool SkinFVFCategoryContainer::GPUSkinning = true;
DynamicVBAccessClass * SkinFVFCategoryContainer::CPUSkinBuffer = nullptr;

// A mesh with procedural material passes is drawn again by Render_Material_Pass from the dynamic buffer,
// so it stays on the CPU.
bool SkinFVFCategoryContainer::Has_Material_Pass(MeshClass * mesh)
{
	for (MatPassTaskClass * mpr = visible_matpass_head; mpr != nullptr; mpr = mpr->Get_Next_Visible()) {
		if (mpr->Peek_Mesh() == mesh) return true;
	}
	return false;
}

void SkinFVFCategoryContainer::Render(void)
{
	SNAPSHOT_SAY(("SkinFVFCategoryContainer::Render()\n"));
	if (!Anything_To_Render()) {
		SNAPSHOT_SAY(("Nothing to render\n"));
		return;
	}
	AnythingToRender=false;

	RenderDevice::Set_Vertex_Buffer(nullptr);	// Free up the reference to the current vertex buffer
														// (in case it is the dynamic, which may have to be resized)

	// GPU skins keep their vertices in a static buffer of the model; TextureCategoryClass::Render binds it
	// with the bone palette per mesh. Only the rest is deformed here into the dynamic buffer.
	unsigned int cpuVertexCount=VisibleVertexCount;
	for (MeshClass * mesh = VisibleSkinHead; mesh != nullptr; mesh = mesh->Peek_Next_Visible_Skin()) {
		const bool gpu = GPUSkinning && !sorting && !mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SORT) &&
			mesh->Check_GPU_Skin() && !Has_Material_Pass(mesh);
		mesh->Set_GPU_Skin(gpu);
		if (gpu) {
			mesh->Set_Base_Vertex_Offset(0);
			cpuVertexCount -= mesh->Peek_Model()->Get_Vertex_Count();
		}
	}

	//'Generals' customization to allow more than 65535 vertices
	unsigned int maxVertexCount=cpuVertexCount;
	if (maxVertexCount > 65535)
	{	//clamp vertex count to maximum size that can be indexed by 16-bit index
		maxVertexCount = 65535;
	}

	std::optional<DynamicVBAccessClass> vb;
	if (cpuVertexCount > 0) {
		vb.emplace(
			sorting ? BUFFER_TYPE_DYNAMIC_SORTING : BUFFER_TYPE_DYNAMIC_DEVICE,
			dynamic_fvf_type,
			maxVertexCount);
	}
	SNAPSHOT_SAY(("DynamicVBAccess - %s - %d vertices\n",sorting ? "sorting" : "non-sorting",cpuVertexCount));

	unsigned int renderedVertexCount=0;

	MeshClass * mesh = VisibleSkinHead;
	MeshClass * remainingMesh = VisibleSkinHead;
	bool firstChunk = true;
	while (firstChunk || renderedVertexCount < cpuVertexCount)
	{	firstChunk = false;
		mesh = remainingMesh;
		remainingMesh = nullptr;
		if (vb) {
			DynamicVBAccessClass::WriteLockClass l(&*vb);
			VertexFormatXYZNDUV2 * dest_verts = l.Get_Formatted_Vertex_Array();
			unsigned vertex_offset=0;

			while (mesh != nullptr) {

				if (mesh->Is_GPU_Skin()) {
					mesh = mesh->Peek_Next_Visible_Skin();
					continue;
				}

				MeshModelClass * mmc = mesh->Peek_Model();
				int mesh_vertex_count=mmc->Get_Vertex_Count();
				//'Generals' mod to deal with cases where not all meshes fit in VB.
				if (vertex_offset+mesh_vertex_count > maxVertexCount || remainingMesh)
				{	//flag mesh so we know it didn't fit in the vertex buffer
					mesh->Set_Base_Vertex_Offset(VERTEX_BUFFER_OVERFLOW);
					if (remainingMesh == nullptr)
						remainingMesh = mesh;	//start of meshes that didn't fit in buffer
					mesh = mesh->Peek_Next_Visible_Skin();	//skip rendering this mesh
					continue;
				}


		// If this assert hits, a skinned mesh has probably been added to the scene more than once.
		// Example: A skinned mesh was added to the scene then it was attached to a bone without being removed from the scene.
		WWASSERT((vertex_offset+mesh_vertex_count)<=cpuVertexCount);
			WW3D_RECORD_SKIN_RENDER(mesh->Get_Num_Polys(),mesh_vertex_count);

				if (_TempVertexBuffer.Length() < mesh_vertex_count) _TempVertexBuffer.Resize(mesh_vertex_count); 
				if (_TempNormalBuffer.Length() < mesh_vertex_count) _TempNormalBuffer.Resize(mesh_vertex_count);

				Vector3* loc=&(_TempVertexBuffer[0]);
				Vector3* norm=&(_TempNormalBuffer[0]);
				const Vector2* uv0=mmc->Get_UV_Array_By_Index(0);
				const Vector2* uv1=mmc->Get_UV_Array_By_Index(1);
				const unsigned* diffuse=mmc->Get_Color_Array(0,false);

				VertexFormatXYZNDUV2* verts=dest_verts+vertex_offset;

				mesh->Get_Deformed_Vertices(loc,norm);

				for (int v=0;v<mesh_vertex_count;++v) {
					verts[v].x=(*loc)[0];
					verts[v].y=(*loc)[1];
					verts[v].z=(*loc)[2];
					verts[v].nx=(*norm)[0];
					verts[v].ny=(*norm)[1];
					verts[v].nz=(*norm)[2];
					if (diffuse) {
						verts[v].diffuse=*diffuse++;
					}
					else {
						verts[v].diffuse=0;
					}
					if (uv0) {
						verts[v].u1=(*uv0)[0];
						verts[v].v1=(*uv0)[1];
						uv0++;
					}
					else {
						verts[v].u1=0.0f;
						verts[v].v1=0.0f;
					}
					if (uv1) {
						verts[v].u2=(*uv1)[0];
						verts[v].v2=(*uv1)[1];
						uv1++;
					}
					else {
						verts[v].u2=0.0f;
						verts[v].v2=0.0f;
					}

					loc++;
					norm++;
				}

				mesh->Set_Base_Vertex_Offset(vertex_offset);
				vertex_offset+=mesh_vertex_count;
				renderedVertexCount += mesh_vertex_count;
				
				mesh = mesh->Peek_Next_Visible_Skin();
			}	//while
		}//lock

		SNAPSHOT_SAY(("Set vb: %x ib: %p\n",vb.has_value(),index_buffer));

		if (vb) RenderDevice::Set_Vertex_Buffer(*vb);
		RenderDevice::Set_Index_Buffer(index_buffer,0);
		CPUSkinBuffer = vb ? &*vb : nullptr;

		//Flush the meshes which fit in the vertex buffer, applying all texture variations
		for (unsigned pass=0;pass<passes;++pass) {
			SNAPSHOT_SAY(("Pass: %d\n",pass));

			TextureCategoryListIterator it(&visible_texture_category_list[pass]);
			while (!it.Is_Done()) {
				it.Peek_Obj()->Render();
				it.Next();
			}
		}
		CPUSkinBuffer = nullptr;

		Render_Procedural_Material_Passes();
	}//while

	//remove all the rendered data from queues
	for (unsigned pass=0;pass<passes;++pass) {
		while (TextureCategoryClass * tex = visible_texture_category_list[pass].Remove_Head()) {
		}
	}

	WWASSERT(renderedVertexCount==cpuVertexCount);


	clearVisibleSkinList();
}

bool SkinFVFCategoryContainer::Check_If_Mesh_Fits(MeshModelClass* mmc)
{
	if (!index_buffer) return true;	// No IB created - mesh will fit as a new ib will be created when inserting
	int required_polygons=mmc->Get_Polygon_Count();
	if (mmc->Get_Gap_Filler()) {
		required_polygons+=mmc->Get_Gap_Filler()->Get_Polygon_Count();
	}

	if ((required_polygons*3*mmc->Get_Pass_Count())<=index_buffer->Get_Index_Count()-used_indices) {
		return true;
	}
	return false;
}

void SkinFVFCategoryContainer::clearVisibleSkinList() 
{
	while (VisibleSkinHead != nullptr)
	{
		MeshClass* next = VisibleSkinHead->Peek_Next_Visible_Skin();
		VisibleSkinHead->Set_Next_Visible_Skin(nullptr);
		VisibleSkinHead->Set_GPU_Skin(false);
		VisibleSkinHead = next;
	}
	VisibleSkinHead = nullptr;
	VisibleSkinTail = nullptr;
	VisibleVertexCount = 0;
}
void SkinFVFCategoryContainer::Add_Visible_Skin(MeshClass * mesh) 
{
	if (mesh->Peek_Next_Visible_Skin() != nullptr || mesh == VisibleSkinTail)
	{
		DEBUG_CRASH(("Mesh %s is already a visible skin, and we tried to add it again... please notify Mark W or Steven J immediately!\n",mesh->Get_Name()));
		return;
	}
	if (VisibleSkinHead == nullptr)
		VisibleSkinTail = mesh;
	mesh->Set_Next_Visible_Skin(VisibleSkinHead);
	VisibleSkinHead = mesh;
	VisibleVertexCount += mesh->Peek_Model()->Get_Vertex_Count();
}


// ----------------------------------------------------------------------------

void SkinFVFCategoryContainer::Reset()
{
	clearVisibleSkinList();
	
	for (unsigned pass=0;pass<passes;++pass) {
		while (TextureCategoryClass* texture_category=texture_category_list[pass].Peek_Head()) {
			delete texture_category;
		}
	}

	REF_PTR_RELEASE(index_buffer);
	used_indices=0;
}

// ----------------------------------------------------------------------------

void SkinFVFCategoryContainer::Add_Mesh(MeshModelClass* mmc)
{
	Vertex_Split_Table split_table(mmc);

	Generate_Texture_Categories(split_table,0);
}

// ----------------------------------------------------------------------------

unsigned TextureCategoryClass::Add_Mesh(
	Vertex_Split_Table& split_table,
	unsigned vertex_offset,
	unsigned index_offset,
	IndexBufferClass* index_buffer,
	unsigned pass_index)
{
	int poly_count=split_table.Get_Polygon_Count();

	unsigned index_count=0;

	/*
	** Count the polygons in the given mesh in the given pass which match this texture category
	*/
	unsigned polygons=0;

	for (int i=0;i<poly_count;++i) {
		bool all_textures_same = true;
		for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
			all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass_index, stage) == textures[stage]);
		}
		VertexMaterialClass* mat=split_table.Peek_Material(i,pass_index);
		ShaderClass shd=split_table.Peek_Shader(i,pass_index);

		if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
			polygons++;
		}
	}

	/*
	** Add the indices for the polygons that match into this renderer's index table.
	*/
	if (polygons) {

		index_count=polygons*3;
#ifndef ENABLE_STRIPING
		bool stripify=false;
#else
		bool stripify=true;
		if (index_buffer->Type()==BUFFER_TYPE_SORTING || index_buffer->Type()==BUFFER_TYPE_DYNAMIC_SORTING) {
			stripify=false;
		}
#endif;
		const TriIndex* src_indices=reinterpret_cast<const TriIndex*>(split_table.Get_Polygon_Array(pass_index));//mmc->Get_Polygon_Array();

		if (stripify) {
			int* triangles=W3DNEWARRAY int[index_count];
			int triangle_index_count=0;
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass_index, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass_index);
				ShaderClass shd=split_table.Peek_Shader(i,pass_index);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					triangles[triangle_index_count++]=src_indices[i][0]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][1]+vertex_offset;
					triangles[triangle_index_count++]=src_indices[i][2]+vertex_offset;
				}
			}

			int* strips=StripOptimizerClass::Stripify(triangles, triangle_index_count/3);
			delete[] triangles;
			int* strip=StripOptimizerClass::Combine_Strips(strips+1,strips[0]);
			delete[] strips;

			if (index_count<unsigned(strip[0])) {
				stripify=false;
			}
			else {
				index_count=strip[0];

				PolygonRendererClass* p_renderer=W3DNEW PolygonRendererClass(
					index_count,
					split_table.Get_Mesh_Model_Class(),
					this,
					vertex_offset,
					index_offset,
					true,
					pass_index);
				PolygonRenderers.Add_Tail(p_renderer);

				{
					IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
					unsigned short* dst_indices=l.Get_Index_Array();

					unsigned short vmin=0xffff;
					unsigned short vmax=0;

					/*
					** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
					*/
					for (unsigned i=0;i<index_count;++i) {
						unsigned short idx;

						idx=static_cast<unsigned short>(strip[i+1]);
						vmin=MIN(vmin,idx);
						vmax=MAX(vmax,idx);
						*dst_indices++=idx;
					}
					
					/*
					** Remember the min and max vertex indices that these polygons used (for optimization)
					*/
					p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
				}
			}
			delete[] strip;
		}

		// Need to check stripify again as it may be changed to false by the previous statement
		if (!stripify ) {
			PolygonRendererClass* p_renderer=W3DNEW PolygonRendererClass(
				index_count,
				split_table.Get_Mesh_Model_Class(),
				this,
				vertex_offset,
				index_offset,
				false,
				pass_index);
			PolygonRenderers.Add_Tail(p_renderer);

			IndexBufferClass::AppendLockClass l(index_buffer,index_offset,index_count);
			unsigned short* dst_indices=l.Get_Index_Array();

			unsigned short vmin=0xffff;
			unsigned short vmax=0;

			/*
			** Iterate over the polys for this pass, adding each one that matches this texture+material+shader
			*/
			for (int i=0;i<poly_count;++i) {
				bool all_textures_same = true;
				for (unsigned int stage = 0; stage < MeshMatDescClass::MAX_TEX_STAGES; stage++) {
					all_textures_same = all_textures_same && (split_table.Peek_Texture(i, pass_index, stage) == textures[stage]);
				}
				VertexMaterialClass* mat=split_table.Peek_Material(i,pass_index);
				ShaderClass shd=split_table.Peek_Shader(i,pass_index);

				if (all_textures_same && Equal_Material(mat,material) && shd==shader) {
					unsigned short idx;

					idx=static_cast<unsigned short>(src_indices[i][0]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=static_cast<unsigned short>(src_indices[i][1]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d, ",idx));

					idx=static_cast<unsigned short>(src_indices[i][2]+vertex_offset);
					vmin=MIN(vmin,idx);
					vmax=MAX(vmax,idx);
					*dst_indices++=idx;
//					WWDEBUG_SAY(("%d\n",idx));
				}
			}

			WWASSERT((vmax-vmin)<split_table.Get_Mesh_Model_Class()->Get_Vertex_Count());
			
			/*
			** Remember the min and max vertex indices that these polygons used (for optimization)
			*/
			p_renderer->Set_Vertex_Index_Range(vmin,vmax-vmin+1);
			WWASSERT(index_count<=unsigned(split_table.Get_Polygon_Count()*3));
		}
	}

	return index_count;
}

// ----------------------------------------------------------------------------

// After a GPU skin: palette off, the dynamic buffer of the CPU skins again (if there is one).
static void Unbind_GPU_Skin(void)
{
	RenderDevice::Set_Skin_Bones(nullptr, 0);
	DynamicVBAccessClass * cpu = SkinFVFCategoryContainer::Peek_CPU_Skin_Buffer();
	if (cpu) {
		RenderDevice::Set_Vertex_Buffer(*cpu);
	}
	else {
		RenderDevice::Set_Vertex_Buffer(nullptr);
	}
}

// A task the backend may merge with its neighbours into one instanced draw: drawn immediately with the
// category's state, changing nothing but lights and world transform.
static bool Is_Instanceable_Task(PolyRenderTaskClass *prt)
{
	MeshClass *mesh = prt->Peek_Mesh();
	if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW) return false;
	if (MeshRendererDebugger::Is_Enabled() && mesh->Is_Disabled_By_Debugger()) return false;
	if (mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SORT) && WW3D::Is_Sorting_Enabled()) return false;
	if (mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN) || mesh->Is_GPU_Skin()) return false;
	if (mesh->Get_Alpha_Override() != 1.0) return false;
	if (mesh->Get_User_Data() && *static_cast<int *>(mesh->Get_User_Data()) == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE) return false;
	return true;
}

// Brings the tasks of the same polygon renderer together (in the order of their first task), so the
// backend can draw each model's instances at once. Only for categories without blending and without
// tasks that blend on their own (overrides, sorting): there the drawing order does not change the image.
// Scratch containers are kept between frames; allocated on first use and freed in Shutdown, because
// function-local statics would only die after the memory report and count as leaks.
struct GroupTasksScratch
{
	std::vector<PolyRenderTaskClass *> tasks;
	std::vector<std::pair<unsigned, unsigned>> order;		// first index of the renderer, index
	std::unordered_map<PolygonRendererClass *, unsigned> first;
};
static GroupTasksScratch *s_groupTasksScratch = nullptr;

static PolyRenderTaskClass *Group_Tasks_By_Renderer(PolyRenderTaskClass *head)
{
	if (!s_groupTasksScratch) s_groupTasksScratch = W3DNEW GroupTasksScratch;
	std::vector<PolyRenderTaskClass *> &tasks = s_groupTasksScratch->tasks;
	std::vector<std::pair<unsigned, unsigned>> &order = s_groupTasksScratch->order;
	std::unordered_map<PolygonRendererClass *, unsigned> &first = s_groupTasksScratch->first;
	tasks.clear();
	for (PolyRenderTaskClass *prt = head; prt; prt = prt->Get_Next_Visible()) {
		if (!Is_Instanceable_Task(prt)) return head;
		tasks.push_back(prt);
	}
	if (tasks.size() < 3) return head;
	first.clear();
	order.clear();
	bool grouped = true;
	PolygonRendererClass *previous = nullptr;
	for (unsigned i = 0; i < tasks.size(); ++i) {
		PolygonRendererClass *renderer = tasks[i]->Peek_Polygon_Renderer();
		auto it = first.emplace(renderer, i).first;
		if (renderer != previous && it->second != i) grouped = false;
		previous = renderer;
		order.emplace_back(it->second, i);
	}
	if (grouped) return head;
	std::stable_sort(order.begin(), order.end(),
		[](const std::pair<unsigned, unsigned> &a, const std::pair<unsigned, unsigned> &b) { return a.first < b.first; });
	for (unsigned i = 0; i + 1 < order.size(); ++i) {
		tasks[order[i].second]->Set_Next_Visible(tasks[order[i + 1].second]);
	}
	tasks[order.back().second]->Set_Next_Visible(nullptr);
	return tasks[order.front().second];
}

void TextureCategoryClass::Render(void)
{

		for (unsigned i=0;i<MeshMatDescClass::MAX_TEX_STAGES;++i) 
		{
			SNAPSHOT_SAY(("Set_Texture(%d,%s)\n",i,Peek_Texture(i) ? (const char *)Peek_Texture(i)->Get_Texture_Name() : "NULL"));
			RenderDevice::Set_Texture(i,Peek_Texture(i));
		}

	SNAPSHOT_SAY(("Set_Material(%s)\n",Peek_Material() ? Peek_Material()->Get_Name() : "NULL"));
	VertexMaterialClass *vmaterial=const_cast<VertexMaterialClass *>(Peek_Material());	//ugly cast from const but we'll restore it after changes so okay. -MW
	RenderDevice::Set_Material(vmaterial);

	SNAPSHOT_SAY(("Set_Shader(%x)\n",Get_Shader().Get_Bits()));
	ShaderClass theShader = Get_Shader();

	//Setup an alpha blend version of this shader just in case it's needed. -MW
	ShaderClass theAlphaShader = theShader;
	theAlphaShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_SRC_ALPHA);
	theAlphaShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE_MINUS_SRC_ALPHA);
	//if we want to allow other translucent polygons behind this mesh, we need to disable z-write but
	//this will cause sorting errors on this mesh.
	//theAlphaShader.Set_Depth_Mask(ShaderClass::DEPTH_WRITE_DISABLE);

	RenderDevice::Set_Shader(theShader);

	if (m_gForceMultiply && theShader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO) {
		theShader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_SRC_COLOR);
		theShader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ZERO);
		RenderDevice::Set_Shader(theShader);
		//VertexMaterialClass *material = VertexMaterialClass::Get_Preset(VertexMaterialClass::PRELIT_DIFFUSE);
		//RenderDevice::Set_Material(material);
		//REF_PTR_RELEASE(material);
		RenderDevice::Apply_Render_State_Changes();
		RenderDevice::Set_Render_State(WW3D_RS_SRCBLEND,WW3D_BLEND_DESTCOLOR);
	}


	bool renderTasksRemaining=false;
	bool gpu_skin_bound=false;

	const bool instancing = RenderDevice::Mesh_Instancing_Enabled() && !m_gForceMultiply &&
		theShader.Get_Src_Blend_Func() == ShaderClass::SRCBLEND_ONE && theShader.Get_Dst_Blend_Func() == ShaderClass::DSTBLEND_ZERO;
	if (instancing) render_task_head = Group_Tasks_By_Renderer(render_task_head);

	PolyRenderTaskClass * prt = render_task_head;
	PolyRenderTaskClass * last_prt = nullptr;

	while (prt) {

		/*
		** Dig out the parameters for this render task
		*/
		PolygonRendererClass * renderer = prt->Peek_Polygon_Renderer();
		MeshClass * mesh = prt->Peek_Mesh();
		
		if (mesh->Get_Base_Vertex_Offset() == VERTEX_BUFFER_OVERFLOW)	//check if this mesh is valid
		{	//skip this mesh so it gets rendered later after vertices are filled in.
			last_prt = prt;
			prt = prt->Get_Next_Visible();
			renderTasksRemaining = true;
			continue;
		}

		SNAPSHOT_SAY(("mesh = %s\n",mesh->Get_Name()));

		// Plain tasks are collected by the backend; anything else first draws what was collected.
		if (instancing) {
			if (Is_Instanceable_Task(prt)) RenderDevice::Begin_Mesh_Instances();
			else RenderDevice::End_Mesh_Instances();
		}

		/*
		** If the user is not installing LightEnvironmentClasses, we leave the lighting render
		** states untouched.  This way they can set a couple global lights that affect the entire scene.
		*/
		LightEnvironmentClass * lenv = mesh->Get_Lighting_Environment();
		if (lenv != nullptr) {
			SNAPSHOT_SAY(("LightEnvironment, lights: %d\n",lenv->Get_Light_Count()));
			RenderDevice::Set_Light_Environment(lenv);
		}
		else {
			SNAPSHOT_SAY(("No light environment\n"));
		}

		/*
		** Support for ALIGNED and ORIENTED camera modes
		*/
		const Matrix3D* world_transform = &mesh->Get_Transform();
		bool identity=mesh->Is_Transform_Identity();
		Matrix3D tmp_world;

		if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ALIGNED)) {
			SNAPSHOT_SAY(("Camera mode ALIGNED\n"));

			Vector3 mesh_position;
			Vector3 camera_z_vector;
			
			TheMeshRenderer.Peek_Camera()->Get_Transform().Get_Z_Vector(&camera_z_vector);
			mesh->Get_Transform().Get_Translation(&mesh_position);

			tmp_world.Obj_Look_At(mesh_position,mesh_position + camera_z_vector,0.0f);
			world_transform = &tmp_world;

		} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::ORIENTED)) {
			SNAPSHOT_SAY(("Camera mode ORIENTED\n"));
		
			Vector3 mesh_position;
			Vector3 camera_position;

			TheMeshRenderer.Peek_Camera()->Get_Transform().Get_Translation(&camera_position);
			mesh->Get_Transform().Get_Translation(&mesh_position);

			tmp_world.Obj_Look_At(mesh_position,camera_position,0.0f);
			world_transform = &tmp_world;
		
		} else if (mesh->Peek_Model()->Get_Flag(MeshModelClass::SKIN)) {
			SNAPSHOT_SAY(("Set world identity (for skin)\n"));
			
			tmp_world.Make_Identity();
			world_transform = &tmp_world;
			identity=true;
		}


		// GPU skins: the model's static buffer and this mesh's bone palette; back to the dynamic buffer
		// of the CPU skins for the next mesh.
		if (mesh->Is_GPU_Skin()) {
			mesh->Bind_GPU_Skin();
			gpu_skin_bound = true;
		}
		else if (gpu_skin_bound) {
			Unbind_GPU_Skin();
			gpu_skin_bound = false;
		}

		if (identity) {
			SNAPSHOT_SAY(("Set_World_Identity\n"));
			RenderDevice::Set_World_Identity();
		}
		else {
			SNAPSHOT_SAY(("Set_World_Transform\n"));
			RenderDevice::Set_Transform(WW3D_TS_WORLD,*world_transform);
		}

		
//--------------------------------------------------------------------
		if (mesh->Get_ObjectScale() != 1.0f)
			RenderDevice::Set_Render_State(WW3D_RS_NORMALIZENORMALS, TRUE);
//--------------------------------------------------------------------
		/*
		** Render mesh using either sorting or immediate pipeline
		*/
		//(gth) this if statement's contents are not tabbed to avoid perforce merge problems...
		if (!MeshRendererDebugger::Is_Enabled() || !mesh->Is_Disabled_By_Debugger()) {

		if ((!!mesh->Peek_Model()->Get_Flag(MeshGeometryClass::SORT)) && WW3D::Is_Sorting_Enabled()) {
			renderer->Render_Sorted(mesh->Get_Base_Vertex_Offset(),mesh->Get_Bounding_Sphere());
		} else {
			//non-transparent mesh that will be rendered immediately.  Okay to adjust the shader/material
			//if necessary
			if (mesh->Get_Alpha_Override() != 1.0 || (mesh->Get_User_Data() && *static_cast<int *>(mesh->Get_User_Data()) == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE))
			{	//mesh has material override of some kind
				//adjust the opacity of this model
				float oldOpacity=vmaterial->Get_Opacity();
				Vector3 oldDiffuse;
				Vector2 oldUVOffset;
				unsigned int oldUVOffsetSyncTime = 0;	// only read when oldMapper is set (C4701)
				vmaterial->Get_Diffuse(&oldDiffuse);
				LinearOffsetTextureMapperClass *oldMapper=static_cast<LinearOffsetTextureMapperClass *>(vmaterial->Peek_Mapper());
				if ( mesh->Get_User_Data() && *static_cast<int *>(mesh->Get_User_Data()) == RenderObjClass::USER_DATA_MATERIAL_OVERRIDE && oldMapper && oldMapper->Mapper_ID() == TextureMapperClass::MAPPER_ID_LINEAR_OFFSET)
				{	RenderObjClass::Material_Override *matOverride=static_cast<RenderObjClass::Material_Override *>(mesh->Get_User_Data());
					oldUVOffsetSyncTime = oldMapper->Get_LastUsedSyncTime();
					oldMapper->Set_LastUsedSyncTime(WW3D::Get_Sync_Time());	//make sure zero time passes for the mapper.
					oldMapper->Get_Current_UV_Offset(oldUVOffset);
					oldMapper->Set_Current_UV_Offset(matOverride->customUVOffset);
				}
				else
					oldMapper=nullptr;
				if (mesh->Get_Alpha_Override() != 1.0)
				{
					if (mesh->Is_Additive())
					{	//additvie blended mesh can't switch to alpha or we will get a black outline.
						//so adjust diffuse color instead.
						vmaterial->Set_Diffuse(mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override(),mesh->Get_Alpha_Override());
						theAlphaShader = theShader;	//keep using additive blending.
					}
					vmaterial->Set_Opacity(mesh->Get_Alpha_Override());
					RenderDevice::Set_Shader(theAlphaShader);
					RenderDevice::Apply_Render_State_Changes();
					RenderDevice::Set_Render_State(WW3D_RS_ALPHAREF,static_cast<int>(static_cast<float>(0x60)*mesh->Get_Alpha_Override()));
					renderer->Render(mesh->Get_Base_Vertex_Offset());
					RenderDevice::Set_Render_State(WW3D_RS_ALPHAREF,0x60);
					vmaterial->Set_Opacity(oldOpacity);	//restore previous value
					vmaterial->Set_Diffuse(oldDiffuse.X,oldDiffuse.Y,oldDiffuse.Z);
					RenderDevice::Set_Shader(theShader);	//restore previous value
				}
				else
					renderer->Render(mesh->Get_Base_Vertex_Offset());

				if (oldMapper)	//did we override the uv offset?
				{	oldMapper->Set_LastUsedSyncTime(oldUVOffsetSyncTime);
					oldMapper->Set_Current_UV_Offset(oldUVOffset);
				}
				RenderDevice::Set_Material(nullptr);	//force a reset of vertex material since we secretly changed opacity
				RenderDevice::Set_Material(vmaterial);	//restore previous material.
			} 
			else
				renderer->Render(mesh->Get_Base_Vertex_Offset());
		}
//--------------------------------------------------------------------
		if (mesh->Get_ObjectScale() != 1.0f)
			RenderDevice::Set_Render_State(WW3D_RS_NORMALIZENORMALS, FALSE);
//--------------------------------------------------------------------




        } // (gth) non-tabbed to aviod per-force merge problems...

		/*
		** Move to the next render task.  Note that the delete should be fast because prt's are pooled
		*/
		PolyRenderTaskClass * next_prt = prt->Get_Next_Visible();

		// remove from list, then delete
		if (last_prt == nullptr) {
		   render_task_head = next_prt;
		} else {
		  last_prt->Set_Next_Visible(next_prt);
		}

		delete prt;
		prt = next_prt;
	}
	if (instancing) RenderDevice::End_Mesh_Instances();
	if (gpu_skin_bound) {
		Unbind_GPU_Skin();
	}

	if (!renderTasksRemaining)
	{	WWASSERT(!render_task_head);
		Clear_Render_List();
	}
}


MeshRendererClass::MeshRendererClass()
	:
	camera(nullptr),
	enable_lighting(true),
	texture_category_container_list_skin(nullptr),
	visible_decal_meshes(nullptr)
{
}

MeshRendererClass::~MeshRendererClass()
{
	Invalidate(true);
	Clear_Pending_Delete_Lists();
	if (texture_category_container_list_skin != nullptr) {
		delete texture_category_container_list_skin;
	}
}

void MeshRendererClass::Init(void)
{
	// DMS - Only allocate one if we havent already (leak fix)
	if(!texture_category_container_list_skin)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;
}

void MeshRendererClass::Shutdown(void)
{
	Invalidate(true);
	Clear_Pending_Delete_Lists();
	_TempVertexBuffer.Clear();	//free memory
	_TempNormalBuffer.Clear();
	delete s_groupTasksScratch;
	s_groupTasksScratch = nullptr;

	// Render tasks come from static pools whose blocks would otherwise only die after the memory
	// report at the end of WinMain and show up as leaks. Memory is freed only if no task is alive.
	PolyRenderTaskClass::Free_Unused_Pool_Memory();
	MatPassTaskClass::Free_Unused_Pool_Memory();
}

// ----------------------------------------------------------------------------

void MeshRendererClass::Clear_Pending_Delete_Lists()
{
	while (TextureCategoryClass* category=texture_category_delete_list.Remove_Head()) {
		delete category;
	}
	while (FVFCategoryContainer* container=fvf_category_container_delete_list.Remove_Head()) {
		delete container;
	}
}

// ----------------------------------------------------------------------------

static void Add_Rigid_Mesh_To_Container(FVFCategoryList* container_list,unsigned fvf,MeshModelClass* mmc)
{
	WWASSERT(container_list);
	FVFCategoryContainer * container = nullptr;
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE));

	FVFCategoryListIterator it(container_list);
	while (!it.Is_Done()) {
		container = it.Peek_Obj();
		if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
			container->Add_Mesh(mmc);
			return;
		}
		it.Next();
	}

	container=W3DNEW RigidFVFCategoryContainer(fvf,sorting);
	container_list->Add_Tail(container);
	container->Add_Mesh(mmc);
}

// ----------------------------------------------------------------------------

void MeshRendererClass::Unregister_Mesh_Type(MeshModelClass* mmc)
{
	while (PolygonRendererClass* n=mmc->PolygonRenderers.Remove_Head()) {
		delete n;
	}
	_RegisteredMeshList.Remove(mmc);

	// Also remove the gap filler!
	if (mmc->GapFiller) {
		GapFillerClass* gf=mmc->GapFiller;
		mmc->GapFiller=nullptr;
		delete gf;
	}

}


void MeshRendererClass::Register_Mesh_Type(MeshModelClass* mmc)
{
#ifdef ENABLE_CATEGORY_LOG
	WWDEBUG_SAY(("Registering mesh: %s (%d polys, %d verts + %d gap polygons)\n",mmc->Get_Name(),mmc->Get_Polygon_Count(),mmc->Get_Vertex_Count(),mmc->Get_Gap_Filler_Polygon_Count()));
#endif
	bool skin=(mmc->Get_Flag(MeshModelClass::SKIN) && mmc->VertexBoneLink);
	bool sorting=((!!mmc->Get_Flag(MeshModelClass::SORT)) && WW3D::Is_Sorting_Enabled() && (mmc->Get_Sort_Level() == SORT_LEVEL_NONE));

	if (skin) {

		/*
		** This mesh is a skin.  Add it to a SkinFVFCategoryContainer.
		*/
		WWASSERT(texture_category_container_list_skin);

		FVFCategoryListIterator it(texture_category_container_list_skin);
		while (!it.Is_Done()) {
			FVFCategoryContainer * container = it.Peek_Obj();
			if (sorting==container->Is_Sorting() && container->Check_If_Mesh_Fits(mmc)) {
				container->Add_Mesh(mmc);
				return;
			}
			it.Next();
		}

		FVFCategoryContainer * new_container=W3DNEW SkinFVFCategoryContainer(sorting);
		texture_category_container_list_skin->Add_Tail(new_container);
		new_container->Add_Mesh(mmc);
	
	} else {

		/*
		** We should never try to add the same mesh model to the system twice.
		*/
		WWASSERT_PRINT(_RegisteredMeshList.Contains(mmc) == false,("Mesh name: %s",mmc->Get_Name()));

		/*
		** If the previous step didn't add the mesh, then we have to actually process this mesh
		*/
		if (!_RegisteredMeshList.Contains(mmc)) {

			unsigned fvf=FVFCategoryContainer::Define_FVF(mmc,enable_lighting);

			/*
			** Search for an existing FVF Category Container that matches this mesh
			*/
			int i;
			for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
				FVFCategoryList * list=texture_category_container_lists_rigid[i];
				WWASSERT(list);
				FVFCategoryContainer * container=list->Peek_Head();
				if (container && container->Get_FVF()!=fvf) continue;

				Add_Rigid_Mesh_To_Container(list,fvf,mmc);
				break;
			}

			if (i==texture_category_container_lists_rigid.Count()) {

				/*
				** We couldn't find an existing FVF category container so we have to add one.  Future
				** meshes that use this FVF will also be able to use this container.
				*/
				FVFCategoryList * new_fvf_category = W3DNEW FVFCategoryList();
				texture_category_container_lists_rigid.Add(new_fvf_category);
				Add_Rigid_Mesh_To_Container(new_fvf_category,fvf,mmc);
			}

			/*
			** Done processing the mesh, add its polygon renderers to the global registered mesh list
			*/
			if (mmc->PolygonRenderers.Is_Empty() == false) {
				_RegisteredMeshList.Add_Tail(mmc);
			}
			else {
				WWDEBUG_SAY(("Error: Register_Mesh_Type failed! file: %s line: %d\r\n",__FILE__,__LINE__));
			}
		}
	}

	return;
}

static unsigned statistics_requested=0;

void MeshRendererClass::Request_Log_Statistics()
{
	statistics_requested=WW3D::Get_Frame_Count();
}


// ---------------------------------------------------------------------------
//
// Render all meshes that are added to visible lists
//
// ---------------------------------------------------------------------------

static void Render_FVF_Category_Container_List(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render();
		it.Next();
	}
}

static void Render_FVF_Category_Container_List_Delayed_Passes(FVFCategoryList& list)
{
	FVFCategoryListIterator it(&list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Render_Delayed_Procedural_Material_Passes();
		it.Next();
	}
}

void MeshRendererClass::Flush(void)
{
	int i;

	WWPROFILE("MeshRenderer::Flush");
	if (!camera) return;
	Log_Statistics_String(true);	

	/*
	** Render the FVF categories.  Note that it is critical that skins be 
	** rendered *after* the rigid meshes.  This is caused by the fact that an object may
	** have its base passes disabled and a translucent procedural material pass rendered
	** instead.  In this case, technically we have to delay rendering of the material pass but
	** for skins we just render these passes as we go because we can assume that the
	** bulk of the meshes have already been drawn (there would be extra overhead involved
	** in solving this for skins)
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
	}

	Render_FVF_Category_Container_List(*texture_category_container_list_skin);

	Render_Decal_Meshes();

	/*
	** Render the translucent procedural material passes that were applied to meshes that
	** had their base passes disabled. 
	*/
	for (i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Render_FVF_Category_Container_List_Delayed_Passes(*texture_category_container_lists_rigid[i]);
	}

	RenderDevice::Set_Vertex_Buffer(nullptr);
	RenderDevice::Set_Index_Buffer(nullptr,0);
}


void MeshRendererClass::Add_To_Render_List(DecalMeshClass * decalmesh)
{
	WWASSERT(decalmesh != nullptr);
	decalmesh->Set_Next_Visible(visible_decal_meshes);
	visible_decal_meshes = decalmesh;
}

void MeshRendererClass::Render_Decal_Meshes(void)
{
	DecalMeshClass * decal_mesh = visible_decal_meshes;
	if (!decal_mesh) return;

	// Z bias level 8 expressed as WW3D_RS_DEPTHBIAS.
	RenderDevice::Set_Render_State(WW3D_RS_DEPTHBIAS,RenderDevice::Convert_ZBias_Level(8));

	while (decal_mesh != nullptr) {
		decal_mesh->Render();
		decal_mesh = decal_mesh->Peek_Next_Visible();
	}
	visible_decal_meshes = nullptr;

	RenderDevice::Set_Render_State(WW3D_RS_DEPTHBIAS,RenderDevice::Convert_ZBias_Level(0));
}

// ----------------------------------------------------------------------------

static void Log_Container_List(FVFCategoryList& container_list,bool only_visible)
{
	FVFCategoryListIterator it(&container_list);
	while (!it.Is_Done()) {
		it.Peek_Obj()->Log(only_visible);
		it.Next();
	}
}

void MeshRendererClass::Log_Statistics_String(bool only_visible)
{
	if (statistics_requested!=WW3D::Get_Frame_Count()) return;

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Log_Container_List(*texture_category_container_lists_rigid[i],only_visible);
	}
	Log_Container_List(*texture_category_container_list_skin,only_visible);

}

static void Invalidate_FVF_Category_Container_List(FVFCategoryList& list)
{
	while (FVFCategoryContainer* fvf_category=list.Remove_Head()) {
		delete fvf_category;
	}
}

void MeshRendererClass::Invalidate( bool shutdown)
{
	_RegisteredMeshList.Reset_List();

	for (int i=0;i<texture_category_container_lists_rigid.Count();++i) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_lists_rigid[i]);
		delete texture_category_container_lists_rigid[i];
	}
	if (texture_category_container_list_skin) {
		Invalidate_FVF_Category_Container_List(*texture_category_container_list_skin);
		delete texture_category_container_list_skin;
		texture_category_container_list_skin=nullptr;
	}

	if (!shutdown)
		texture_category_container_list_skin = W3DNEW FVFCategoryList;

	texture_category_container_lists_rigid.Delete_All();
}







