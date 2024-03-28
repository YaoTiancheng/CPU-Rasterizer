#pragma once

#include <immintrin.h>

namespace SIMDMath
{ 
    inline void __vectorcall Vec3DotVec3( __m128 ax, __m128 ay, __m128 az, __m128 bx, __m128 by, __m128 bz, __m128& out )
    {
        out = _mm_fmadd_ps( ax, bx, _mm_fmadd_ps( ay, by, _mm_mul_ps( az, bz ) ) );
    }

    inline void __vectorcall Vec3DotVec4( __m128 ax, __m128 ay, __m128 az, __m128 bx, __m128 by, __m128 bz, __m128 bw, __m128& out )
    {
        out = _mm_fmadd_ps( ax, bx, _mm_fmadd_ps( ay, by, _mm_fmadd_ps( az, bz, bw ) ) );
    }

    inline void __vectorcall Vec3MulMatrix4x4( __m128 x, __m128 y, __m128 z,
                      __m128 m00, __m128 m01, __m128 m02, __m128 m03,
                      __m128 m10, __m128 m11, __m128 m12, __m128 m13,
                      __m128 m20, __m128 m21, __m128 m22, __m128 m23,
                      __m128 m30, __m128 m31, __m128 m32, __m128 m33,
                      __m128& outX, __m128& outY, __m128& outZ, __m128& outW )
    {
        outX = _mm_fmadd_ps( x, m00, _mm_fmadd_ps( y, m10, _mm_fmadd_ps( z, m20, m30 ) ) );
        outY = _mm_fmadd_ps( x, m01, _mm_fmadd_ps( y, m11, _mm_fmadd_ps( z, m21, m31 ) ) );
        outZ = _mm_fmadd_ps( x, m02, _mm_fmadd_ps( y, m12, _mm_fmadd_ps( z, m22, m32 ) ) );
        outW = _mm_fmadd_ps( x, m03, _mm_fmadd_ps( y, m13, _mm_fmadd_ps( z, m23, m33 ) ) );
    }
}
    

