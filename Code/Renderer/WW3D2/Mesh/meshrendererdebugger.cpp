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

#include "meshrendererdebugger.h"
#include "hashtemplate.h"
#include "mesh.h"
#include "meshmdl.h"

static HashTemplateClass<unsigned, MeshClass*> MeshHash;

bool MeshRendererDebugger::Enabled;

void MeshRendererDebugger::Enable(bool enable)
{
	Enabled=true;
}

void MeshRendererDebugger::Get_String(StringClass& s)
{
	if (!Enabled) {
		s="";
		return;
	}
	s="\n\n\n\n";

	int cnt=0;

	HashTemplateIterator<unsigned,MeshClass*> ite(MeshHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		StringClass tmp(0,true);
		MeshClass* mesh=ite.Peek_Value();
		MeshModelClass* mmc=mesh->Peek_Model();
		int polys=0;
		int verts=0;
		if (mmc) {
			polys=mmc->Get_Polygon_Count();
			verts=mmc->Get_Vertex_Count();
		}
		tmp.Format("id: %5.5d mesh: %s %d polys, %d verts",
			ite.Peek_Key(),
			mesh->Get_Name(),
			polys,
			verts);
		s+=tmp;
		if (mesh->Is_Disabled_By_Debugger()) {
			s+=" (disabled)\n";
		}
		else {
			s+="\n";
		}
		cnt++;
		if (cnt>20) break;
	}

}

void MeshRendererDebugger::Update()
{
	// Release references to all meshes and empty the hash
	HashTemplateIterator<unsigned,MeshClass*> ite(MeshHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		ite.Peek_Value()->Release_Ref();
	}
	MeshHash.Remove_All();

//	if (!Enabled) return;
}

void MeshRendererDebugger::Disable_Mesh(unsigned id)
{
	if (!Enabled) return;
	MeshClass* mesh=MeshHash.Get(id);
	if (!mesh) return;
	mesh->Set_Debugger_Disable(true);
}

void MeshRendererDebugger::Enable_Mesh(unsigned id)
{
	if (!Enabled) return;
	MeshClass* mesh=MeshHash.Get(id);
	if (!mesh) return;
	mesh->Set_Debugger_Disable(false);
}

void MeshRendererDebugger::Disable_All()
{
	if (!Enabled) return;

	HashTemplateIterator<unsigned,MeshClass*> ite(MeshHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		ite.Peek_Value()->Set_Debugger_Disable(true);
	}
}

void MeshRendererDebugger::Enable_All()
{
	if (!Enabled) return;

	HashTemplateIterator<unsigned,MeshClass*> ite(MeshHash);
	for (ite.First();!ite.Is_Done();ite.Next()) {
		ite.Peek_Value()->Set_Debugger_Disable(false);
	}
}

