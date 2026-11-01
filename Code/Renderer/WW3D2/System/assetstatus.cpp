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

#include "assetstatus.h"
#include <new>
#include "hashtemplate.h"
#include "wwstring.h"
#include "RAWFILE.H"

AssetStatusClass AssetStatusClass::Instance;

const char* ReportCategoryNames[AssetStatusClass::REPORT_COUNT]={
	"LOAD_ON_DEMAND_ROBJ",
	"LOAD_ON_DEMAND_HANIM",
	"LOAD_ON_DEMAND_HTREE",
	"MISSING_ROBJ",
	"MISSING_HANIM",
	"MISSING_HTREE"
};

AssetStatusClass::AssetStatusClass()
	:
	Reporting (true),
	LoadOnDemandReporting(false)
{
}

AssetStatusClass::~AssetStatusClass()
{
	Flush_Report();
}

void AssetStatusClass::Flush_Report()
{
	// Remove_All() keeps the allocated arrays; rebuild the tables so nothing survives into the
	// memory report.
	for (int i=0;i<REPORT_COUNT;++i) {
		ReportHashTables[i].~HashTemplateClass();
		new (&ReportHashTables[i]) HashTemplateClass<StringClass, int>();
	}
}

void AssetStatusClass::Add_To_Report(int index, const char* name)
{
	StringClass lower_case_name(name,true);
	_strlwr(lower_case_name.Peek_Buffer());
	// This is a bit slow - two accesses to the same member, but currently there's no better way to do it.
	int count=ReportHashTables[index].Get(lower_case_name);
	count++;
	ReportHashTables[index].Set_Value(lower_case_name,count);
}

void AssetStatusClass::Report_Load_On_Demand_RObj(const char* name)
{
	if (LoadOnDemandReporting) Add_To_Report(REPORT_LOAD_ON_DEMAND_ROBJ,name);
}

void AssetStatusClass::Report_Load_On_Demand_HAnim(const char* name)
{
	if (LoadOnDemandReporting) Add_To_Report(REPORT_LOAD_ON_DEMAND_HANIM,name);
}

void AssetStatusClass::Report_Load_On_Demand_HTree(const char* name)
{
	if (LoadOnDemandReporting) Add_To_Report(REPORT_LOAD_ON_DEMAND_HTREE,name);
}

void AssetStatusClass::Report_Missing_RObj(const char* name)
{
	Add_To_Report(REPORT_MISSING_ROBJ,name);
}

void AssetStatusClass::Report_Missing_HAnim(const char* name)
{
	Add_To_Report(REPORT_MISSING_HANIM,name);
}

void AssetStatusClass::Report_Missing_HTree(const char* name)
{
	Add_To_Report(REPORT_MISSING_HTREE,name);
}

