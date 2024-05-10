#include "PCH.h"
#include "Rasterizer.h"
#include "ImageOps.inl"
#include "SIMDMath.inl"
#include "MathHelper.h"

#define SIMD_WIDTH 4

#define VERTEX_TRANSFORM_FUNCTION_TABLE_SIZE 4
#define PERSPECTIVE_DIVISION_FUNCTION_TABLE_SIZE 16
#define TRIANGLE_SETUP_FUNCTION_TABLE_SIZE 16
#define RASTERIZING_FUNCTION_TABLE_SIZE 128

using namespace Rasterizer;

static const int32_t s_SubpixelStep = 16; // 4 bits sub-pixel precision

struct alignas( 16 ) SFloat4A
{
    float m_Data[ 4 ];
};

struct alignas( 16 ) SInt4A
{
    int32_t m_Data[ 4 ];
};

struct STriangleAttribute
{
    float row, a, b;
};

struct STriangleBaseAttributes
{
    int32_t minX, maxX, minY, maxY;
    int32_t imgMinX, imgY;
    int32_t w0_row, w1_row, w2_row;
    int32_t a01, a12, a20;
    int32_t b01, b12, b20;
    uint8_t faceSign;
};

struct SAttributeStreamPtrs
{
    union
    {
        uint8_t* pos;
        uint8_t* base;
    };
    uint8_t* z;
    union
    {
        uint8_t* w;
        uint8_t* rcpw;
    };
    uint8_t* texcoord;
    uint8_t* color;
    uint8_t* normal;
    uint8_t* viewPos;
};

using STriangleSetupInput = SAttributeStreamPtrs;
using STriangleSetupOutput = SAttributeStreamPtrs;

typedef void (*VertexTransformFunctionPtr)( const uint8_t*, const uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t );
typedef void (*PerspectiveDivisionFunctionPtr)( const uint8_t*, const uint8_t*, SAttributeStreamPtrs, uint32_t, uint32_t, uint32_t, uint32_t );
typedef void (*TriangleSetupFunctionPtr)( const STriangleSetupInput&, const uint8_t*, uint32_t, STriangleSetupOutput, uint32_t, uint32_t, uint32_t );
typedef void (*RasterizingFunctionPtr)( STriangleSetupOutput, uint32_t, uint32_t );

static VertexTransformFunctionPtr s_VertexTransformFunctionTable[ VERTEX_TRANSFORM_FUNCTION_TABLE_SIZE ] = {};
static PerspectiveDivisionFunctionPtr s_PerspectiveDivisionFunctionTable[ PERSPECTIVE_DIVISION_FUNCTION_TABLE_SIZE ] = {};
static TriangleSetupFunctionPtr s_TriangleSetupFunctionTable[ TRIANGLE_SETUP_FUNCTION_TABLE_SIZE ] = {};
static RasterizingFunctionPtr s_RasterizingFunctionTable[ RASTERIZING_FUNCTION_TABLE_SIZE ] = {};

static SPipelineState s_PipelineState;
static VertexTransformFunctionPtr s_VertexTransformFunction = nullptr;
static PerspectiveDivisionFunctionPtr s_PerspectiveDivisionFunction = nullptr;
static TriangleSetupFunctionPtr s_TriangleSetupFunction = nullptr;
static RasterizingFunctionPtr s_RasterizingFunction = nullptr;

static bool s_EnableDepthWrite = true;
static ECullMode s_CullMode = ECullMode::eCullCW;

static SMatrix s_WorldViewMatrix =
    {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
static SMatrix s_NormalMatrix =
    {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
static SMatrix s_ProjectionMatrix =
    {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
static SMatrix s_WorldViewProjectionMatrix = 
    {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f,
    };
static uint8_t s_AlphaRef = 0;
static SMaterial s_Material = { { 1.f, 1.f, 1.f, 1.f }, { 1.f, 1.f, 1.f }, 32.f };
static SLight s_Light;

static SViewport s_Viewport = { 0 };
static int32_t s_RasterCoordStartX = 0;
static int32_t s_RasterCoordStartY = 0;
static int32_t s_RasterCoordEndX = 0;
static int32_t s_RasterCoordEndY = 0;

static SStream s_StreamSourcePos;
static SStream s_StreamSourceTex;
static SStream s_StreamSourceColor;
static SStream s_StreamSourceNormal;
static SStream s_StreamSourceIndex;
static EIndexType s_IndexType = EIndexType::e16bit;

static SImage s_RenderTarget = { 0 };
static SImage s_DepthTarget = { 0 };
static SImage s_Texture = { 0 };

static inline __m128 GatherMatrixColumn( const SMatrix& m, uint32_t column )
{
    assert( column < 4 );
    SFloat4A float4;
    const float* base = m.m_Data + column;
    float4.m_Data[ 0 ] = base[ 0 ];
    float4.m_Data[ 1 ] = base[ 4 ];
    float4.m_Data[ 2 ] = base[ 8 ];
    float4.m_Data[ 3 ] = base[ 12 ];
    return _mm_load_ps( float4.m_Data );
}

static inline float SSEDotFloat4( __m128 a, __m128 b )
{
    __m128 mul = _mm_mul_ps( a, b ); // mul = [A|B|C|D]
    __m128 shuf = _mm_shuffle_ps( mul, mul, _MM_SHUFFLE( 2, 3, 0, 1 ) ); // shuf = [B|A|D|C]
    __m128 sums = _mm_add_ps( mul, shuf ); // sums = [A+B|B+A|C+D|D+C]
    shuf = _mm_movehl_ps( shuf, sums ); // shuf = [B|A|A+B|B+A]
    sums = _mm_add_ss( sums, shuf ); // sums = [A+B|B+A|C+D|D+C+B+A]
    return _mm_cvtss_f32( sums );
}

static SMatrix MatrixMultiply4x3( const SMatrix& lhs, const SMatrix& rhs )
{
    __m128 r0 = _mm_load_ps( lhs.m_Data );
    __m128 r1 = _mm_load_ps( lhs.m_Data + 4 );
    __m128 r2 = _mm_load_ps( lhs.m_Data + 8 );
    __m128 r3 = _mm_load_ps( lhs.m_Data + 12 );
    __m128 c0 = GatherMatrixColumn( rhs, 0 );
    __m128 c1 = GatherMatrixColumn( rhs, 1 );
    __m128 c2 = GatherMatrixColumn( rhs, 2 );

    SMatrix result;
    result.m_00 = SSEDotFloat4( r0, c0 );
    result.m_01 = SSEDotFloat4( r0, c1 );
    result.m_02 = SSEDotFloat4( r0, c2 );
    result.m_03 = 0.f;
    result.m_10 = SSEDotFloat4( r1, c0 );
    result.m_11 = SSEDotFloat4( r1, c1 );
    result.m_12 = SSEDotFloat4( r1, c2 );
    result.m_13 = 0.f;
    result.m_20 = SSEDotFloat4( r2, c0 );
    result.m_21 = SSEDotFloat4( r2, c1 );
    result.m_22 = SSEDotFloat4( r2, c2 );
    result.m_23 = 0.f;
    result.m_30 = SSEDotFloat4( r3, c0 );
    result.m_31 = SSEDotFloat4( r3, c1 );
    result.m_32 = SSEDotFloat4( r3, c2 );
    result.m_33 = 1.f;
    return result;
}

static SMatrix MatrixMultiply4x4( const SMatrix& lhs, const SMatrix& rhs )
{
    __m128 r0 = _mm_load_ps( lhs.m_Data );
    __m128 r1 = _mm_load_ps( lhs.m_Data + 4 );
    __m128 r2 = _mm_load_ps( lhs.m_Data + 8 );
    __m128 r3 = _mm_load_ps( lhs.m_Data + 12 );
    __m128 c0 = GatherMatrixColumn( rhs, 0 );
    __m128 c1 = GatherMatrixColumn( rhs, 1 );
    __m128 c2 = GatherMatrixColumn( rhs, 2 );
    __m128 c3 = GatherMatrixColumn( rhs, 3 );

    SMatrix result;
    result.m_00 = SSEDotFloat4( r0, c0 );
    result.m_01 = SSEDotFloat4( r0, c1 );
    result.m_02 = SSEDotFloat4( r0, c2 );
    result.m_03 = SSEDotFloat4( r0, c3 );
    result.m_10 = SSEDotFloat4( r1, c0 );
    result.m_11 = SSEDotFloat4( r1, c1 );
    result.m_12 = SSEDotFloat4( r1, c2 );
    result.m_13 = SSEDotFloat4( r1, c3 );
    result.m_20 = SSEDotFloat4( r2, c0 );
    result.m_21 = SSEDotFloat4( r2, c1 );
    result.m_22 = SSEDotFloat4( r2, c2 );
    result.m_23 = SSEDotFloat4( r2, c3 );
    result.m_30 = SSEDotFloat4( r3, c0 );
    result.m_31 = SSEDotFloat4( r3, c1 );
    result.m_32 = SSEDotFloat4( r3, c2 );
    result.m_33 = SSEDotFloat4( r3, c3 );
    return result;
}

static void UpdateNormalMatrix()
{
    // TODO: This is incorrect if world matrix contains non-uniform scaling
    s_NormalMatrix = s_WorldViewMatrix;
}

static void UpdateWorldViewProjectionMatrix()
{
    s_WorldViewProjectionMatrix = MatrixMultiply4x4( s_WorldViewMatrix, s_ProjectionMatrix );
}

static inline __m128 __vectorcall GatherFloat4( const uint8_t* stream, uint32_t stride )
{
    SFloat4A float4;
    float4.m_Data[ 0 ] = *(float*)( stream );
    float4.m_Data[ 1 ] = *(float*)( stream + stride );
    float4.m_Data[ 2 ] = *(float*)( stream + stride + stride );
    float4.m_Data[ 3 ] = *(float*)( stream + stride + stride + stride );
    return _mm_load_ps( float4.m_Data );
}

static inline void __vectorcall ScatterFloat4( __m128 value, uint8_t* stream, uint32_t stride )
{
    SFloat4A float4;
    _mm_store_ps( float4.m_Data, value );
    *(float*)( stream ) = float4.m_Data[ 0 ];
    *(float*)( stream + stride ) = float4.m_Data[ 1 ];
    *(float*)( stream + stride + stride ) = float4.m_Data[ 2 ];
    *(float*)( stream + stride + stride + stride ) = float4.m_Data[ 3 ];
}

static inline void __vectorcall ScatterInt4( __m128i value, uint8_t* stream, uint32_t stride )
{
    SInt4A int4;
    _mm_store_si128( (__m128i*)int4.m_Data, value );
    *(int32_t*)( stream ) = int4.m_Data[ 0 ];
    *(int32_t*)( stream + stride ) = int4.m_Data[ 1 ];
    *(int32_t*)( stream + stride + stride ) = int4.m_Data[ 2 ];
    *(int32_t*)( stream + stride + stride + stride ) = int4.m_Data[ 3 ];
}

template <bool UseNormal, bool UseViewPos>
static void TransformVertices( 
    const uint8_t* inPos,
    const uint8_t* inNormal,
    uint8_t* outPos,
    uint8_t* outNormal,
    uint8_t* outViewPos,
    uint32_t posStride,
    uint32_t normalStride,
    uint32_t outStride,
    uint32_t count
    )
{
    assert( count % SIMD_WIDTH == 0 );
    const uint32_t batchCount = count / SIMD_WIDTH;

    __m128 m00 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 0 ] );
    __m128 m01 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 1 ] );
    __m128 m02 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 2 ] );
    __m128 m03 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 3 ] );
    __m128 m10 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 4 ] );
    __m128 m11 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 5 ] );
    __m128 m12 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 6 ] );
    __m128 m13 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 7 ] );
    __m128 m20 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 8 ] );
    __m128 m21 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 9 ] );
    __m128 m22 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 10 ] );
    __m128 m23 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 11 ] );
    __m128 m30 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 12 ] );
    __m128 m31 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 13 ] );
    __m128 m32 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 14 ] );
    __m128 m33 = _mm_set1_ps( s_WorldViewProjectionMatrix.m_Data[ 15 ] );

    __m128 n00, n01, n02, n10, n11, n12, n20, n21, n22, n30, n31, n32;
    if ( UseViewPos )
    {
        n00 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 0 ] );
        n01 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 1 ] );
        n02 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 2 ] );
        n10 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 4 ] );
        n11 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 5 ] );
        n12 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 6 ] );
        n20 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 8 ] );
        n21 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 9 ] );
        n22 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 10 ] );
        n30 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 12 ] );
        n31 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 13 ] );
        n32 = _mm_set1_ps( s_WorldViewMatrix.m_Data[ 14 ] );
    }

    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = GatherFloat4( inPos, posStride );
        __m128 y = GatherFloat4( sizeof( float ) + inPos, posStride );
        __m128 z = GatherFloat4( sizeof( float ) * 2 + inPos, posStride );
        __m128 dotX, dotY, dotZ, dotW;

        // Clip space position
        SIMDMath::Vec3DotVec4( x, y, z, m00, m10, m20, m30, dotX );
        SIMDMath::Vec3DotVec4( x, y, z, m01, m11, m21, m31, dotY );
        SIMDMath::Vec3DotVec4( x, y, z, m02, m12, m22, m32, dotZ );
        SIMDMath::Vec3DotVec4( x, y, z, m03, m13, m23, m33, dotW );
        ScatterFloat4( dotX, outPos, outStride );
        ScatterFloat4( dotY, sizeof( float ) + outPos, outStride );
        ScatterFloat4( dotZ, sizeof( float ) * 2 + outPos, outStride );
        ScatterFloat4( dotW, sizeof( float ) * 3 + outPos, outStride );

        inPos += SIMD_WIDTH * posStride;
        outPos += SIMD_WIDTH * outStride;

        if ( UseViewPos )
        { 
            // Get view space position
            SIMDMath::Vec3DotVec4( x, y, z, n00, n10, n20, n30, dotX );
            SIMDMath::Vec3DotVec4( x, y, z, n01, n11, n21, n31, dotY );
            SIMDMath::Vec3DotVec4( x, y, z, n02, n12, n22, n32, dotZ );
            ScatterFloat4( dotX, outViewPos, outStride );
            ScatterFloat4( dotY, sizeof(float) + outViewPos, outStride );
            ScatterFloat4( dotZ, sizeof(float) * 2 + outViewPos, outStride );

            outViewPos += SIMD_WIDTH * outStride;
        }
    }

    if ( UseNormal )
    { 
        m00 = _mm_set1_ps( s_NormalMatrix.m_Data[ 0 ] );
        m01 = _mm_set1_ps( s_NormalMatrix.m_Data[ 1 ] );
        m02 = _mm_set1_ps( s_NormalMatrix.m_Data[ 2 ] );
        m10 = _mm_set1_ps( s_NormalMatrix.m_Data[ 4 ] );
        m11 = _mm_set1_ps( s_NormalMatrix.m_Data[ 5 ] );
        m12 = _mm_set1_ps( s_NormalMatrix.m_Data[ 6 ] );
        m20 = _mm_set1_ps( s_NormalMatrix.m_Data[ 8 ] );
        m21 = _mm_set1_ps( s_NormalMatrix.m_Data[ 9 ] );
        m22 = _mm_set1_ps( s_NormalMatrix.m_Data[ 10 ] );

        for ( uint32_t i = 0; i < batchCount; ++i )
        {
            __m128 x = GatherFloat4( inNormal, normalStride );
            __m128 y = GatherFloat4( sizeof( float ) + inNormal, normalStride );
            __m128 z = GatherFloat4( sizeof( float ) * 2 + inNormal, normalStride );
            __m128 dotX, dotY, dotZ;
            SIMDMath::Vec3DotVec3( x, y, z, m00, m10, m20, dotX );
            SIMDMath::Vec3DotVec3( x, y, z, m01, m11, m21, dotY );
            SIMDMath::Vec3DotVec3( x, y, z, m02, m12, m22, dotZ );
            ScatterFloat4( dotX, outNormal, outStride );
            ScatterFloat4( dotY, sizeof( float ) + outNormal, outStride );
            ScatterFloat4( dotZ, sizeof( float ) * 2 + outNormal, outStride );

            inNormal += SIMD_WIDTH * normalStride;
            outNormal += SIMD_WIDTH * outStride;
        }
    }
}

inline static void ReadTriangleIndices( const uint8_t* indices, uint32_t location, uint32_t stride, uint32_t* i0, uint32_t* i1, uint32_t* i2 )
{
    indices += location * stride;
    const uint8_t* indexPtr0 = indices;
    const uint8_t* indexPtr1 = indices + stride;
    const uint8_t* indexPtr2 = indices + stride + stride;
    if ( s_IndexType == EIndexType::e16bit )
    {
        *i0 = *( (uint16_t*)indexPtr0 );
        *i1 = *( (uint16_t*)indexPtr1 );
        *i2 = *( (uint16_t*)indexPtr2 );
    }
    else
    {
        *i0 = *( (uint32_t*)indexPtr0 );
        *i1 = *( (uint32_t*)indexPtr1 );
        *i2 = *( (uint32_t*)indexPtr2 );
    }
}

inline static void WriteTriangleIndices( uint8_t* indices, uint32_t location, uint32_t stride, uint32_t i0, uint32_t i1, uint32_t i2 )
{
    indices += location * stride;
    uint8_t* indexPtr0 = indices;
    uint8_t* indexPtr1 = indices + stride;
    uint8_t* indexPtr2 = indices + stride + stride;
    if ( s_IndexType == EIndexType::e16bit )
    {
        *( (uint16_t*)indexPtr0 ) = (uint16_t)( i0 );
        *( (uint16_t*)indexPtr1 ) = (uint16_t)( i1 );
        *( (uint16_t*)indexPtr2 ) = (uint16_t)( i2 );
    }
    else
    {
        *( (uint32_t*)indexPtr0 ) = i0;
        *( (uint32_t*)indexPtr1 ) = i1;
        *( (uint32_t*)indexPtr2 ) = i2;
    }
}

// Cull triangle who has any vertices passing by the near clip
static void CullTriangles( const uint8_t* inZ, const uint8_t* inIndices, uint8_t* outIndices, uint32_t inStride, uint32_t outStride, uint32_t inIndexStride,
    uint32_t outIndexStride, uint32_t trianglesCount, uint32_t& resultTriangleCount )
{
    resultTriangleCount = 0;
    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        uint32_t i0, i1, i2;
        if ( inIndices != nullptr )
        {
            ReadTriangleIndices( inIndices, i * 3, inIndexStride, &i0, &i1, &i2 );
        }
        else
        {
            i0 = i * 3;
            i1 = i * 3 + 1;
            i2 = i * 3 + 2;
        }

        const uint32_t offset0 = i0 * inStride, offset1 = i1 * inStride, offset2 = i2 * inStride;
        // Read z as int, only the sign bit matters
        const int32_t z0 = *(int32_t*)( inZ + offset0 );
        const int32_t z1 = *(int32_t*)( inZ + offset1 );
        const int32_t z2 = *(int32_t*)( inZ + offset2 );
        const bool isCulled = ( z0 | z1 | z2 ) < 0;
        if ( !isCulled )
        {
            WriteTriangleIndices( outIndices, resultTriangleCount * 3, outIndexStride, i0, i1, i2 );
            ++resultTriangleCount;
        }
    }
}

template <bool UseTexture, bool UseVertexColor, bool UseNormal, bool UseViewPos>
static void PerspectiveDivision( const uint8_t* inTex, const uint8_t* inColor,
    SAttributeStreamPtrs streamPtrs,
    uint32_t stride, uint32_t texStride, uint32_t colorStride,
    uint32_t count )
{
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || UseNormal;

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
        __m128 x = GatherFloat4( streamPtrs.pos, stride );
        __m128 y = GatherFloat4( sizeof( float ) + streamPtrs.pos, stride );
        __m128 z = GatherFloat4( streamPtrs.z, stride );
        __m128 w = GatherFloat4( streamPtrs.w, stride );
        __m128 one = _mm_set1_ps( 1.f );
        __m128 half = _mm_set1_ps( .5f );
        __m128 rcpw = _mm_div_ps( one, w );

        x = _mm_fmadd_ps( x, rcpw, one ); // x = x / w - (-1)
        x = _mm_fmadd_ps( x, vHalfRasterizerWidth, half ); // Add 0.5 for rounding
        __m128i xi = _mm_cvttps_epi32( _mm_floor_ps( x ) );
        xi = _mm_add_epi32( xi, vOffsetX );

        y = _mm_fmadd_ps( y, rcpw, one ); // y = y / w - (-1)
        y = _mm_fmadd_ps( y, vHalfRasterizerHeight, half ); // Add 0.5 for rounding
        __m128i yi = _mm_cvttps_epi32( _mm_floor_ps( y ) );
        yi = _mm_add_epi32( yi, vOffsetY );

        z = _mm_mul_ps( z, rcpw );

        ScatterInt4( xi, streamPtrs.pos, stride );
        ScatterInt4( yi, sizeof( int32_t ) + streamPtrs.pos, stride );
        ScatterFloat4( z, streamPtrs.z, stride );

        if ( NeedRcpw )
        { 
            ScatterFloat4( rcpw, streamPtrs.rcpw, stride );
        }

        streamPtrs.pos += SIMD_WIDTH * stride;
        streamPtrs.z += SIMD_WIDTH * stride;
        streamPtrs.w += SIMD_WIDTH * stride;

        if ( UseTexture )
        {
            __m128 texU = GatherFloat4( inTex, texStride );
            __m128 texV = GatherFloat4( sizeof( float ) + inTex, texStride );
            texU = _mm_mul_ps( texU, rcpw );
            texV = _mm_mul_ps( texV, rcpw );
            ScatterFloat4( texU, streamPtrs.texcoord, stride );
            ScatterFloat4( texV, sizeof( float ) + streamPtrs.texcoord, stride );
            inTex += SIMD_WIDTH * texStride;
            streamPtrs.texcoord += SIMD_WIDTH * stride;
        }
        
        if ( UseVertexColor )
        {
            __m128 colorR = GatherFloat4( inColor, colorStride );
            __m128 colorG = GatherFloat4( sizeof( float ) + inColor, colorStride );
            __m128 colorB = GatherFloat4( sizeof( float ) * 2 + inColor, colorStride );
            colorR = _mm_mul_ps( colorR, rcpw );
            colorG = _mm_mul_ps( colorG, rcpw );
            colorB = _mm_mul_ps( colorB, rcpw );
            ScatterFloat4( colorR, streamPtrs.color, stride );
            ScatterFloat4( colorG, sizeof( float ) + streamPtrs.color, stride );
            ScatterFloat4( colorB, sizeof( float ) * 2 + streamPtrs.color, stride );
            inColor += SIMD_WIDTH * colorStride;
            streamPtrs.color += SIMD_WIDTH * stride;
        }
        
        if ( UseNormal )
        {
            __m128 normalX = GatherFloat4( streamPtrs.normal, stride );
            __m128 normalY = GatherFloat4( sizeof( float ) + streamPtrs.normal, stride );
            __m128 normalZ = GatherFloat4( sizeof( float ) * 2 + streamPtrs.normal, stride );
            normalX = _mm_mul_ps( normalX, rcpw );
            normalY = _mm_mul_ps( normalY, rcpw );
            normalZ = _mm_mul_ps( normalZ, rcpw );
            ScatterFloat4( normalX, streamPtrs.normal, stride );
            ScatterFloat4( normalY, sizeof( float ) + streamPtrs.normal, stride );
            ScatterFloat4( normalZ, sizeof( float ) * 2 + streamPtrs.normal, stride );
            streamPtrs.normal += SIMD_WIDTH * stride;
        }
        
        if ( UseViewPos )
        {
            __m128 viewPosX = GatherFloat4( streamPtrs.viewPos, stride );
            __m128 viewPosY = GatherFloat4( sizeof( float ) + streamPtrs.viewPos, stride );
            __m128 viewPosZ = GatherFloat4( sizeof( float ) * 2 + streamPtrs.viewPos, stride );
            viewPosX = _mm_mul_ps( viewPosX, rcpw );
            viewPosY = _mm_mul_ps( viewPosY, rcpw );
            viewPosZ = _mm_mul_ps( viewPosZ, rcpw );
            ScatterFloat4( viewPosX, streamPtrs.viewPos, stride );
            ScatterFloat4( viewPosY, sizeof( float ) + streamPtrs.viewPos, stride );
            ScatterFloat4( viewPosZ, sizeof( float ) * 2 + streamPtrs.viewPos, stride );
            streamPtrs.viewPos += SIMD_WIDTH * stride;
        }
    }
}

struct SVertex
{
    explicit SVertex( const uint8_t* data )
    {
        x = ( (int32_t*)data )[ 0 ];
        y = ( (int32_t*)data )[ 1 ];
    }

    int32_t x, y;
};

static inline bool IsTopLeftEdge( const SVertex& v0, const SVertex& v1 )
{
    bool isTop = v0.y == v1.y && v0.x > v1.x;
    bool isLeft = v0.y > v1.y;
    return isLeft || isTop;
}

static inline float BarycentricInterplation( float attr0, float attr1, float attr2, float w0, float w1, float w2 )
{
    return attr0 * w0 + ( attr1 * w1 + ( attr2 * w2 ) );
}

template <bool UseTexcoord, bool UseColor, bool UseNormal, bool UseViewPos>
static void SetupTriangles( const STriangleSetupInput& input,
    const uint8_t* indices, uint32_t indexStride,
    STriangleSetupOutput output,
    uint32_t inputStride, uint32_t outputStride, uint32_t trianglesCount )
{
    constexpr bool UseRcpw = UseTexcoord || UseColor || UseNormal || UseViewPos;

    int32_t cullSign = s_CullMode == ECullMode::eCullCW ? 0 : 0x80000000;

    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        uint32_t i0, i1, i2;
        ReadTriangleIndices( indices, i * 3, indexStride, &i0, &i1, &i2 );

        const uint32_t offset0 = i0 * inputStride, offset1 = i1 * inputStride, offset2 = i2 * inputStride;
        const SVertex v0( input.pos + offset0 ), v1( input.pos + offset1 ), v2( input.pos + offset2 );

        STriangleBaseAttributes* baseAttrs = (STriangleBaseAttributes*)output.base;

        int32_t a01 = v0.y - v1.y, b01 = v1.x - v0.x, c01 = v0.x * v1.y - v0.y * v1.x;
        // Compute the signed area of the triangle for barycentric coordinates normalization
        const int32_t doubleSignedArea = a01 * v2.x + b01 * v2.y + c01; // Plug v2 into the edge function of edge01
        const int32_t faceSign = doubleSignedArea & 0x80000000;
        baseAttrs->faceSign = faceSign >> 24; // 32bit to 8bit
        cullSign = s_CullMode == ECullMode::eNone ? faceSign : cullSign; // If cull mode is none, each triangle overrides the cull sign with its facing.
        // Early out if the triangle facing is different than the cull facing
        if ( ( cullSign ^ doubleSignedArea ) >= 0 )
        {
            const float rcpDoubleSignedArea = 1.0f / doubleSignedArea;

            int32_t a12 = v1.y - v2.y, b12 = v2.x - v1.x, c12 = v1.x * v2.y - v1.y * v2.x;
            int32_t a20 = v2.y - v0.y, b20 = v0.x - v2.x, c20 = v2.x * v0.y - v2.y * v0.x;

            // Calculate bounding box of the triangle and crop with the viewport
            int32_t minX = std::max( s_RasterCoordStartX, std::min( v0.x, std::min( v1.x, v2.x ) ) );
            int32_t minY = std::max( s_RasterCoordStartY, std::min( v0.y, std::min( v1.y, v2.y ) ) );
            int32_t maxX = std::min( s_RasterCoordEndX, std::max( v0.x, std::max( v1.x, v2.x ) ) );
            int32_t maxY = std::min( s_RasterCoordEndY, std::max( v0.y, std::max( v1.y, v2.y ) ) );
            // Round up the minimum of the bounding box to the nearest pixel center
            minX = MathHelper::DivideAndRoundUp( minX - s_RasterCoordStartX, s_SubpixelStep ) * s_SubpixelStep + s_RasterCoordStartX;
            minY = MathHelper::DivideAndRoundUp( minY - s_RasterCoordStartY, s_SubpixelStep ) * s_SubpixelStep + s_RasterCoordStartY;
            // Compute the image coordinate where the minimum of the bounding box is
            const int32_t imgMinX = s_Viewport.m_Left + ( minX - s_RasterCoordStartX ) / s_SubpixelStep;
            int32_t imgY = s_Viewport.m_Top + s_Viewport.m_Height - ( minY - s_RasterCoordStartY ) / s_SubpixelStep - 1; // Image axis y is flipped

            int32_t w0_row = a12 * minX + b12 * minY + c12;
            int32_t w1_row = a20 * minX + b20 * minY + c20;
            int32_t w2_row = a01 * minX + b01 * minY + c01;

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

            // Apply top left rule
            // The following bias computing are facing agnostic, because if the triangle is CW
            // 1) The IsTopLeftEdge test gives opposite result (IsBottomRightEdge) and...
            // 2) top left edge bias should be -1, which yields positive (inside) when XOR'ed with the negative face sign and...
            // 3) non-top left edge bias should be 0, which yields negative (outside) when XOR'ed with the negative face sign
            // which is the same as if the triangle is CCW
            const int32_t topLeftBias0 = IsTopLeftEdge( v1, v2 ) ? 0 : -1;
            const int32_t topLeftBias1 = IsTopLeftEdge( v2, v0 ) ? 0 : -1;
            const int32_t topLeftBias2 = IsTopLeftEdge( v0, v1 ) ? 0 : -1;
            w0_row += topLeftBias0;
            w1_row += topLeftBias1;
            w2_row += topLeftBias2;

            // Write all base attributes
            baseAttrs->minX = minX; baseAttrs->maxX = maxX;
            baseAttrs->minY = minY; baseAttrs->maxY = maxY;
            baseAttrs->imgMinX = imgMinX; baseAttrs->imgY = imgY;
            baseAttrs->w0_row = w0_row; baseAttrs->w1_row = w1_row; baseAttrs->w2_row = w2_row;
            baseAttrs->a01 = a01; baseAttrs->a12 = a12; baseAttrs->a20 = a20;
            baseAttrs->b01 = b01; baseAttrs->b12 = b12; baseAttrs->b20 = b20;

#define SETUP_ATTRIBUTE( name, offset, condition ) \
            if ( condition ) \
            { \
                STriangleAttribute* dstAttr = (STriangleAttribute*)output.##name; \
                dstAttr += offset; \
                float attr0 = *( (float*)( input.##name + offset0 ) + offset ); \
                float attr1 = *( (float*)( input.##name + offset1 ) + offset ); \
                float attr2 = *( (float*)( input.##name + offset2 ) + offset ); \
                dstAttr->row = BarycentricInterplation( attr0, attr1, attr2, bw0_row, bw1_row, bw2_row ); \
                dstAttr->a = BarycentricInterplation( attr0, attr1, attr2, ba12, ba20, ba01 ); \
                dstAttr->b = BarycentricInterplation( attr0, attr1, attr2, bb12, bb20, bb01 ); \
            }

            SETUP_ATTRIBUTE( z, 0, true )
    
            SETUP_ATTRIBUTE( rcpw, 0, UseRcpw )

            SETUP_ATTRIBUTE( texcoord, 0, UseTexcoord )
            SETUP_ATTRIBUTE( texcoord, 1, UseTexcoord )

            SETUP_ATTRIBUTE( color, 0, UseColor )
            SETUP_ATTRIBUTE( color, 1, UseColor )
            SETUP_ATTRIBUTE( color, 2, UseColor )

            SETUP_ATTRIBUTE( normal, 0, UseNormal )
            SETUP_ATTRIBUTE( normal, 1, UseNormal )
            SETUP_ATTRIBUTE( normal, 2, UseNormal )

            SETUP_ATTRIBUTE( viewPos, 0, UseViewPos )
            SETUP_ATTRIBUTE( viewPos, 1, UseViewPos )
            SETUP_ATTRIBUTE( viewPos, 2, UseViewPos )

#undef SETUP_ATTRIBUTE
        }

        output.base += outputStride;
        output.z += outputStride;
        if ( UseRcpw ) output.rcpw += outputStride;
        if ( UseTexcoord ) output.texcoord += outputStride;
        if ( UseColor ) output.color += outputStride;
        if ( UseNormal ) output.normal += outputStride;
        if ( UseViewPos ) output.viewPos += outputStride;
    }
}

template <bool UseTexture, bool UseVertexColor, ELightingModel LightingModel, ELightType LightType, bool EnableAlphaTest, bool EnableAlphaBlend>
static void RasterizeTriangles( STriangleSetupOutput input, uint32_t inputStride, uint32_t trianglesCount )
{
    constexpr bool NeedLighting = LightingModel != ELightingModel::eUnlit;
    constexpr bool NeedViewPos = NeedLighting && ( LightingModel == ELightingModel::eBlinnPhong || LightType == ELightType::ePoint );
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || NeedLighting;

    int32_t cullSign = s_CullMode == ECullMode::eCullCW ? 0 : 0x80000000;

    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        STriangleBaseAttributes* base = (STriangleBaseAttributes*)input.base;

        // Fetch all base attributes
        const int32_t minX = base->minX, maxX = base->maxX, minY = base->minY, maxY = base->maxY;
        int32_t imgMinX = base->imgMinX, imgY = base->imgY;
        int32_t w0_row = base->w0_row, w1_row = base->w1_row, w2_row = base->w2_row;
        const int32_t a01 = base->a01, a12 = base->a12, a20 = base->a20;
        const int32_t b01 = base->b01, b12 = base->b12, b20 = base->b20;

        const int32_t faceSign = base->faceSign << 24; // 8bit to 32bit
        cullSign = s_CullMode == ECullMode::eNone ? faceSign : cullSign; // If cull mode is none, each triangle overrides the cull sign with its facing.
        // Early out if the triangle facing is different than the cull facing
        if ( ( faceSign ^ cullSign ) < 0 )
        {
            goto NextTriangle;
        }

#define FETCH_ATTRIBUTE( dstName, srcName, offset, condition ) \
        float dstName##_row, dstName##_a, dstName##_b; \
        if ( condition ) \
        { \
            STriangleAttribute* attr = (STriangleAttribute*)input.##srcName; \
            attr += offset; \
            dstName##_row = attr->row; \
            dstName##_a = attr->a; \
            dstName##_b = attr->b; \
        }

        FETCH_ATTRIBUTE( z, z, 0, true )
    
        FETCH_ATTRIBUTE( rcpw, rcpw, 0, NeedRcpw )

        FETCH_ATTRIBUTE( texU_w, texcoord, 0, UseTexture )
        FETCH_ATTRIBUTE( texV_w, texcoord, 1, UseTexture )

        FETCH_ATTRIBUTE( colorR_w, color, 0, UseVertexColor )
        FETCH_ATTRIBUTE( colorG_w, color, 1, UseVertexColor )
        FETCH_ATTRIBUTE( colorB_w, color, 2, UseVertexColor )

        FETCH_ATTRIBUTE( normalX_w, normal, 0, NeedLighting )
        FETCH_ATTRIBUTE( normalY_w, normal, 1, NeedLighting )
        FETCH_ATTRIBUTE( normalZ_w, normal, 2, NeedLighting )

        FETCH_ATTRIBUTE( viewPosX_w, viewPos, 0, NeedViewPos )
        FETCH_ATTRIBUTE( viewPosY_w, viewPos, 1, NeedViewPos )
        FETCH_ATTRIBUTE( viewPosZ_w, viewPos, 2, NeedViewPos )

#undef FETCH_ATTRIBUTE

        int32_t pX, pY;
        for ( pY = minY; pY <= maxY; pY += s_SubpixelStep, imgY -= 1 )
        {
            int32_t w0 = w0_row;
            int32_t w1 = w1_row;
            int32_t w2 = w2_row;

            int32_t imgX = imgMinX;

#define ROW_INIT_ATTRIBUTE( name, condition ) \
            float name; \
            if ( condition ) \
            { \
                name = name##_row; \
            }

            ROW_INIT_ATTRIBUTE( z, true )
        
            ROW_INIT_ATTRIBUTE( rcpw, NeedRcpw )

            ROW_INIT_ATTRIBUTE( texU_w, UseTexture )
            ROW_INIT_ATTRIBUTE( texV_w, UseTexture )

            ROW_INIT_ATTRIBUTE( colorR_w, UseVertexColor )
            ROW_INIT_ATTRIBUTE( colorG_w, UseVertexColor )
            ROW_INIT_ATTRIBUTE( colorB_w, UseVertexColor )

            ROW_INIT_ATTRIBUTE( normalX_w, NeedLighting )
            ROW_INIT_ATTRIBUTE( normalY_w, NeedLighting )
            ROW_INIT_ATTRIBUTE( normalZ_w, NeedLighting )

            ROW_INIT_ATTRIBUTE( viewPosX_w, NeedViewPos )
            ROW_INIT_ATTRIBUTE( viewPosY_w, NeedViewPos )
            ROW_INIT_ATTRIBUTE( viewPosZ_w, NeedViewPos )

#undef ROW_INIT_ATTRIBUTE

            for ( pX = minX; pX <= maxX; pX += s_SubpixelStep, imgX += 1 )
            {
                if ( ( ( faceSign ^ w0 ) | ( faceSign ^ w1 ) | ( faceSign ^ w2 ) ) >= 0 ) // "Inside" fragments yields positive
                {
                    float* dstDepth = (float*)s_DepthTarget.m_Bits + imgY * s_DepthTarget.m_Width + imgX;
                    if ( z < *dstDepth )
                    {
                        if ( !EnableAlphaTest && s_EnableDepthWrite )
                        {
                            *dstDepth = z;
                        }

                        float w = 0.f;
                        if ( NeedRcpw )
                        {
                            w = 1.0f / rcpw;
                        }
                    
                        float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
                        if ( UseTexture )
                        { 
                            float texU = texU_w * w;
                            float texV = texV_w * w;
                            SampleTexture_PointClamp( s_Texture, texU, texV, &r, &g, &b, &a );
                        }

                        if ( UseVertexColor )
                        {
                            float vertexColorR = colorR_w * w;
                            float vertexColorG = colorG_w * w;
                            float vertexColorB = colorB_w * w;
                            r *= vertexColorR;
                            g *= vertexColorG;
                            b *= vertexColorB;
                        }

                        r *= s_Material.m_Diffuse.m_X;
                        g *= s_Material.m_Diffuse.m_Y;
                        b *= s_Material.m_Diffuse.m_Z;
                        a *= s_Material.m_Diffuse.m_W;

                        if ( EnableAlphaTest )
                        { 
                            const uint8_t a8 = uint8_t( a * 255.f + 0.5f );
                            if ( a8 < s_AlphaRef )
                            {
                                goto NextFragment;
                            }

                            if ( s_EnableDepthWrite )
                            { 
                                *dstDepth = z;
                            }
                        }

                        if ( NeedLighting )
                        {
                            float normalX = normalX_w * w;
                            float normalY = normalY_w * w;
                            float normalZ = normalZ_w * w;
                            // Re-normalize the normal
                            float length = normalX * normalX + normalY * normalY + normalZ * normalZ;
                            float rcpDenorm = 1.0f / sqrtf( length );
                            normalX *= rcpDenorm;
                            normalY *= rcpDenorm;
                            normalZ *= rcpDenorm;

                            float viewPosX, viewPosY, viewPosZ;
                            if ( NeedViewPos )
                            { 
                                viewPosX = viewPosX_w * w;
                                viewPosY = viewPosY_w * w;
                                viewPosZ = viewPosZ_w * w;
                            }

                            float lightVecX, lightVecY, lightVecZ, lightDistanceSqr;
                            if ( LightType == ELightType::eDirectional )
                            {
                                lightVecX = s_Light.m_Position.m_X;
                                lightVecY = s_Light.m_Position.m_Y;
                                lightVecZ = s_Light.m_Position.m_Z;
                            }
                            else if ( LightType == ELightType::ePoint )
                            {
                                lightVecX = s_Light.m_Position.m_X - viewPosX;
                                lightVecY = s_Light.m_Position.m_Y - viewPosY;
                                lightVecZ = s_Light.m_Position.m_Z - viewPosZ;
                                lightDistanceSqr = lightVecX * lightVecX + lightVecY * lightVecY + lightVecZ * lightVecZ;
                                // Normalize the light vector
                                rcpDenorm = 1.f / sqrtf( lightDistanceSqr );
                                lightVecX *= rcpDenorm;
                                lightVecY *= rcpDenorm;
                                lightVecZ *= rcpDenorm;
                            }

                            float NdotL = normalX * lightVecX + normalY * lightVecY + normalZ * lightVecZ;
                            NdotL = std::max( 0.f, NdotL );

                            const float lambertR = r * s_Light.m_Diffuse.m_X * NdotL;
                            const float lambertG = g * s_Light.m_Diffuse.m_Y * NdotL;
                            const float lambertB = b * s_Light.m_Diffuse.m_Z * NdotL;

                            float specularR = 0.f, specularG = 0.f, specularB = 0.f;
                            if ( LightingModel == ELightingModel::eBlinnPhong )
                            { 
                                float viewVecX = -viewPosX;
                                float viewVecY = -viewPosY;
                                float viewVecZ = -viewPosZ;
                                // Re-normalize the view vector
                                length = viewVecX * viewVecX + viewVecY * viewVecY + viewVecZ * viewVecZ;
                                rcpDenorm = 1.0f / sqrtf( length );
                                viewVecX *= rcpDenorm;
                                viewVecY *= rcpDenorm;
                                viewVecZ *= rcpDenorm;

                                float halfVecX = lightVecX + viewVecX;
                                float halfVecY = lightVecY + viewVecY;
                                float halfVecZ = lightVecZ + viewVecZ;
                                // Re-normalize the half vector
                                length = halfVecX * halfVecX + halfVecY * halfVecY + halfVecZ * halfVecZ;
                                rcpDenorm = 1.0f / sqrtf( length );
                                halfVecX *= rcpDenorm;
                                halfVecY *= rcpDenorm;
                                halfVecZ *= rcpDenorm;

                                float NdotH = normalX * halfVecX + normalY * halfVecY + normalZ * halfVecZ;
                                NdotH = std::max( 0.f, NdotH );

                                const float blinnPhong = NdotL > 0.f ? std::powf( NdotH, s_Material.m_Power ) : 0.f;
                                specularR = s_Material.m_Specular.m_X * s_Light.m_Specular.m_X * blinnPhong;
                                specularG = s_Material.m_Specular.m_Y * s_Light.m_Specular.m_Y * blinnPhong;
                                specularB = s_Material.m_Specular.m_Z * s_Light.m_Specular.m_Z * blinnPhong;
                            }

                            r = lambertR + specularR;
                            g = lambertG + specularG;
                            b = lambertB + specularB;
                        
                            if ( LightType == ELightType::ePoint )
                            {
                                const float rcpDistanceSqr = 1.f / lightDistanceSqr;
                                r *= rcpDistanceSqr;
                                g *= rcpDistanceSqr;
                                b *= rcpDistanceSqr;
                            }

                            r += s_Light.m_Ambient.m_X;
                            g += s_Light.m_Ambient.m_Y;
                            b += s_Light.m_Ambient.m_Z;
                        }

                        uint32_t* pixelPtr = (uint32_t*)s_RenderTarget.m_Bits + imgY * s_RenderTarget.m_Width + imgX;

                        if ( EnableAlphaBlend )
                        {
                            float dstR, dstG, dstB, dstA;
                            R8G8B8A8Unorm_To_Float( *pixelPtr, &dstR, &dstG, &dstB, &dstA );

                            r = ( r - dstR ) * a + dstR;
                            g = ( g - dstG ) * a + dstG;
                            b = ( b - dstB ) * a + dstB;
                        }

                        r = std::fmin( r, 1.f );
                        g = std::fmin( g, 1.f );
                        b = std::fmin( b, 1.f );

                        uint8_t r8 = uint8_t( r * 255.f + 0.5f );
                        uint8_t g8 = uint8_t( g * 255.f + 0.5f );
                        uint8_t b8 = uint8_t( b * 255.f + 0.5f );
                        *pixelPtr = 0xFF000000 | r8 << 16 | g8 << 8 | b8;
                    }
                }

NextFragment:
                w0 += a12;
                w1 += a20;
                w2 += a01;

#define ROW_INC_ATTRIBUTE( name, condition ) \
                if ( condition ) \
                { \
                    name += name##_a; \
                }

                ROW_INC_ATTRIBUTE( z, true )

                ROW_INC_ATTRIBUTE( rcpw, NeedRcpw )

                ROW_INC_ATTRIBUTE( texU_w, UseTexture )
                ROW_INC_ATTRIBUTE( texV_w, UseTexture )

                ROW_INC_ATTRIBUTE( colorR_w, UseVertexColor )
                ROW_INC_ATTRIBUTE( colorG_w, UseVertexColor )
                ROW_INC_ATTRIBUTE( colorB_w, UseVertexColor )

                ROW_INC_ATTRIBUTE( normalX_w, NeedLighting )
                ROW_INC_ATTRIBUTE( normalY_w, NeedLighting )
                ROW_INC_ATTRIBUTE( normalZ_w, NeedLighting )

                ROW_INC_ATTRIBUTE( viewPosX_w, NeedViewPos )
                ROW_INC_ATTRIBUTE( viewPosY_w, NeedViewPos )
                ROW_INC_ATTRIBUTE( viewPosZ_w, NeedViewPos )

#undef ROW_INC_ATTRIBUTE
            }

            w0_row += b12;
            w1_row += b20;
            w2_row += b01;

#define VERTICAL_INC_ATTRIBUTE( name, condition ) \
            if ( condition ) \
            { \
                name##_row += name##_b; \
            }

            VERTICAL_INC_ATTRIBUTE( z, true )

            VERTICAL_INC_ATTRIBUTE( rcpw, NeedRcpw )

            VERTICAL_INC_ATTRIBUTE( texU_w, UseTexture )
            VERTICAL_INC_ATTRIBUTE( texV_w, UseTexture )

            VERTICAL_INC_ATTRIBUTE( colorR_w, UseVertexColor )
            VERTICAL_INC_ATTRIBUTE( colorG_w, UseVertexColor )
            VERTICAL_INC_ATTRIBUTE( colorB_w, UseVertexColor )

            VERTICAL_INC_ATTRIBUTE( normalX_w, NeedLighting )
            VERTICAL_INC_ATTRIBUTE( normalY_w, NeedLighting )
            VERTICAL_INC_ATTRIBUTE( normalZ_w, NeedLighting )

            VERTICAL_INC_ATTRIBUTE( viewPosX_w, NeedViewPos )
            VERTICAL_INC_ATTRIBUTE( viewPosY_w, NeedViewPos )
            VERTICAL_INC_ATTRIBUTE( viewPosZ_w, NeedViewPos )

#undef VERTICAL_INC_ATTRIBUTE
        }

NextTriangle:
        input.base += inputStride;
        input.z += inputStride;
        if ( NeedRcpw ) input.rcpw += inputStride;
        if ( UseTexture ) input.texcoord += inputStride;
        if ( UseVertexColor ) input.color += inputStride;
        if ( NeedLighting ) input.normal += inputStride;
        if ( NeedViewPos ) input.viewPos += inputStride;
    }
}


static uint32_t MakeFunctionIndex_VertexTransform( bool useNormal, bool useViewPos )
{
    useViewPos = useNormal ? useViewPos : false;
    const uint32_t index = ( useNormal ? 0x1 : 0 ) | ( useViewPos ? 0x2 : 0 );
    assert( index < VERTEX_TRANSFORM_FUNCTION_TABLE_SIZE );
    return index;
}

static uint32_t MakeFunctionIndex_VertexTransform( const SPipelineState& state )
{
    return MakeFunctionIndex_VertexTransform( state.m_LightingModel != ELightingModel::eUnlit, state.m_LightingModel == ELightingModel::eBlinnPhong || state.m_LightType == ELightType::ePoint );
}

static uint32_t MakeFunctionIndex_PerspectiveDivision( bool useTexture, bool useColor, bool useNormal, bool useViewPos )
{
    useViewPos = useNormal ? useViewPos : false;
    const uint32_t index = ( useTexture ? 0x1 : 0 ) | ( useColor ? 0x2 : 0 ) | ( useNormal ? 0x4 : 0 ) | ( useViewPos ? 0x8 : 0 );
    assert( index < PERSPECTIVE_DIVISION_FUNCTION_TABLE_SIZE );
    return index;
}

static uint32_t MakeFunctionIndex_PerspectiveDivision( const SPipelineState& state )
{
    return MakeFunctionIndex_PerspectiveDivision( state.m_UseTexture, state.m_UseVertexColor, state.m_LightingModel != ELightingModel::eUnlit,
        state.m_LightingModel == ELightingModel::eBlinnPhong || state.m_LightType == ELightType::ePoint );
}

static uint32_t MakeFunctionIndex_TriangleSetup( bool useTexture, bool useColor, bool useNormal, bool useViewPos )
{
    useViewPos = useNormal ? useViewPos : false;
    const uint32_t index = ( useTexture ? 0x1 : 0 ) | ( useColor ? 0x2 : 0 ) | ( useNormal ? 0x4 : 0 ) | ( useViewPos ? 0x8 : 0 );
    assert( index < TRIANGLE_SETUP_FUNCTION_TABLE_SIZE );
    return index;
}

static uint32_t MakeFunctionIndex_TriangleSetup( const SPipelineState& state )
{
    return MakeFunctionIndex_TriangleSetup( state.m_UseTexture, state.m_UseVertexColor, state.m_LightingModel != ELightingModel::eUnlit,
        state.m_LightingModel == ELightingModel::eBlinnPhong || state.m_LightType == ELightType::ePoint );
}

static uint32_t MakeFunctionIndex_RasterizeTriangles( bool useTexture, bool useColor, ELightingModel lightingModel, ELightType lightType, bool enableAlphaTest, bool enableAlphaBlend )
{
    lightType = lightingModel != ELightingModel::eUnlit ? lightType : ELightType::eDirectional;
    const uint32_t index = ( useTexture ? 0x1 : 0 ) | ( useColor ? 0x2 : 0 ) | ( (uint32_t)lightingModel << 2 ) | ( (uint32_t)lightType << 4 ) | ( enableAlphaTest ? 0x20 : 0 ) | ( enableAlphaBlend ? 0x40 : 0 );
    assert( index < RASTERIZING_FUNCTION_TABLE_SIZE );
    return index;
}

static uint32_t MakeFunctionIndex_RasterizeTriangles( const SPipelineState& state )
{
    return MakeFunctionIndex_RasterizeTriangles( state.m_UseTexture, state.m_UseVertexColor, state.m_LightingModel, state.m_LightType, state.m_EnableAlphaTest, state.m_EnableAlphaBlend );
}

void Rasterizer::Initialize()
{
#define SET_VERTEX_TRANSFORM_FUNCTION_TABLE( useNormal, useViewPos ) \
    s_VertexTransformFunctionTable[ MakeFunctionIndex_VertexTransform( useNormal, useViewPos ) ] = TransformVertices<useNormal, useViewPos>;

    SET_VERTEX_TRANSFORM_FUNCTION_TABLE( false, false )
    SET_VERTEX_TRANSFORM_FUNCTION_TABLE( true, false )
    SET_VERTEX_TRANSFORM_FUNCTION_TABLE( true, true )
#undef SET_VERTEX_TRANSFORM_FUNCTION_TABLE

#define SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( useTexture, useColor, useNormal, useViewPos ) \
    s_PerspectiveDivisionFunctionTable[ MakeFunctionIndex_PerspectiveDivision( useTexture, useColor, useNormal, useViewPos ) ] = PerspectiveDivision<useTexture, useColor, useNormal, useViewPos>;

    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, false, false, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, false, true, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, false, true, true )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, true, false, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, true, true, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( false, true, true, true )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, false, false, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, false, true, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, false, true, true )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, true, false, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, true, true, false )
    SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE( true, true, true, true )
#undef SET_PERSPECTIVE_DIVISION_FUNCTION_TABLE

#define SET_TRIANGLE_SETUP_FUNCTION_TABLE( useTexture, useColor, useNormal, useViewPos ) \
    s_TriangleSetupFunctionTable[ MakeFunctionIndex_TriangleSetup( useTexture, useColor, useNormal, useViewPos ) ] = SetupTriangles<useTexture, useColor, useNormal, useViewPos>;

    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, false, false, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, false, true, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, false, true, true )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, true, false, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, true, true, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( false, true, true, true )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, false, false, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, false, true, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, false, true, true )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, true, false, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, true, true, false )
    SET_TRIANGLE_SETUP_FUNCTION_TABLE( true, true, true, true )
#undef SET_TRIANGLE_SETUP_FUNCTION_TABLE

#define SET_RASTERIZING_FUNCTION_TABLE( useTexture, useColor, lightingModel, lightType, enableAlphaTest, enableAlphaBlend ) \
    s_RasterizingFunctionTable[ MakeFunctionIndex_RasterizeTriangles( useTexture, useColor, lightingModel, lightType, enableAlphaTest, enableAlphaBlend ) ] = RasterizeTriangles<useTexture, useColor, lightingModel, lightType, enableAlphaTest, enableAlphaBlend>;
    
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eUnlit, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eUnlit, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eUnlit, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eUnlit, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::ePoint, false, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eUnlit, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eUnlit, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eUnlit, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eUnlit, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::ePoint, true, false );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eUnlit, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eUnlit, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eUnlit, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eUnlit, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::ePoint, false, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eUnlit, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eLambert, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, false, ELightingModel::eBlinnPhong, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eUnlit, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eLambert, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( false, true, ELightingModel::eBlinnPhong, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eUnlit, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eLambert, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, false, ELightingModel::eBlinnPhong, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eUnlit, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eLambert, ELightType::ePoint, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::eDirectional, true, true );
    SET_RASTERIZING_FUNCTION_TABLE( true, true, ELightingModel::eBlinnPhong, ELightType::ePoint, true, true );

#undef SET_RASTERIZING_FUNCTION_TABLE
}

void Rasterizer::SetPositionStream( const SStream& stream )
{
    s_StreamSourcePos = stream;
}

void Rasterizer::SetNormalStream( const SStream& stream )
{
    s_StreamSourceNormal = stream;
}

void Rasterizer::SetTexcoordStream( const SStream& stream )
{
    s_StreamSourceTex = stream;
}

void Rasterizer::SetColorStream( const SStream& stream )
{
    s_StreamSourceColor = stream;
}

void Rasterizer::SetIndexStream( const SStream& indices )
{
    s_StreamSourceIndex = indices;
}

void Rasterizer::SetWorldViewTransform( const SMatrix& matrix )
{
    s_WorldViewMatrix = matrix;
    UpdateNormalMatrix();
    UpdateWorldViewProjectionMatrix();
}

void Rasterizer::SetProjectionTransform( const SMatrix& matrix )
{
    s_ProjectionMatrix = matrix;
    UpdateWorldViewProjectionMatrix();
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

void Rasterizer::SetMaterialDiffuse( SVector4 color )
{
    s_Material.m_Diffuse = color;
}

void Rasterizer::SetMaterial( const SMaterial& material )
{
    s_Material = material;
}

void Rasterizer::SetLight( const SLight& light )
{
    s_Light = light;
}

void Rasterizer::SetTexture( const SImage& image )
{
    s_Texture = image;
}

void Rasterizer::SetAlphaRef( uint8_t value )
{
    s_AlphaRef = value;
}

void Rasterizer::SetEnableDepthWrite( bool enable )
{
    s_EnableDepthWrite = enable;
}

void Rasterizer::SetCullMode( ECullMode mode )
{
    s_CullMode = mode;
}

void Rasterizer::SetIndexType( EIndexType type )
{
    s_IndexType = type;
}

void Rasterizer::SetPipelineState( const SPipelineState& state )
{
    s_PipelineState = state;
    s_VertexTransformFunction = s_VertexTransformFunctionTable[ MakeFunctionIndex_VertexTransform( state ) ];
    s_PerspectiveDivisionFunction = s_PerspectiveDivisionFunctionTable[ MakeFunctionIndex_PerspectiveDivision( state ) ];
    s_TriangleSetupFunction = s_TriangleSetupFunctionTable[ MakeFunctionIndex_TriangleSetup( state ) ];
    s_RasterizingFunction = s_RasterizingFunctionTable[ MakeFunctionIndex_RasterizeTriangles( state ) ];
}

struct SAttributesLayout
{
    uint32_t size;
    uint32_t zOffset;
    uint32_t rcpwOffset;
    uint32_t texcoordOffset;
    uint32_t colorOffset;
    uint32_t normalOffset;
    uint32_t viewPosOffset;
};

static SAttributesLayout ComputeAttributesLayout( const SPipelineState& pipelineState, uint32_t baseSize, bool forceKeepW, uint32_t multiplier )
{
    SAttributesLayout layout;

    const bool needTexcoord = pipelineState.m_UseTexture;
    const bool needColor = pipelineState.m_UseVertexColor;
    const bool needNormal = pipelineState.m_LightingModel != ELightingModel::eUnlit;
    const bool needRcpw = forceKeepW || needTexcoord || needColor || needNormal;

    layout.size = baseSize;

    layout.zOffset = layout.size;
    layout.size += sizeof( float ) * multiplier;
    
    if ( needRcpw )
    { 
        layout.rcpwOffset = layout.size;
        layout.size += sizeof( float ) * multiplier;
    }

    layout.texcoordOffset = layout.size;
    if ( needTexcoord )
    {
        layout.size += sizeof( float ) * 2 * multiplier;
    }

    layout.colorOffset = layout.size;
    if ( needColor )
    {
        layout.size += sizeof( float ) * 3 * multiplier;
    }

    layout.normalOffset = layout.size;
    if ( needNormal )
    {
        layout.size += sizeof( float ) * 3 * multiplier;
    }

    layout.viewPosOffset = layout.size;
    if ( needNormal && ( pipelineState.m_LightingModel == ELightingModel::eBlinnPhong || pipelineState.m_LightType == ELightType::ePoint ) )
    {
        layout.size += sizeof( float ) * 3 * multiplier;
    }

    return layout;
}

SAttributeStreamPtrs GetAttributeStreamPointers( uint8_t* stream, const SAttributesLayout& layout )
{
    SAttributeStreamPtrs ptrs;
    ptrs.pos = stream;
    ptrs.z = stream + layout.zOffset;
    ptrs.rcpw = stream + layout.rcpwOffset;
    ptrs.texcoord = stream + layout.texcoordOffset;
    ptrs.color = stream + layout.colorOffset;
    ptrs.normal = stream + layout.normalOffset;
    ptrs.viewPos = stream + layout.viewPosOffset;
    return ptrs;
}

static void InternalDraw( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount, bool useIndex )
{
    const uint32_t verticesCount = s_StreamSourcePos.m_Size / s_StreamSourcePos.m_Stride; // It is caller's responsibility to make sure other streams contains same numbers of vertices
    const uint32_t roundedUpVerticesCount = MathHelper::DivideAndRoundUp( verticesCount, (uint32_t)SIMD_WIDTH ) * SIMD_WIDTH;

    // Allocate intermediate vertices buffer
    const SAttributesLayout vertexLayout = ComputeAttributesLayout( s_PipelineState, sizeof( float ) * 2, true, 1 ); // Keeping w to store the z from vertex transform
    uint8_t* vertices = (uint8_t*)malloc( vertexLayout.size * roundedUpVerticesCount );
    SAttributeStreamPtrs vertexStreamPtrs = GetAttributeStreamPointers( vertices, vertexLayout );
    
    // Vertex transform
    {
        const uint8_t* inPos = s_StreamSourcePos.m_Data + s_StreamSourcePos.m_Offset + s_StreamSourcePos.m_Stride * baseVertexLocation;
        const uint8_t* inNormal = s_StreamSourceNormal.m_Data + s_StreamSourceNormal.m_Offset + s_StreamSourceNormal.m_Stride * baseVertexLocation;
        s_VertexTransformFunction( inPos, inNormal, vertexStreamPtrs.pos, vertexStreamPtrs.normal, vertexStreamPtrs.viewPos, 
            s_StreamSourcePos.m_Stride, s_StreamSourceNormal.m_Stride, vertexLayout.size, roundedUpVerticesCount );
    }

    // Allocate intermediate triangle buffer
    // Every triangle attribute needs 3 float: row start, row increment and vertical increment
    const SAttributesLayout triangleLayout = ComputeAttributesLayout( s_PipelineState, sizeof( STriangleBaseAttributes ), false, 3 ); 
    uint8_t* triangles = (uint8_t*)malloc( triangleLayout.size * trianglesCount );
    SAttributeStreamPtrs triangleStreamPtrs = GetAttributeStreamPointers( triangles, triangleLayout );

    const uint8_t* sourceIndices = useIndex ? s_StreamSourceIndex.m_Data + s_StreamSourceIndex.m_Offset + s_StreamSourceIndex.m_Stride * baseIndexLocation : nullptr;
    const uint32_t indexStride = s_IndexType == EIndexType::e16bit ? 2 : 4;
    uint8_t* indices = (uint8_t*)malloc( indexStride * trianglesCount * 3 );

    // Triangle cull
    {
        uint32_t newTrianglesCount = 0;
        CullTriangles( vertexStreamPtrs.z, sourceIndices, indices, vertexLayout.size, triangleLayout.size, s_StreamSourceIndex.m_Stride, indexStride, trianglesCount, newTrianglesCount );
        trianglesCount = newTrianglesCount;
    }

    // Perspective division
    {
        const uint8_t* inTexcoordStream = s_StreamSourceTex.m_Data + s_StreamSourceTex.m_Offset + s_StreamSourceTex.m_Stride * baseVertexLocation;
        const uint8_t* inColorStream = s_StreamSourceColor.m_Data + s_StreamSourceColor.m_Offset + s_StreamSourceColor.m_Stride * baseVertexLocation;
        s_PerspectiveDivisionFunction( inTexcoordStream, inColorStream, vertexStreamPtrs, vertexLayout.size,
            s_StreamSourceTex.m_Stride, s_StreamSourceColor.m_Stride, roundedUpVerticesCount );
    }

    // Triangle setup
    {
        s_TriangleSetupFunction( vertexStreamPtrs, indices, indexStride, triangleStreamPtrs, vertexLayout.size, triangleLayout.size, trianglesCount );
    }

    free( indices );
    free( vertices );

    // Rasterize triangles
    {
        s_RasterizingFunction( triangleStreamPtrs, triangleLayout.size, trianglesCount );
    }

    free( triangles );
}

void Rasterizer::Draw( uint32_t baseVertexIndex, uint32_t trianglesCount )
{
    InternalDraw( baseVertexIndex, 0, trianglesCount, false );
}

void Rasterizer::DrawIndexed( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount )
{
    InternalDraw( baseVertexLocation, baseIndexLocation, trianglesCount, true );
}
