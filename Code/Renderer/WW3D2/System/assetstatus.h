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

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef WW3D_ASSET_STATUS_H
#define WW3D_ASSET_STATUS_H

#include "always.h"
#include "hashtemplate.h"

class AssetStatusClass
{
public:
	enum {
		REPORT_LOAD_ON_DEMAND_ROBJ,
		REPORT_LOAD_ON_DEMAND_HANIM,
		REPORT_LOAD_ON_DEMAND_HTREE,
		REPORT_MISSING_ROBJ,
		REPORT_MISSING_HANIM,
		REPORT_MISSING_HTREE,
		REPORT_COUNT
	};

	AssetStatusClass();
	~AssetStatusClass();

	void Enable_Reporting(bool enable)					  { Reporting=enable; }	
	void Enable_Load_On_Demand_Reporting(bool enable) { LoadOnDemandReporting=enable; }

	void Report_Load_On_Demand_RObj(const char* name);
	void Report_Load_On_Demand_HAnim(const char* name);
	void Report_Load_On_Demand_HTree(const char* name);

	void Report_Missing_RObj(const char* name);
	void Report_Missing_HAnim(const char* name);
	void Report_Missing_HTree(const char* name);

	static AssetStatusClass* Peek_Instance() { return &Instance; }

	// Empties the report tables. The asset manager calls it on shutdown so the entries do not
	// live until the memory report at the end of WinMain, where they would count as leaks.
	// The destructor of the static instance calls it again and finds empty tables.
	void Flush_Report();

private:
	bool Reporting;
	bool LoadOnDemandReporting;
	static AssetStatusClass Instance;
	HashTemplateClass<StringClass, int> ReportHashTables[REPORT_COUNT];

	void Add_To_Report(int index, const char* name);

};

#endif
