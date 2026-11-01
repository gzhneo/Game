/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// wwd3dmath.cpp -- D3D-layout matrix helpers, see header.

#include "wwd3dmath.h"
#include <math.h>
#include <string.h>

WWD3DMatrix WWD3DMatrix::operator*(const WWD3DMatrix& b) const
{
	WWD3DMatrix out;
	WWD3D::MatrixMultiply(&out, this, &b);
	return out;
}

WWD3DMatrix& WWD3DMatrix::operator*=(const WWD3DMatrix& b)
{
	WWD3D::MatrixMultiply(this, this, &b);
	return *this;
}

WWD3DMatrix* WWD3D::MatrixIdentity(WWD3DMatrix* out)
{
	memset(out, 0, sizeof(WWD3DMatrix));
	out->_11 = out->_22 = out->_33 = out->_44 = 1.0f;
	return out;
}

WWD3DMatrix* WWD3D::MatrixScaling(WWD3DMatrix* out, float sx, float sy, float sz)
{
	MatrixIdentity(out);
	out->_11 = sx;
	out->_22 = sy;
	out->_33 = sz;
	return out;
}

WWD3DMatrix* WWD3D::MatrixTranslation(WWD3DMatrix* out, float x, float y, float z)
{
	MatrixIdentity(out);
	out->_41 = x;
	out->_42 = y;
	out->_43 = z;
	return out;
}

WWD3DMatrix* WWD3D::MatrixRotationZ(WWD3DMatrix* out, float angle)
{
	const float s = sinf(angle);
	const float c = cosf(angle);
	MatrixIdentity(out);
	out->_11 = c;  out->_12 = s;
	out->_21 = -s; out->_22 = c;
	return out;
}

WWD3DMatrix* WWD3D::MatrixMultiply(WWD3DMatrix* out, const WWD3DMatrix* a, const WWD3DMatrix* b)
{
	WWD3DMatrix r;
	for (int i = 0; i < 4; ++i) {
		for (int j = 0; j < 4; ++j) {
			r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j] + a->m[i][3] * b->m[3][j];
		}
	}
	*out = r;
	return out;
}

WWD3DMatrix* WWD3D::MatrixTranspose(WWD3DMatrix* out, const WWD3DMatrix* in)
{
	WWD3DMatrix r;
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			r.m[i][j] = in->m[j][i];
	*out = r;
	return out;
}

Vector4* WWD3D::Vec3Transform(Vector4* out, const Vector3* v, const WWD3DMatrix* m)
{
	const float x = v->X, y = v->Y, z = v->Z;
	Vector4 r;
	r.X = x * m->_11 + y * m->_21 + z * m->_31 + m->_41;
	r.Y = x * m->_12 + y * m->_22 + z * m->_32 + m->_42;
	r.Z = x * m->_13 + y * m->_23 + z * m->_33 + m->_43;
	r.W = x * m->_14 + y * m->_24 + z * m->_34 + m->_44;
	*out = r;
	return out;
}

// General 4x4 inverse via cofactors (Laplace expansion with 2x2 subdeterminants).
WWD3DMatrix* WWD3D::MatrixInverse(WWD3DMatrix* out, float* determinant, const WWD3DMatrix* in)
{
	const float* a = &in->_11;

	const float s0 = a[0]  * a[5]  - a[4]  * a[1];
	const float s1 = a[0]  * a[6]  - a[4]  * a[2];
	const float s2 = a[0]  * a[7]  - a[4]  * a[3];
	const float s3 = a[1]  * a[6]  - a[5]  * a[2];
	const float s4 = a[1]  * a[7]  - a[5]  * a[3];
	const float s5 = a[2]  * a[7]  - a[6]  * a[3];

	const float c5 = a[10] * a[15] - a[14] * a[11];
	const float c4 = a[9]  * a[15] - a[13] * a[11];
	const float c3 = a[9]  * a[14] - a[13] * a[10];
	const float c2 = a[8]  * a[15] - a[12] * a[11];
	const float c1 = a[8]  * a[14] - a[12] * a[10];
	const float c0 = a[8]  * a[13] - a[12] * a[9];

	const float det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
	if (determinant) *determinant = det;
	if (det == 0.0f) return nullptr;

	const float invdet = 1.0f / det;
	WWD3DMatrix r;
	float* b = &r._11;

	b[0]  = ( a[5]  * c5 - a[6]  * c4 + a[7]  * c3) * invdet;
	b[1]  = (-a[1]  * c5 + a[2]  * c4 - a[3]  * c3) * invdet;
	b[2]  = ( a[13] * s5 - a[14] * s4 + a[15] * s3) * invdet;
	b[3]  = (-a[9]  * s5 + a[10] * s4 - a[11] * s3) * invdet;

	b[4]  = (-a[4]  * c5 + a[6]  * c2 - a[7]  * c1) * invdet;
	b[5]  = ( a[0]  * c5 - a[2]  * c2 + a[3]  * c1) * invdet;
	b[6]  = (-a[12] * s5 + a[14] * s2 - a[15] * s1) * invdet;
	b[7]  = ( a[8]  * s5 - a[10] * s2 + a[11] * s1) * invdet;

	b[8]  = ( a[4]  * c4 - a[5]  * c2 + a[7]  * c0) * invdet;
	b[9]  = (-a[0]  * c4 + a[1]  * c2 - a[3]  * c0) * invdet;
	b[10] = ( a[12] * s4 - a[13] * s2 + a[15] * s0) * invdet;
	b[11] = (-a[8]  * s4 + a[9]  * s2 - a[11] * s0) * invdet;

	b[12] = (-a[4]  * c3 + a[5]  * c1 - a[6]  * c0) * invdet;
	b[13] = ( a[0]  * c3 - a[1]  * c1 + a[2]  * c0) * invdet;
	b[14] = (-a[12] * s3 + a[13] * s1 - a[14] * s0) * invdet;
	b[15] = ( a[8]  * s3 - a[9]  * s1 + a[10] * s0) * invdet;

	*out = r;
	return out;
}
