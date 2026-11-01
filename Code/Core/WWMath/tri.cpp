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
 *                 Project Name : WWMath                                                       *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/WWMath/tri.cpp                               $*
 *                                                                                             *
 *                       Author:: Greg Hjelstrom                                               *
 *                                                                                             *
 *                     $Modtime:: 5/03/01 3:41p                                               $*
 *                                                                                             *
 *                    $Revision:: 8                                                           $*
 *                                                                                             *
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   TriClass::Find_Dominant_Plane -- returns indices of the axes of the dominant plane        *
 *   TriClass::Contains_Point -- performs 2D point-in-triangle test.                           *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "tri.h"
#include "vector2.h"


struct FDPRec 
{ 
	int axis1; 
	int axis2; 
	int axis3; 
};

static inline void find_dominant_plane(const TriClass & tri, int * axis1,int * axis2,int * axis3)
{
	/*
	** Find the largest component of the normal
	*/
	int ni = 0;
	float x = WWMath::Fabs(tri.N->X);
	float y = WWMath::Fabs(tri.N->Y);
	float z = WWMath::Fabs(tri.N->Z);
	float val = x;

	if (y > val) {
		ni = 1;
		val = y;
	}

	if (z > val) {
		ni = 2;
	}

	/*
	** return the indices of the two axes perpendicular
	*/
	switch (ni) 
	{
	case 0: 
		// Dominant is the X axis
		*axis1 = 1;
		*axis2 = 2;
		*axis3 = 0;
		break;
	case 1: 
		// Dominant is the Y axis
		*axis1 = 0;
		*axis2 = 2;
		*axis3 = 1;
		break;
	case 2: 
		// Dominant is the Z axis
		*axis1 = 0;
		*axis2 = 1;
		*axis3 = 2;
		break;
	}
}



/***********************************************************************************************
 * TriClass::Find_Dominant_Plane -- returns indices of the axes of the dominant plane          *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/99    GTH : Created.                                                                 *
 *=============================================================================================*/
void TriClass::Find_Dominant_Plane(int * axis1,int * axis2) const
{
	/*
	** Find the largest component of the normal
	*/
	int ni = 0;
	float x = WWMath::Fabs(N->X);
	float y = WWMath::Fabs(N->Y);
	float z = WWMath::Fabs(N->Z);
	float val = x;

	if (y > val) {
		ni = 1;
		val = y;
	}

	if (z > val) {
		ni = 2;
	}

	/*
	** return the indices of the two axes perpendicular
	*/
	switch (ni) 
	{
	case 0: 
		// Dominant is the X axis
		*axis1 = 1;
		*axis2 = 2;
		break;
	case 1: 
		// Dominant is the Y axis
		*axis1 = 0;
		*axis2 = 2;
		break;
	case 2: 
		// Dominant is the Z axis
		*axis1 = 0;
		*axis2 = 1;
		break;
	}
}


/***********************************************************************************************
 * TriClass::Contains_Point -- performs 2D point-in-triangle test.                             *
 *                                                                                             *
 * INPUT:                                                                                      *
 *                                                                                             *
 * OUTPUT:                                                                                     *
 *                                                                                             *
 * WARNINGS:                                                                                   *
 * Assumes that the point is in the plane of the triangle... use this after you've intersected *
 * a ray with the plane of the triangle.                                                       *
 *                                                                                             *
 * HISTORY:                                                                                    *
 *   3/24/99    GTH : Created.                                                                 *
 *=============================================================================================*/
bool TriClass::Contains_Point(const Vector3 & ipoint) const
{
/*
** New cross-product based point-containment
*/


/*
** "Optimized" version
*/
#if 1
	int vi;
	int axis1 = 0;
	int axis2 = 0;
	int axis3 = 0;

	find_dominant_plane(*this,&axis1,&axis2,&axis3);

	bool side[3];

	/*
	** Compute the 2D cross product of edge0 with a vector to the point
	*/
	Vector2 edge;
	Vector2 dp;

	for (vi=0; vi<3; vi++) {
		
		int va=vi;
		int vb=(vi+1)%3;
		
		edge.Set((*V[vb])[axis1] - (*V[va])[axis1] , (*V[vb])[axis2] - (*V[va])[axis2]);
		dp.Set(ipoint[axis1] - (*V[va])[axis1] , ipoint[axis2] - (*V[va])[axis2]);
		float cross = edge.X * dp.Y - edge.Y * dp.X;
		side[vi] = (cross >= 0.0f);
	}

	bool my_intersect = ((side[0] == side[1]) && (side[1] == side[2]));
	return my_intersect;
#endif


}
