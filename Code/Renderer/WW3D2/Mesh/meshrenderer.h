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
 *                     $Archive:: /Commando/Code/ww3d2/meshrenderer.h                          $*
 *                                                                                             *
 *              Original Author:: Jani Penttinen                                               *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               * 
 *                                                                                             * 
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 29                                                          $*
 *                                                                                             *
 * 06/27/02 KM Changes to max texture stage caps																*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */


#if defined(_MSC_VER)
#pragma once
#endif

#ifndef MESHRENDERER_H
#define MESHRENDERER_H

#include "always.h"
#include "wwstring.h"
#include "simplevec.h"
#include "refcount.h"
#include "Vector.H"
#include "meshrendererlist.h"
#include "shader.h"
#include "WW3D2/Common/renderdevice.h"
#include "meshmatdesc.h"

class IndexBufferClass;
class VertexBufferClass;
class RenderTypeArrayClass;
class MeshClass;
class MeshModelClass;
class PolygonRendererClass;
class Vertex_Split_Table;
class FVFCategoryContainer;
class DecalMeshClass;
class MaterialPassClass;
class MatPassTaskClass;
class PolyRenderTaskClass;
class TextureClass;
class VertexMaterialClass;
class CameraClass;

#define RECORD_RENDER(I,P) render_stats[I].count++; render_stats[I].polys+=P;

/**
** TextureCategoryClass
** This class is used for each Material-Texture-Shader combination that is encountered during rendering.
** Each polygon_renderer that uses the same 'TextureCategory' will be linked to the 'TextureCategory' object.
** Then, all polygons will be rendered in 'TextureCategory' batches to reduce the number of stage changes
** (and most importantly, texture changes) that we cause in the device.
*/
class TextureCategoryClass : public MultiListObjectClass
{
	int												pass;
	TextureClass *									textures[MeshMatDescClass::MAX_TEX_STAGES];
	ShaderClass										shader;
	VertexMaterialClass *						material;					
	PolygonRendererList						PolygonRenderers;
	FVFCategoryContainer*					container;	

	PolyRenderTaskClass *						render_task_head;			// polygon renderers queued for rendering
	static bool											m_gForceMultiply;  // Forces opaque materials to use the multiply blend - pseudo transparent effect.  jba.

public:

	TextureCategoryClass(FVFCategoryContainer* container,TextureClass** textures, ShaderClass shd, VertexMaterialClass* mat,int pass);
	~TextureCategoryClass() override;

	void									Add_Render_Task(PolygonRendererClass * p_renderer,MeshClass * p_mesh);

	void									Render(void);
	bool									Anything_To_Render() { return (render_task_head != nullptr); }
	void									Clear_Render_List() { render_task_head = nullptr; }

	TextureClass *						Peek_Texture(int stage)	{ return textures[stage]; }
	const VertexMaterialClass *	Peek_Material() { return material; }	
	ShaderClass							Get_Shader() { return shader; }

	PolygonRendererList&			Get_Polygon_Renderer_List() { return PolygonRenderers; }

	unsigned Add_Mesh(
		Vertex_Split_Table& split_buffer,
		unsigned vertex_offset,
		unsigned index_offset,
		IndexBufferClass* index_buffer,
		unsigned pass);
	void Log(bool only_visible);

	void Remove_Polygon_Renderer(PolygonRendererClass* p_renderer);
	void Add_Polygon_Renderer(PolygonRendererClass* p_renderer,PolygonRendererClass* add_after_this=nullptr);
	

	FVFCategoryContainer * Get_Container(void) { return container; }

	// Force multiply blend on all objects inserted from now on. (Doesn't affect the objects that are already in the lists)
	static void						SetForceMultiply(bool multiply) { m_gForceMultiply=multiply; }

};

// ----------------------------------------------------------------------------

/**
** FVFCategoryContainer
*/

class FVFCategoryContainer : public MultiListObjectClass
{
public:
	enum {
		MAX_PASSES=4
	};
protected:

	TextureCategoryList									texture_category_list[MAX_PASSES];
	TextureCategoryList									visible_texture_category_list[MAX_PASSES];
	
	MatPassTaskClass *									visible_matpass_head;
	MatPassTaskClass *									visible_matpass_tail;
	
	IndexBufferClass *									index_buffer;
	int														used_indices;
	unsigned													FVF;
	unsigned													passes;
	unsigned													uv_coordinate_channels;
	bool														sorting;
	bool														AnythingToRender;
	bool														AnyDelayedPassesToRender;

	void Generate_Texture_Categories(Vertex_Split_Table& split_table,unsigned vertex_offset);
	void Insert_To_Texture_Category(
		Vertex_Split_Table& split_table,
		TextureClass** textures,
		VertexMaterialClass* mat,
		ShaderClass shader,
		int pass,
		unsigned vertex_offset);

	inline bool Anything_To_Render()					{ return AnythingToRender; }
	inline bool Any_Delayed_Passes_To_Render()	{ return AnyDelayedPassesToRender; }

	void Render_Procedural_Material_Passes(void);

	TextureCategoryClass* Find_Matching_Texture_Category(
		TextureClass* texture,
		unsigned pass,
		unsigned stage,
		TextureCategoryClass* ref_category);

	TextureCategoryClass* Find_Matching_Texture_Category(
		VertexMaterialClass* vmat,
		unsigned pass,		
		TextureCategoryClass* ref_category);

public:
	
	FVFCategoryContainer(unsigned FVF,bool sorting);
	~FVFCategoryContainer() override;

	static unsigned Define_FVF(MeshModelClass* mmc,bool enable_lighting);
	bool Is_Sorting() const { return sorting; }

	void Change_Polygon_Renderer_Texture(
		PolygonRendererList& polygon_renderer_list,
		TextureClass* texture,
		TextureClass* new_texture,
		unsigned pass,
		unsigned stage);

	void Change_Polygon_Renderer_Material(
		PolygonRendererList& polygon_renderer_list,
		VertexMaterialClass* vmat,
		VertexMaterialClass* new_vmat,
		unsigned pass);

	void Remove_Texture_Category(TextureCategoryClass* tex_category);

	virtual void Render(void)=0;
	virtual void Add_Mesh(MeshModelClass* mmc)=0;
	virtual void Log(bool only_visible)=0;
	virtual bool Check_If_Mesh_Fits(MeshModelClass* mmc)=0;

	inline unsigned Get_FVF() const { return FVF; }
	
	inline void Add_Visible_Texture_Category(TextureCategoryClass * tex_category,int pass) 
	{
		WWASSERT(pass<MAX_PASSES);
		WWASSERT(tex_category != nullptr);
		WWASSERT(texture_category_list[pass].Contains(tex_category));
		visible_texture_category_list[pass].Add(tex_category);
		AnythingToRender=true;
	}

	/*
	** Material pass rendering.  The following two functions allow procedural material passes
	** to be applied to meshes in this FVF category.  In certain cases, the game will *only* render
	** the procedural pass and not the base materials for the mesh.  When this happens there can
	** be rendering errors unless these procedural passes are rendered after all of the meshes in 
	** the scene.  The virtual method Add_Delayed_Material_Pass is used in this case.  
	*/
	void Add_Visible_Material_Pass(MaterialPassClass * pass,MeshClass * mesh);
	virtual void Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh) = 0;
	virtual void Render_Delayed_Procedural_Material_Passes(void) = 0;
};


/**
** RigidFVFCategoryContainer
** This is an FVFCategoryContainer for rigid (non-skin) meshes
*/
class RigidFVFCategoryContainer : public FVFCategoryContainer
{
public:
	RigidFVFCategoryContainer(unsigned FVF,bool sorting);
	~RigidFVFCategoryContainer() override;

	void Add_Mesh(MeshModelClass* mmc) override;
	void Log(bool only_visible) override;
	bool Check_If_Mesh_Fits(MeshModelClass* mmc) override;

	void Render(void) override;	// Generic render function

	/* 
	** This method adds a material pass which must be rendered after all of the other rendering is complete. 
	** This is needed whenever a mesh turns off its base passes and renders a translucent pass on its geometry.
	*/
	void Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh) override;
	void Render_Delayed_Procedural_Material_Passes(void) override;

protected:


	VertexBufferClass *	vertex_buffer;
	int						used_vertices;

	MatPassTaskClass *	delayed_matpass_head;
	MatPassTaskClass *	delayed_matpass_tail;

};


/**
** SkinFVFCategoryContainer
** This is an FVFCategoryContainer for skin meshes
*/
class SkinFVFCategoryContainer: public FVFCategoryContainer
{
public:
	SkinFVFCategoryContainer(bool sorting);
	~SkinFVFCategoryContainer() override;

	void Render(void) override;
	void Add_Mesh(MeshModelClass* mmc) override;
	void Log(bool only_visible) override;
	bool Check_If_Mesh_Fits(MeshModelClass* mmc) override;

	void Add_Visible_Skin(MeshClass * mesh);

	/// Skins with a GPU skin buffer (MeshModelClass::Get_GPU_Skin) are deformed by the vertex shader instead
	/// of the CPU. On by default; off for comparisons (-cpuSkinning).
	static void Enable_GPU_Skinning(bool enable) { GPUSkinning = enable; }
	static bool Is_GPU_Skinning_Enabled(void) { return GPUSkinning; }
	/// The dynamic buffer of the CPU skins while the texture categories render (nullptr: none).
	static DynamicVBAccessClass * Peek_CPU_Skin_Buffer(void) { return CPUSkinBuffer; }

	/* 
	** Since skins are already rendered after the rigid meshes, the Add_Delayed_Material_Pass function simply
	** routes into the Add_Visible_Material_Pass method and no extra overhead is added.
	*/
	void Add_Delayed_Visible_Material_Pass(MaterialPassClass * pass, MeshClass * mesh) override { Add_Visible_Material_Pass(pass,mesh); }
	void Render_Delayed_Procedural_Material_Passes(void) override { }

private:

	void Reset();
 	void clearVisibleSkinList();

	bool Has_Material_Pass(MeshClass * mesh);

	unsigned int								VisibleVertexCount;
	MeshClass *									VisibleSkinHead;
	MeshClass *									VisibleSkinTail;

	static bool									GPUSkinning;
	static DynamicVBAccessClass *			CPUSkinBuffer;

};



/**
** MeshRendererClass
** This object is controller for the entire mesh rendering system.  It organizes mesh
** fragments into groups based on FVF, texture, and material.  During rendering, a list of 
** the visible mesh fragments is composed and rendered.  There is a global instance of this
** class called TheMeshRenderer that should be used for all mesh rendering.
*/
class MeshRendererClass
{
public:
	MeshRendererClass();
	~MeshRendererClass();

	void						Init();
	void						Shutdown();

	void						Flush();
	void						Clear_Pending_Delete_Lists();

	void						Log_Statistics_String(bool only_visible);
	static void				Request_Log_Statistics();

	void						Register_Mesh_Type(MeshModelClass* mmc);
	void						Unregister_Mesh_Type(MeshModelClass* mmc);
	void						Set_Camera(CameraClass* cam) { camera=cam; }
	CameraClass *			Peek_Camera(void)	{ return camera; }
	void						Add_To_Render_List(DecalMeshClass * decalmesh);

	// Enable or disable lighting on all objects inserted from now on. (Doesn't affect the objects that are already in the lists)
	void						Enable_Lighting(bool enable) { enable_lighting=enable; }

	// This should be called at the beginning of a game or menu or after a major modifications to the scene...
	void						Invalidate(bool shutdown=false);	// Added flag so it doesn't allocate more mem when shutting down. -MW

protected:

	void Render_Decal_Meshes(void);

	bool													enable_lighting;
	CameraClass *										camera;

	SimpleDynVecClass<FVFCategoryList *>		texture_category_container_lists_rigid;
	FVFCategoryList *									texture_category_container_list_skin;

	DecalMeshClass *									visible_decal_meshes;


};

extern MeshRendererClass TheMeshRenderer;

#endif
