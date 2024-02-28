#include "PCH.h"
#include "Rasterization.h"
#include "SIMDMath.h"
#include "MathHelper.h"

#define SIMD_WIDTH 4

static const int32_t s_SubpixelStep = 16; // 4 bits sub-pixel precision

struct SRasterizerVertex
{
    int32_t m_X;
    int32_t m_Y;
    float m_Z;
    float m_Rcpw;
    float m_TexcoordU;
    float m_TexcoordV;
};

using namespace Rasterizer;

static float s_ViewProjectionMatrix[ 16 ] =
    { 
        1.f, 0.f, 0.f, 0.f, 
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f 
    };
static float s_BaseColor[ 4 ] = { 1.f, 1.f, 1.f, 1.f };

static SViewport s_Viewport = { 0 };
static int32_t s_RasterCoordStartX = 0;
static int32_t s_RasterCoordStartY = 0;
static int32_t s_RasterCoordEndX = 0;
static int32_t s_RasterCoordEndY = 0;

static const float* s_StreamSourcePosX = nullptr;
static const float* s_StreamSourcePosY = nullptr;
static const float* s_StreamSourcePosZ = nullptr;
static const float* s_StreamSourceTexU = nullptr;
static const float* s_StreamSourceTexV = nullptr;

static SImage s_RenderTarget = { 0 };
static SImage s_DepthTarget = { 0 };

static void __vectorcall TransformVec3Stream( __m128 m00, __m128 m01, __m128 m02, __m128 m03,
    __m128 m10, __m128 m11, __m128 m12, __m128 m13,
    __m128 m20, __m128 m21, __m128 m22, __m128 m23,
    __m128 m30, __m128 m31, __m128 m32, __m128 m33,
    const float* inX, const float* inY, const float* inZ, uint32_t count,
    float* outX, float* outY, float* outZ, float* outW )
{
    assert( count % SIMD_WIDTH == 0 );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = _mm_load_ps( inX );
        __m128 y = _mm_load_ps( inY );
        __m128 z = _mm_load_ps( inZ );
        __m128 dotX, dotY, dotZ, dotW;
        SIMDMath::Vec3DotVec4( x, y, z, m00, m10, m20, m30, dotX );
        SIMDMath::Vec3DotVec4( x, y, z, m01, m11, m21, m31, dotY );
        SIMDMath::Vec3DotVec4( x, y, z, m02, m12, m22, m32, dotZ );
        SIMDMath::Vec3DotVec4( x, y, z, m03, m13, m23, m33, dotW );
        _mm_store_ps( outX, dotX );
        _mm_store_ps( outY, dotY );
        _mm_store_ps( outZ, dotZ );
        _mm_store_ps( outW, dotW );

        inX += SIMD_WIDTH;
        inY += SIMD_WIDTH;
        inZ += SIMD_WIDTH;
        outX += SIMD_WIDTH;
        outY += SIMD_WIDTH;
        outZ += SIMD_WIDTH;
        outW += SIMD_WIDTH;
    }
}

static void __vectorcall AffineTransformVec3Stream( __m128 m00, __m128 m01, __m128 m02,
    __m128 m10, __m128 m11, __m128 m12,
    __m128 m20, __m128 m21, __m128 m22,
    __m128 m30, __m128 m31, __m128 m32,
    const float* inX, const float* inY, const float* inZ, uint32_t count,
    float* outX, float* outY, float* outZ )
{
    assert( count % SIMD_WIDTH == 0 );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = _mm_load_ps( inX );
        __m128 y = _mm_load_ps( inY );
        __m128 z = _mm_load_ps( inZ );
        __m128 dotX, dotY, dotZ;
        SIMDMath::Vec3DotVec4( x, y, z, m00, m10, m20, m30, dotX );
        SIMDMath::Vec3DotVec4( x, y, z, m01, m11, m21, m31, dotY );
        SIMDMath::Vec3DotVec4( x, y, z, m02, m12, m22, m32, dotZ );
        _mm_store_ps( outX, dotX );
        _mm_store_ps( outY, dotY );
        _mm_store_ps( outZ, dotZ );

        inX += SIMD_WIDTH;
        inY += SIMD_WIDTH;
        inZ += SIMD_WIDTH;
        outX += SIMD_WIDTH;
        outY += SIMD_WIDTH;
        outZ += SIMD_WIDTH;
    }
}

static void TransformVerticesToRasterizerCoordinates( float* inX, float* inY, float* inZ, float* inW, const float* inTexU, const float* inTexV, float* outTexU, float* outTexV, uint32_t count )
{
    assert( count % SIMD_WIDTH == 0 );

    const float halfRasterizerWidth = s_Viewport.m_Width * s_SubpixelStep * 0.5f;
    const float halfRasterizerHeight = s_Viewport.m_Height * s_SubpixelStep * 0.5f;
    const int32_t halfPixelOffset = s_SubpixelStep / 2;
    const int32_t offsetX = s_RasterCoordStartX - halfPixelOffset;
    const int32_t offsetY = s_RasterCoordStartY - halfPixelOffset;

    __m128 vHalfRasterizerWidth = _mm_set1_ps( halfRasterizerWidth );
    __m128 vHalfRasterizerHeight = _mm_set1_ps( halfRasterizerHeight );
    __m128i vOffsetX = _mm_set1_epi32( offsetX );
    __m128i vOffsetY = _mm_set1_epi32( offsetY );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = _mm_load_ps( inX );
        __m128 y = _mm_load_ps( inY );
        __m128 z = _mm_load_ps( inZ );
        __m128 w = _mm_load_ps( inW );
        __m128 texU = _mm_load_ps( inTexU );
        __m128 texV = _mm_load_ps( inTexV );
        __m128 one = { 1.f, 1.f, 1.f, 1.f };
        __m128 rcpw = _mm_div_ps( one, w );
        __m128 half = { .5f, .5f, .5f, .5f };

        x = _mm_fmadd_ps( x, rcpw, one ); // x = x / w - (-1)
        x = _mm_fmadd_ps( x, vHalfRasterizerWidth, half ); // Add 0.5 for rounding
        __m128i xi = _mm_cvttps_epi32( x );
        xi = _mm_add_epi32( xi, vOffsetX );

        y = _mm_fmadd_ps( y, rcpw, one ); // y = y / w - (-1)
        y = _mm_fmadd_ps( y, vHalfRasterizerHeight, half ); // Add 0.5 for rounding
        __m128i yi = _mm_cvttps_epi32( y );
        yi = _mm_add_epi32( yi, vOffsetY );

        z = _mm_mul_ps( z, rcpw );

        texU = _mm_mul_ps( texU, rcpw );
        texV = _mm_mul_ps( texV, rcpw );

        _mm_store_si128( (__m128i*)inX, xi );
        _mm_store_si128( (__m128i*)inY, yi );
        _mm_store_ps( inZ, z );
        _mm_store_ps( inW, rcpw );
        _mm_store_ps( outTexU, texU );
        _mm_store_ps( outTexV, texV );

        inX += SIMD_WIDTH;
        inY += SIMD_WIDTH;
        inZ += SIMD_WIDTH;
        inW += SIMD_WIDTH;
        inTexU += SIMD_WIDTH;
        inTexV += SIMD_WIDTH;
        outTexU += SIMD_WIDTH;
        outTexV += SIMD_WIDTH;
    }
}

static inline bool IsTopLeftEdge( const SRasterizerVertex& v0, const SRasterizerVertex& v1 )
{
    bool isTop = v0.m_Y == v1.m_Y && v0.m_X > v1.m_X;
    bool isLeft = v0.m_Y > v1.m_Y;
    return isLeft || isTop;
}

static void RasterizeTriangle( const SRasterizerVertex& v0, const SRasterizerVertex& v1, const SRasterizerVertex& v2 )
{
    // Calculate bounding box of the triangle and crop with the viewport
    int32_t minX = std::max( s_RasterCoordStartX, std::min( v0.m_X, std::min( v1.m_X, v2.m_X ) ) );
    int32_t minY = std::max( s_RasterCoordStartY, std::min( v0.m_Y, std::min( v1.m_Y, v2.m_Y ) ) );
    int32_t maxX = std::min( s_RasterCoordEndX, std::max( v0.m_X, std::max( v1.m_X, v2.m_X ) ) );
    int32_t maxY = std::min( s_RasterCoordEndY, std::max( v0.m_Y, std::max( v1.m_Y, v2.m_Y ) ) );
    // Round up the minimum of the bounding box to the nearest pixel center
    minX = MathHelper::DivideAndRoundUp( minX - s_RasterCoordStartX, s_SubpixelStep ) * s_SubpixelStep + s_RasterCoordStartX;
    minY = MathHelper::DivideAndRoundUp( minY - s_RasterCoordStartY, s_SubpixelStep ) * s_SubpixelStep + s_RasterCoordStartY;
    // Compute the image coordinate where the minimum of the bounding box is
    const int32_t imgMinX = s_Viewport.m_Left + ( minX - s_RasterCoordStartX ) / s_SubpixelStep;
    int32_t imgY = s_Viewport.m_Top + s_Viewport.m_Height - ( minY - s_RasterCoordStartY ) / s_SubpixelStep - 1; // Image axis y is flipped

    int32_t a01 = v0.m_Y - v1.m_Y, b01 = v1.m_X - v0.m_X, c01 = v0.m_X * v1.m_Y - v0.m_Y * v1.m_X;
    int32_t a12 = v1.m_Y - v2.m_Y, b12 = v2.m_X - v1.m_X, c12 = v1.m_X * v2.m_Y - v1.m_Y * v2.m_X;
    int32_t a20 = v2.m_Y - v0.m_Y, b20 = v0.m_X - v2.m_X, c20 = v2.m_X * v0.m_Y - v2.m_Y * v0.m_X;

    int32_t w0_row = a12 * minX + b12 * minY + c12;
    int32_t w1_row = a20 * minX + b20 * minY + c20;
    int32_t w2_row = a01 * minX + b01 * minY + c01;

    // Compute the singed area of the triangle for barycentric coordinates normalization
    const int32_t doubleSignedArea = a01 * v2.m_X + b01 * v2.m_Y + c01; // Plug v2 into the edge function of edge01
    const float rcpDoubleSignedArea = 1.0f / doubleSignedArea;

    // Pre-multiply the edge function increments by sub-pixel steps
    a01 *= s_SubpixelStep; b01 *= s_SubpixelStep;
    a12 *= s_SubpixelStep; b12 *= s_SubpixelStep;
    a20 *= s_SubpixelStep; b20 *= s_SubpixelStep;

    // Barycentric coordinates at minimum of the bounding box
    float bw0_row = w0_row * rcpDoubleSignedArea;
    float bw1_row = w1_row * rcpDoubleSignedArea;
    float bw2_row = w2_row * rcpDoubleSignedArea;
    // Horizontal barycentric coordinates increment 
    float ba01 = a01 * rcpDoubleSignedArea;
    float ba12 = a12 * rcpDoubleSignedArea;
    float ba20 = a20 * rcpDoubleSignedArea;
    // Vertical barycentric coordinates increment
    float bb01 = b01 * rcpDoubleSignedArea;
    float bb12 = b12 * rcpDoubleSignedArea;
    float bb20 = b20 * rcpDoubleSignedArea;

    float z_row = v0.m_Z * bw0_row + ( v1.m_Z * bw1_row + ( v2.m_Z * bw2_row ) ); // Z at the minimum of the bounding box
    float z_a = v0.m_Z * ba12 + ( v1.m_Z * ba20 + ( v2.m_Z * ba01 ) ); // Horizontal Z increment
    float z_b = v0.m_Z * bb12 + ( v1.m_Z * bb20 + ( v2.m_Z * bb01 ) ); // Vertical Z increment
    
    float rcpw_row = v0.m_Rcpw * bw0_row + ( v1.m_Rcpw * bw1_row + ( v2.m_Rcpw * bw2_row ) ); // rcpw at the minimum of the bounding box
    float rcpw_a = v0.m_Rcpw * ba12 + ( v1.m_Rcpw * ba20 + ( v2.m_Rcpw * ba01 ) ); // Horizontal rcpw increment
    float rcpw_b = v0.m_Rcpw * bb12 + ( v1.m_Rcpw * bb20 + ( v2.m_Rcpw * bb01 ) ); // Vertical rcpw increment

    float texU_row = v0.m_TexcoordU * bw0_row + ( v1.m_TexcoordU * bw1_row + ( v2.m_TexcoordU * bw2_row ) ); // texU at the minimum of the bounding box
    float texU_a = v0.m_TexcoordU * ba12 + ( v1.m_TexcoordU * ba20 + ( v2.m_TexcoordU * ba01 ) ); // Horizontal texU increment
    float texU_b = v0.m_TexcoordU * bb12 + ( v1.m_TexcoordU * bb20 + ( v2.m_TexcoordU * bb01 ) ); // Vertical texU increment

    float texV_row = v0.m_TexcoordV * bw0_row + ( v1.m_TexcoordV * bw1_row + ( v2.m_TexcoordV * bw2_row ) ); // texV at the minimum of the bounding box
    float texV_a = v0.m_TexcoordV * ba12 + ( v1.m_TexcoordV * ba20 + ( v2.m_TexcoordV * ba01 ) ); // Horizontal texV increment
    float texV_b = v0.m_TexcoordV * bb12 + ( v1.m_TexcoordV * bb20 + ( v2.m_TexcoordV * bb01 ) ); // Vertical texV increment

    // Apply top left rule
    const int32_t topLeftBias0 = IsTopLeftEdge( v1, v2 ) ? 0 : -1;
    const int32_t topLeftBias1 = IsTopLeftEdge( v2, v0 ) ? 0 : -1;
    const int32_t topLeftBias2 = IsTopLeftEdge( v0, v1 ) ? 0 : -1;
    w0_row += topLeftBias0;
    w1_row += topLeftBias1;
    w2_row += topLeftBias2;
    
    int32_t pX, pY;
    for ( pY = minY; pY <= maxY; pY += s_SubpixelStep, imgY -= 1 )
    {
        int32_t w0 = w0_row;
        int32_t w1 = w1_row;
        int32_t w2 = w2_row;

        int32_t imgX = imgMinX;

        float z = z_row;
        float rcpw = rcpw_row;
        float texU = texU_row;
        float texV = texV_row;

        for ( pX = minX; pX <= maxX; pX += s_SubpixelStep, imgX += 1 )
        {
            if ( ( w0 | w1 | w2 ) >= 0 ) // counter-clockwise triangle has positive area
            {
                float* dstDepth = (float*)s_DepthTarget.m_Bits + imgY * s_DepthTarget.m_Width + imgX;
                if ( z < *dstDepth )
                {
                    *dstDepth = z;

                    float w = 1.0f / rcpw;
                    float texcoordU = texU * w;
                    float texcoordV = texV * w;

                    bool isWhite = ( int32_t( texcoordU / 0.1f ) + int32_t( texcoordV / 0.1f ) ) % 2 == 0;
                    uint8_t r = isWhite ? 255 : uint8_t( s_BaseColor[ 0 ] * 255.f + 0.5f );
                    uint8_t g = isWhite ? 255 : uint8_t( s_BaseColor[ 1 ] * 255.f + 0.5f );
                    uint8_t b = isWhite ? 255 : uint8_t( s_BaseColor[ 2 ] * 255.f + 0.5f );
                    uint32_t* dstColor = (uint32_t*)s_RenderTarget.m_Bits + imgY * s_RenderTarget.m_Width + imgX;
                    *dstColor = 0xFF000000 | r << 16 | g << 8 | b;
                }
            }

            w0 += a12;
            w1 += a20;
            w2 += a01;

            z += z_a;
            rcpw += rcpw_a;
            texU += texU_a;
            texV += texV_a;
        }

        w0_row += b12;
        w1_row += b20;
        w2_row += b01;

        z_row += z_b;
        rcpw_row += rcpw_b;
        texU_row += texU_b;
        texV_row += texV_b;
    }
}

void Rasterizer::SetPositionStreams( const float* x, const float* y, const float* z )
{
    s_StreamSourcePosX = x;
    s_StreamSourcePosY = y;
    s_StreamSourcePosZ = z;
}

void Rasterizer::SetTexcoordStreams( const float* texU, const float* texV )
{
    s_StreamSourceTexU = texU;
    s_StreamSourceTexV = texV;
}

void Rasterizer::SetViewProjectionMatrix( const float* matrix )
{
    memcpy( s_ViewProjectionMatrix, matrix, sizeof( s_ViewProjectionMatrix ) );
}

void Rasterizer::SetViewport( const SViewport& viewport )
{
    s_Viewport = viewport;
    // Center the viewport at the rasterizer coordinate origin
    s_RasterCoordStartX = -int32_t( viewport.m_Width * s_SubpixelStep / 2 );
    s_RasterCoordStartY = -int32_t( viewport.m_Height * s_SubpixelStep / 2 );
    s_RasterCoordEndX = s_RasterCoordStartX + s_Viewport.m_Width * s_SubpixelStep - 1;
    s_RasterCoordEndY = s_RasterCoordStartY + s_Viewport.m_Height * s_SubpixelStep - 1;
}

void Rasterizer::SetRenderTarget( const SImage& image )
{
    s_RenderTarget = image;
}

void Rasterizer::SetDepthTarget( const SImage& image )
{
    s_DepthTarget = image;
}

void Rasterizer::SetBaseColor( const float* color )
{
    memcpy( s_BaseColor, color, sizeof( s_BaseColor ) );
}

void Rasterizer::Draw( uint32_t baseVertexIndex, uint32_t trianglesCount )
{
    uint32_t verticesCount = trianglesCount * 3;
    uint32_t roundedUpVerticesCount = MathHelper::DivideAndRoundUp( verticesCount, (uint32_t)SIMD_WIDTH ) * SIMD_WIDTH;
    uint64_t streamSize = roundedUpVerticesCount * sizeof( float );
    float* streamPosX = (float*)_aligned_malloc( streamSize, 16 );
    float* streamPosY = (float*)_aligned_malloc( streamSize, 16 );
    float* streamPosZ = (float*)_aligned_malloc( streamSize, 16 );
    float* streamPosW = (float*)_aligned_malloc( streamSize, 16 );
    
    // transform vertex positions
    {
        __m128 m00 = _mm_set1_ps( s_ViewProjectionMatrix[ 0 ] );
        __m128 m01 = _mm_set1_ps( s_ViewProjectionMatrix[ 1 ] );
        __m128 m02 = _mm_set1_ps( s_ViewProjectionMatrix[ 2 ] );
        __m128 m03 = _mm_set1_ps( s_ViewProjectionMatrix[ 3 ] );
        __m128 m10 = _mm_set1_ps( s_ViewProjectionMatrix[ 4 ] );
        __m128 m11 = _mm_set1_ps( s_ViewProjectionMatrix[ 5 ] );
        __m128 m12 = _mm_set1_ps( s_ViewProjectionMatrix[ 6 ] );
        __m128 m13 = _mm_set1_ps( s_ViewProjectionMatrix[ 7 ] );
        __m128 m20 = _mm_set1_ps( s_ViewProjectionMatrix[ 8 ] );
        __m128 m21 = _mm_set1_ps( s_ViewProjectionMatrix[ 9 ] );
        __m128 m22 = _mm_set1_ps( s_ViewProjectionMatrix[ 10 ] );
        __m128 m23 = _mm_set1_ps( s_ViewProjectionMatrix[ 11 ] );
        __m128 m30 = _mm_set1_ps( s_ViewProjectionMatrix[ 12 ] );
        __m128 m31 = _mm_set1_ps( s_ViewProjectionMatrix[ 13 ] );
        __m128 m32 = _mm_set1_ps( s_ViewProjectionMatrix[ 14 ] );
        __m128 m33 = _mm_set1_ps( s_ViewProjectionMatrix[ 15 ] );

        TransformVec3Stream( m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33,
            s_StreamSourcePosX + baseVertexIndex, s_StreamSourcePosY + baseVertexIndex, s_StreamSourcePosZ + baseVertexIndex,
            roundedUpVerticesCount,
            streamPosX, streamPosY, streamPosZ, streamPosW );
    }

    float* streamTexU = (float*)_aligned_malloc( streamSize, 16 );
    float* streamTexV = (float*)_aligned_malloc( streamSize, 16 );

    TransformVerticesToRasterizerCoordinates( streamPosX, streamPosY, streamPosZ, streamPosW, s_StreamSourceTexU + baseVertexIndex, s_StreamSourceTexV + baseVertexIndex, streamTexU, streamTexV, roundedUpVerticesCount );

    {
        const int32_t* streamPosXi = (int32_t*)streamPosX;
        const int32_t* streamPosYi = (int32_t*)streamPosY;
        for ( uint32_t i = 0; i < trianglesCount; ++i )
        {
            uint32_t vertexIndexBase = i * 3;
            SRasterizerVertex v0 = { streamPosXi[ vertexIndexBase ], streamPosYi[ vertexIndexBase ], streamPosZ[ vertexIndexBase ], streamPosW[ vertexIndexBase ], streamTexU[ vertexIndexBase ], streamTexV[ vertexIndexBase ] };
            SRasterizerVertex v1 = { streamPosXi[ vertexIndexBase + 1 ], streamPosYi[ vertexIndexBase + 1 ], streamPosZ[ vertexIndexBase + 1 ], streamPosW[ vertexIndexBase + 1 ], streamTexU[ vertexIndexBase + 1 ], streamTexV[ vertexIndexBase + 1 ] };
            SRasterizerVertex v2 = { streamPosXi[ vertexIndexBase + 2 ], streamPosYi[ vertexIndexBase + 2 ], streamPosZ[ vertexIndexBase + 2 ], streamPosW[ vertexIndexBase + 2 ], streamTexU[ vertexIndexBase + 2 ], streamTexV[ vertexIndexBase + 2 ] };
            RasterizeTriangle( v0, v1, v2 );
        }
    }

    _aligned_free( streamPosX );
    _aligned_free( streamPosY );
    _aligned_free( streamPosZ );
    _aligned_free( streamPosW );
    _aligned_free( streamTexU );
    _aligned_free( streamTexV );
}
