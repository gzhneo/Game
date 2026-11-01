/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// wwd3dmath.h --------------------------------------------------------------------------------
//
// Matrix helpers with D3DX semantics (D3DXMATRIX, D3DXMatrixInverse/Scaling/Translation/Multiply/
// Transpose/Identity/RotationZ, D3DXVec3Transform; D3DX_PI -> WWMATH_PI). The renderer works in
// D3D layout here on purpose (row vectors, translation in row 4, a*b = a first, then b) because
// the matrices come straight from _Get_Device_Transform or go to _Set_Device_Transform. The
// Westwood Matrix4x4 uses the transposed convention, hence a separate class with the same layout
// (16 floats, _11.._44). Vectors are the WWMath types.
//
// The inverse uses cofactors; its last bits may differ from D3DX. This only affects texture
// projections, never game logic.
// ---------------------------------------------------------------------------------------------

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef WWD3DMATH_H
#define WWD3DMATH_H

#include "vector3.h"
#include "vector4.h"

// Anonymous struct in the union as in D3DMATRIX is intended; C4201 is disabled only here.
#pragma warning(push)
#pragma warning(disable: 4201)
struct WWD3DMatrix
{
	union {
		struct {
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float m[4][4];
	};
#pragma warning(pop)

	float& operator()(int row, int col) { return m[row][col]; }
	const float& operator()(int row, int col) const { return m[row][col]; }

	WWD3DMatrix operator*(const WWD3DMatrix& b) const;
	WWD3DMatrix& operator*=(const WWD3DMatrix& b);
};

namespace WWD3D
{
	WWD3DMatrix* MatrixIdentity(WWD3DMatrix* out);
	WWD3DMatrix* MatrixInverse(WWD3DMatrix* out, float* determinant, const WWD3DMatrix* in);	///< NULL if singular (out unchanged)
	WWD3DMatrix* MatrixScaling(WWD3DMatrix* out, float sx, float sy, float sz);
	WWD3DMatrix* MatrixTranslation(WWD3DMatrix* out, float x, float y, float z);
	WWD3DMatrix* MatrixMultiply(WWD3DMatrix* out, const WWD3DMatrix* a, const WWD3DMatrix* b);	///< out = a * b, aliasing allowed
	WWD3DMatrix* MatrixTranspose(WWD3DMatrix* out, const WWD3DMatrix* in);						///< aliasing allowed
	WWD3DMatrix* MatrixRotationZ(WWD3DMatrix* out, float angle);
	Vector4* Vec3Transform(Vector4* out, const Vector3* v, const WWD3DMatrix* m);					///< out = (v, 1) * m
}

#endif // WWD3DMATH_H
