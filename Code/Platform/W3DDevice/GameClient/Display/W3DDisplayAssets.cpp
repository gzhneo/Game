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

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include <stdlib.h>
#include <windows.h>
#include <io.h>
#include <time.h>
#include "Common/ThingFactory.h"
#include "formatinto.h"
#include "W3DDevice/GameClient/W3DParticleSys.h"
#include "Engine/Clock.h"
#include "Common/GameEngine.h"
#include "Common/GameEdition.h"	// Generals-specific shader and light settings
#include "Common/GlobalData.h"
#include "Common/PerfTimer.h"
#include "Common/FileSystem.h"
#include "Common/LocalFileSystem.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/GameLOD.h"
#include "Common/DrawModule.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameClient/CameraPath.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameText.h"
#include "GameClient/Line2D.h"
#include "GameClient/Mouse.h"
#include "GameClient/GlobalLanguage.h"
#include "GameClient/Water.h"
#include "GameNetwork/NetworkInterface.h"
#include "Common/ModelState.h"
#include "Lib/BaseType.h"
#include "W3DDevice/Common/W3DConvert.h"
#include "W3DDevice/GameClient/W3DAssetManager.h"
#include "W3DDevice/GameClient/camerashakesystem.h"
#include "W3DDevice/GameClient/W3DGameClient.h"
#include "W3DDevice/GameClient/W3DFileSystem.h"
#include "W3DDevice/GameClient/W3DDynamicLight.h"
#include "W3DDevice/GameClient/HeightMap.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "W3DDevice/GameClient/W3DScene.h"
#include "W3DDevice/GameClient/W3DTerrainTracks.h"
#include "W3DDevice/GameClient/W3DWater.h"
#include "W3DDevice/GameClient/W3DVideobuffer.h"
#include "W3DDevice/GameClient/W3DShaderManager.h"
#include "W3DDevice/GameClient/W3DDebugDisplay.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "WWMath/wwmath.h"
#include "WW3D2/System/ww3d.h"
#include "WW3D2/Mesh/predlod.h"
#include "WW3D2/Effects/part_emt.h"
#include "WW3D2/Effects/part_ldr.h"
#include "WW3D2/Common/rendercaps.h"
#include "WW3D2/Mesh/meshrenderer.h"
#include "WW3D2/Effects/pointgr.h"
#include "WW3D2/Material/ww3dformat.h"
#include "WW3D2/Animation/agg_def.h"
#include "WW3D2/Render2D/render2dsentence.h"
#include "WW3D2/Buffers/sortingrenderer.h"
#include "WW3D2/Material/textureloader.h"
#include "WW3D2/Mesh/mesh.h"
#include "WW3D2/Mesh/hlod.h"
#include "WW3D2/Material/meshmatdesc.h"
#include "WW3D2/Mesh/meshmdl.h"
#include "WW3D2/Scene/rddesc.h"
#include "TARGA.H"
#include "Lib/BaseType.h"
#include "GameLogic/ScriptEngine.h"		// For TheScriptEngine - jkmcd
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"	// shroud status of light positions (dynamic lights)
#include "Win32Device/Common/Win32AppWindow.h"
#include "GameClient/GameClient.h"	// isRenderOnlyFrame()
#include "Engine/ThreadRole.h"
#include "Lib/SafeString.h"
#include <cstdarg>
#include "formatinto.h"
#include "Engine/UserDataFiles.h"

// W3DDisplay: screenshots and assets -- writing images, dumping, preloading and purging assets

// Write the measured GPU times of the render effects as one line to the runner log (GPUEFFEKTE), what the
// device built behind the scenes (VULKANPIPELINES) and shaders that did not compile (SHADERFEHLER). Lines without
// content are left out.
void W3DDisplay::writeRenderStatistics(void)
{
	char line[2048];
	RenderDevice::Get_Effect_Statistics( line, sizeof(line) );
	if( line[0] )
	{
		GameEngine_RunnerLog( line );
		DEBUG_LOG(( "D3D11 effects: %s\n", line ));
	}
	RenderDevice::Get_Device_Statistics( line, sizeof(line) );
	if( line[0] )
	{
		GameEngine_RunnerLog( line );
		DEBUG_LOG(( "Render device: %s\n", line ));
	}
	RenderDevice::Get_Shader_Failures( line, sizeof(line) );
	if( line[0] )
	{
		GameEngine_RunnerLog( line );
		DEBUG_LOG(( "Render device: %s\n", line ));
	}
	if( TheParticleSystemManager )
	{
		static_cast<W3DParticleSystemManager *>(TheParticleSystemManager)->formatCpuTimes( line, sizeof(line) );
		if( line[0] )
			GameEngine_RunnerLog( line );
	}
}

///Save Screen Capture to a file
void W3DDisplay::takeScreenShot(void)
{
	char leafname[256];
	char pathname[1024];

	static int frame_number = 1;

	Bool done = false;
	while (!done) {
		WWFormat::Into(leafname, "{}{:03d}.bmp", "sshot", frame_number++);
		safe_strcpy(pathname, TheGlobalData->getPath_UserData().str());
		safe_strcat(pathname, leafname);
		if (!::Engine::UserDataFiles::exists(::Engine::pathFromNarrow(pathname)))
			done = true;
	}

	// Saved at the next present from the finished frame (3D scene, post-processing, 2D UI). Read now, in the
	// middle of the frame, the scene target has no UI yet.
	RenderDevice::Request_Back_Buffer_Capture(pathname);
	m_screenShotLeaf = leafname;
}

// After the captured frame, so the message itself is not on the screenshot.
void W3DDisplay::showScreenShotMessage()
{
	if (m_screenShotLeaf.isEmpty() || TheInGameUI == nullptr)
		return;
	UnicodeString ufileName;
	ufileName.translate(m_screenShotLeaf);
	m_screenShotLeaf.clear();
	TheInGameUI->message(TheGameText->fetch("GUI:ScreenCapture"), ufileName.str());
}

#if defined(_DEBUG)

static FILE *AssetDumpFile=nullptr;

void dumpMeshAssets(MeshClass *mesh)
{
	if (mesh)
	{
		TextureClass *texture;
		//MaterialInfoClass	*material = mesh->Get_Material_Info();
		MeshModelClass *model=mesh->Get_Model();
		for (int stage=0;stage<MeshMatDescClass::MAX_TEX_STAGES;++stage)
		{
			for (int pass=0;pass<model->Get_Pass_Count();++pass) 
			{
				if (model->Has_Texture_Array(pass,stage))
				{
					for (int i=0;i<model->Get_Polygon_Count();++i)
					{
						if ((texture=model->Peek_Texture(i,pass,stage)) != nullptr)
						{
							// Get_Texture_Name returns StringClass, not const char*;
							// passing a class object through the ellipsis is undefined.
							fprintf(AssetDumpFile,"\t%s\n",texture->Get_Texture_Name().Peek_Buffer());
						}
					}
				}
				else
				{
					if ((texture=model->Peek_Single_Texture(pass,stage)) != nullptr)
					{
						// Get_Texture_Name returns StringClass, not const char*;
						// passing a class object through the ellipsis is undefined.
						fprintf(AssetDumpFile,"\t%s\n",texture->Get_Texture_Name().Peek_Buffer());
					}
				}
			}
		}
	}
}

void dumpHLODAssets(HLodClass *hlod)
{
	if (hlod)
	{
		//model composed of multiple meshes.
		for (Int i=0; i<hlod->Get_Num_Sub_Objects(); i++)
		{
			RenderObjClass *subObj=hlod->Get_Sub_Object(i);
			if (subObj->Class_ID() == RenderObjClass::CLASSID_HLOD)
				dumpHLODAssets(static_cast<HLodClass *>(subObj));
			else
			if (subObj->Class_ID() == RenderObjClass::CLASSID_MESH)
				dumpMeshAssets(static_cast<MeshClass *>(subObj));
		}
	}
}

//-------------------------------------------------------------------------------------------------
/**  dump all used models/textures to a file.*/
//-------------------------------------------------------------------------------------------------
void W3DDisplay::dumpModelAssets(const char *path)
{
	if (m_3DScene)
	{	
		AssetDumpFile=fopen(path,"w");
		if (AssetDumpFile)
		{
			fprintf(AssetDumpFile,"Models and Textures used on %s:\n\n",TheGlobalData->m_mapName.str());
			SceneIterator *sceneIter = m_3DScene->Create_Iterator();
			sceneIter->First();
			while(!sceneIter->Is_Done())
			{
				RenderObjClass * robj = sceneIter->Current_Item();
				if (robj->Class_ID() == RenderObjClass::CLASSID_HLOD)
				{	fprintf(AssetDumpFile,"%s.W3D:\n",robj->Get_Name());
					dumpHLODAssets(static_cast<HLodClass *>(robj));
				}
				else
				if (robj->Class_ID() == RenderObjClass::CLASSID_MESH)
				{	fprintf(AssetDumpFile,"%s.W3D:\n",robj->Get_Name());
					dumpMeshAssets(static_cast<MeshClass *>(robj));
				}
				sceneIter->Next();
			}
			m_3DScene->Destroy_Iterator(sceneIter);
			fclose(AssetDumpFile);
		}
	}
}
#endif	//only include above code in debug and internal

//-------------------------------------------------------------------------------------------------
/** Preload using the W3D asset manager the model referenced by the string parameter */
//-------------------------------------------------------------------------------------------------
void W3DDisplay::preloadModelAssets( AsciiString model )
{

	if( m_assetManager )
	{
		AsciiString nameWithExtension;

		nameWithExtension.format( "%s.w3d", model.str() );
		m_assetManager->Load_3D_Assets( nameWithExtension.str() );

	}  // end if

}  // end preloadModelAssets

//-------------------------------------------------------------------------------------------------
/** Preload using the W3D asset manager the texture referenced by the string parameter */
//-------------------------------------------------------------------------------------------------
void W3DDisplay::preloadTextureAssets( AsciiString texture )
{

	if( m_assetManager )
	{
		TextureClass *theTexture = m_assetManager->Get_Texture( texture.str() );
		theTexture->Release_Ref();//release reference
	}  // end if

}  // end preloadModelAssets

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DDisplay::doSmartAssetPurgeAndPreload(const char* usageFileName)
{
	if (!m_assetManager || !usageFileName || !*usageFileName)
		return;

	DynamicVectorClass<StringClass> names(8000);

	// use TheFileSystem here so we can bigify these files
	File* f = TheFileSystem->openFile(usageFileName, File::READ | File::TEXT);
	if (f)
	{
		for (;;)
		{
			AsciiString tmp;
			if (f->scanString(tmp) == FALSE)
				break;

			// allow for comments in the file. Note that this doesn't allow for comments
			// with spaces! doh. oh well. better than nothing.
			if (tmp.str()[0] == ';')
				continue;

			names.Add(StringClass(tmp.str()));
		}
		f->close();
	}

	// just free everything if there's no exclusion list file (send in an empty list)
	m_assetManager->Free_Assets_With_Exclusion_List(names);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
#if defined(_DEBUG)
void W3DDisplay::dumpAssetUsage(const char* mapname)
{
	if (!m_assetManager || !mapname || !*mapname)
		return;

	DynamicVectorClass<StringClass> names(8000);
	m_assetManager->Create_Asset_List(names);

	const char* leafname = strrchr(mapname, '\\');
	if (leafname)
		++leafname;					// point to first character after the last backslash
	else
		leafname = mapname;		// point to the start of the filename

	char buf[256];
	int idx = 1;
	while (true)
	{
		WWFormat::Into(buf, "AssetUsage_{}_{:04d}.txt", leafname, idx);
		if (_access(buf, 0) != 0)
			break;	// it exists, we're good
		++idx;
	}
	
	FILE *fp = fopen(buf, "w");
	if (fp)
	{
		for (int i=0; i<names.Count(); i++) 
		{
			const char* n = names[i];
			fprintf(fp, "%s\n", n);
		}
		fclose(fp);
	}
}
#endif
