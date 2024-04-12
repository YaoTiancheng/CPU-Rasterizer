#include "PCH.h"
#include "Rasterizer.h"
#include "ImageOps.inl"
#include "SIMDMath.inl"
#include "MathHelper.h"

#define SIMD_WIDTH 4
#define MAX_PIPELINE_STATE_COUNT 12

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

struct SRasterizerVertex
{
    explicit SRasterizerVertex( const uint8_t* data )
    {
        m_X = ( (int32_t*)data )[ 0 ];
        m_Y = ( (int32_t*)data )[ 1 ];
        m_Z = ( (float*)data )[ 2 ];
    }
    void SetRcpW( const uint8_t* rcpw ) 
    { 
        m_Rcpw = *(float*)rcpw; 
    }
    void SetTexcoords( const uint8_t* data )
    { 
        m_TexcoordU = ( (float*)data )[ 0 ];
        m_TexcoordV = ( (float*)data )[ 1 ]; 
    }
    void SetColor( const uint8_t* data ) 
    { 
        m_ColorR = ( (float*)data )[ 0 ]; 
        m_ColorG = ( (float*)data )[ 1 ]; 
        m_ColorB = ( (float*)data )[ 2 ]; 
    }
    void SetNormal( const uint8_t* data ) 
    { 
        m_NormalX = ( (float*)data )[ 0 ]; 
        m_NormalY = ( (float*)data )[ 1 ];
        m_NormalZ = ( (float*)data )[ 2 ];
    }
    void SetViewPos( const uint8_t* data ) 
    { 
        m_ViewPosX = ( (float*)data )[ 0 ];
        m_ViewPosY = ( (float*)data )[ 1 ];
        m_ViewPosZ = ( (float*)data )[ 2 ];
    }

    int32_t m_X, m_Y;
    float m_Z;
    float m_Rcpw;
    float m_TexcoordU, m_TexcoordV;
    float m_ColorR, m_ColorG, m_ColorB;
    float m_NormalX, m_NormalY, m_NormalZ;
    float m_ViewPosX, m_ViewPosY, m_ViewPosZ;
};

typedef void (*VertexTransformFunctionPtr)( const uint8_t*, const uint8_t*, uint8_t*, uint8_t*, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t );
typedef void (*PerspectiveDivisionFunctionPtr)( uint8_t*, uint8_t*, uint8_t*, const uint8_t*, uint8_t*, const uint8_t*, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t );
typedef void (*RasterizeFunctionPtr)( const uint8_t*, const uint8_t*, const uint8_t*, const uint8_t*, const uint8_t*, uint32_t, const uint32_t*, uint32_t );

struct SPipelineFunctionPointers
{
    VertexTransformFunctionPtr m_VertexTransformFunction;
    PerspectiveDivisionFunctionPtr m_PerspectiveDivisionFunction;
    RasterizeFunctionPtr m_RasterizerFunction;
    RasterizeFunctionPtr m_RasterizerFunctionIndexed;
};

static SPipelineFunctionPointers s_PipelineFunctionPtrsTable[ MAX_PIPELINE_STATE_COUNT ] = { 0 };

static SPipelineState s_PipelineState;
static SPipelineFunctionPointers s_PipelineFunctionPtrs;

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
static const uint32_t* s_StreamSourceIndex = nullptr;

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

template <bool NeedLighting>
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
    if ( NeedLighting )
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

        if ( NeedLighting )
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

    if ( NeedLighting )
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

template <bool UseTexture, bool UseVertexColor, bool UseNormal, bool UseViewPos>
static void PerspectiveDivision( uint8_t* pos, uint8_t* normal, uint8_t* viewPos,
    const uint8_t* inTex, uint8_t* outTex,
    const uint8_t* inColor, uint8_t* outColor,
    uint32_t stride, uint32_t texStride, uint32_t colorStride,
    uint32_t count )
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
        __m128 x = GatherFloat4( pos, stride );
        __m128 y = GatherFloat4( sizeof( float ) + pos, stride );
        __m128 z = GatherFloat4( sizeof( float ) * 2 + pos, stride );
        __m128 w = GatherFloat4( sizeof( float ) * 3 + pos, stride );
        __m128 one = { 1.f, 1.f, 1.f, 1.f };
        __m128 rcpw = _mm_div_ps( one, w );
        __m128 half = { .5f, .5f, .5f, .5f };
        __m128 texU, texV;
        if ( UseTexture )
        {
            texU = GatherFloat4( inTex, texStride );
            texV = GatherFloat4( sizeof( float ) + inTex, texStride );
        }
        __m128 colorR, colorG, colorB;
        if ( UseVertexColor )
        {
            colorR = GatherFloat4( inColor, colorStride );
            colorG = GatherFloat4( sizeof( float ) + inColor, colorStride );
            colorB = GatherFloat4( sizeof( float ) * 2 + inColor, colorStride );
        }
        __m128 normalX, normalY, normalZ;
        if ( UseNormal )
        {
            normalX = GatherFloat4( normal, stride );
            normalY = GatherFloat4( sizeof( float ) + normal, stride );
            normalZ = GatherFloat4( sizeof( float ) * 2 + normal, stride );
        }
        __m128 viewPosX, viewPosY, viewPosZ;
        if ( UseViewPos )
        {
            viewPosX = GatherFloat4( viewPos, stride );
            viewPosY = GatherFloat4( sizeof( float ) + viewPos, stride );
            viewPosZ = GatherFloat4( sizeof( float ) * 2 + viewPos, stride );
        }

        x = _mm_fmadd_ps( x, rcpw, one ); // x = x / w - (-1)
        x = _mm_fmadd_ps( x, vHalfRasterizerWidth, half ); // Add 0.5 for rounding
        __m128i xi = _mm_cvttps_epi32( _mm_floor_ps( x ) );
        xi = _mm_add_epi32( xi, vOffsetX );

        y = _mm_fmadd_ps( y, rcpw, one ); // y = y / w - (-1)
        y = _mm_fmadd_ps( y, vHalfRasterizerHeight, half ); // Add 0.5 for rounding
        __m128i yi = _mm_cvttps_epi32( _mm_floor_ps( y ) );
        yi = _mm_add_epi32( yi, vOffsetY );

        z = _mm_mul_ps( z, rcpw );

        if ( UseTexture )
        { 
            texU = _mm_mul_ps( texU, rcpw );
            texV = _mm_mul_ps( texV, rcpw );
        }

        if ( UseVertexColor )
        {
            colorR = _mm_mul_ps( colorR, rcpw );
            colorG = _mm_mul_ps( colorG, rcpw );
            colorB = _mm_mul_ps( colorB, rcpw );
        }

        if ( UseNormal )
        {
            normalX = _mm_mul_ps( normalX, rcpw );
            normalY = _mm_mul_ps( normalY, rcpw );
            normalZ = _mm_mul_ps( normalZ, rcpw );
        }

        if ( UseViewPos )
        {
            viewPosX = _mm_mul_ps( viewPosX, rcpw );
            viewPosY = _mm_mul_ps( viewPosY, rcpw );
            viewPosZ = _mm_mul_ps( viewPosZ, rcpw );
        }

        ScatterInt4( xi, pos, stride );
        ScatterInt4( yi, sizeof( int32_t ) + pos, stride );
        ScatterFloat4( z, sizeof( int32_t ) * 2 + pos, stride );

        constexpr bool NeedRcpw = UseTexture || UseVertexColor || UseNormal;
        if ( NeedRcpw )
        { 
            ScatterFloat4( rcpw, sizeof( int32_t ) * 2 + sizeof( float ) + pos, stride );
        }

        if ( UseTexture )
        { 
            ScatterFloat4( texU, outTex, stride );
            ScatterFloat4( texV, sizeof( float ) + outTex, stride );
        }

        if ( UseVertexColor )
        {
            ScatterFloat4( colorR, outColor, stride );
            ScatterFloat4( colorG, sizeof( float ) + outColor, stride );
            ScatterFloat4( colorB, sizeof( float ) * 2 + outColor, stride );
        }

        if ( UseNormal )
        {
            ScatterFloat4( normalX, normal, stride );
            ScatterFloat4( normalY, sizeof( float ) + normal, stride );
            ScatterFloat4( normalZ, sizeof( float ) * 2 + normal, stride );
        }

        if ( UseViewPos )
        {
            ScatterFloat4( viewPosX, viewPos, stride );
            ScatterFloat4( viewPosY, sizeof( float ) + viewPos, stride );
            ScatterFloat4( viewPosZ, sizeof( float ) * 2 + viewPos, stride );
        }

        pos += SIMD_WIDTH * stride;

        if ( UseTexture )
        { 
            inTex += SIMD_WIDTH * texStride;
            outTex += SIMD_WIDTH * stride;
        }

        if ( UseVertexColor )
        {
            inColor += SIMD_WIDTH * colorStride;
            outColor += SIMD_WIDTH * stride;
        }

        if ( UseNormal )
        {
            normal += SIMD_WIDTH * stride;
        }

        if ( UseViewPos )
        {
            viewPos += SIMD_WIDTH * stride;
        }
    }
}

static inline bool IsTopLeftEdge( const SRasterizerVertex& v0, const SRasterizerVertex& v1 )
{
    bool isTop = v0.m_Y == v1.m_Y && v0.m_X > v1.m_X;
    bool isLeft = v0.m_Y > v1.m_Y;
    return isLeft || isTop;
}

static inline float BarycentricInterplation( float attr0, float attr1, float attr2, float w0, float w1, float w2 )
{
    return attr0 * w0 + ( attr1 * w1 + ( attr2 * w2 ) );
}

template <bool UseTexture, bool UseVertexColor, ELightType LightType>
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

#define SETUP_ATTRIBUTE( name, a0, a1, a2, condition ) \
    float name##_row, name##_a, name##_b; \
    if ( condition ) \
    { \
        name##_row = BarycentricInterplation( a0, a1, a2, bw0_row, bw1_row, bw2_row ); \
        name##_a = BarycentricInterplation( a0, a1, a2, ba12, ba20, ba01 ); \
        name##_b = BarycentricInterplation( a0, a1, a2, bb12, bb20, bb01 ); \
    }

    constexpr bool NeedLighting = LightType != ELightType::eInvalid;
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || NeedLighting;

    SETUP_ATTRIBUTE( z, v0.m_Z, v1.m_Z, v2.m_Z, true )
    
    SETUP_ATTRIBUTE( rcpw, v0.m_Rcpw, v1.m_Rcpw, v2.m_Rcpw, NeedRcpw )

    SETUP_ATTRIBUTE( texU_w, v0.m_TexcoordU, v1.m_TexcoordU, v2.m_TexcoordU, UseTexture )
    SETUP_ATTRIBUTE( texV_w, v0.m_TexcoordV, v1.m_TexcoordV, v2.m_TexcoordV, UseTexture )

    SETUP_ATTRIBUTE( colorR_w, v0.m_ColorR, v1.m_ColorR, v2.m_ColorR, UseVertexColor )
    SETUP_ATTRIBUTE( colorG_w, v0.m_ColorG, v1.m_ColorG, v2.m_ColorG, UseVertexColor )
    SETUP_ATTRIBUTE( colorB_w, v0.m_ColorB, v1.m_ColorB, v2.m_ColorB, UseVertexColor )

    SETUP_ATTRIBUTE( normalX_w, v0.m_NormalX, v1.m_NormalX, v2.m_NormalX, NeedLighting )
    SETUP_ATTRIBUTE( normalY_w, v0.m_NormalY, v1.m_NormalY, v2.m_NormalY, NeedLighting )
    SETUP_ATTRIBUTE( normalZ_w, v0.m_NormalZ, v1.m_NormalZ, v2.m_NormalZ, NeedLighting )

    SETUP_ATTRIBUTE( viewPosX_w, v0.m_ViewPosX, v1.m_ViewPosX, v2.m_ViewPosX, NeedLighting )
    SETUP_ATTRIBUTE( viewPosY_w, v0.m_ViewPosY, v1.m_ViewPosY, v2.m_ViewPosY, NeedLighting )
    SETUP_ATTRIBUTE( viewPosZ_w, v0.m_ViewPosZ, v1.m_ViewPosZ, v2.m_ViewPosZ, NeedLighting )

#undef SETUP_ATTRIBUTE

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

        ROW_INIT_ATTRIBUTE( viewPosX_w, NeedLighting )
        ROW_INIT_ATTRIBUTE( viewPosY_w, NeedLighting )
        ROW_INIT_ATTRIBUTE( viewPosZ_w, NeedLighting )

#undef ROW_INIT_ATTRIBUTE

        for ( pX = minX; pX <= maxX; pX += s_SubpixelStep, imgX += 1 )
        {
            if ( ( w0 | w1 | w2 ) >= 0 ) // counter-clockwise triangle has positive area
            {
                float* dstDepth = (float*)s_DepthTarget.m_Bits + imgY * s_DepthTarget.m_Width + imgX;
                if ( z < *dstDepth )
                {
                    *dstDepth = z;

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

                        float viewPosX = viewPosX_w * w;
                        float viewPosY = viewPosY_w * w;
                        float viewPosZ = viewPosZ_w * w;
                        float viewVecX = -viewPosX;
                        float viewVecY = -viewPosY;
                        float viewVecZ = -viewPosZ;

                        // Re-normalize the view vector
                        length = viewVecX * viewVecX + viewVecY * viewVecY + viewVecZ * viewVecZ;
                        rcpDenorm = 1.0f / sqrtf( length );
                        viewVecX *= rcpDenorm;
                        viewVecY *= rcpDenorm;
                        viewVecZ *= rcpDenorm;

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

                        const float lambertR = r * s_Light.m_Diffuse.m_X * NdotL;
                        const float lambertG = g * s_Light.m_Diffuse.m_Y * NdotL;
                        const float lambertB = b * s_Light.m_Diffuse.m_Z * NdotL;
                        const float blinnPhong = NdotL > 0.f ? std::powf( NdotH, s_Material.m_Power ) : 0.f;
                        const float specularR = s_Material.m_Specular.m_X * s_Light.m_Specular.m_X * blinnPhong;
                        const float specularG = s_Material.m_Specular.m_Y * s_Light.m_Specular.m_Y * blinnPhong;
                        const float specularB = s_Material.m_Specular.m_Z * s_Light.m_Specular.m_Z * blinnPhong;

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

                    r = std::fmin( r, 1.f );
                    g = std::fmin( g, 1.f );
                    b = std::fmin( b, 1.f );

                    uint8_t ri = uint8_t( r * 255.f + 0.5f );
                    uint8_t gi = uint8_t( g * 255.f + 0.5f );
                    uint8_t bi = uint8_t( b * 255.f + 0.5f );
                    uint32_t* dstColor = (uint32_t*)s_RenderTarget.m_Bits + imgY * s_RenderTarget.m_Width + imgX;
                    *dstColor = 0xFF000000 | ri << 16 | gi << 8 | bi;
                }
            }

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

            ROW_INC_ATTRIBUTE( viewPosX_w, NeedLighting )
            ROW_INC_ATTRIBUTE( viewPosY_w, NeedLighting )
            ROW_INC_ATTRIBUTE( viewPosZ_w, NeedLighting )

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

        VERTICAL_INC_ATTRIBUTE( viewPosX_w, NeedLighting )
        VERTICAL_INC_ATTRIBUTE( viewPosY_w, NeedLighting )
        VERTICAL_INC_ATTRIBUTE( viewPosZ_w, NeedLighting )

#undef VERTICAL_INC_ATTRIBUTE
    }
}

template <bool UseTexture, bool UseVertexColor, ELightType LightType, bool IsIndexed>
static void RasterizeTriangles( const uint8_t* pos, const uint8_t* texcoord, const uint8_t* color, const uint8_t* normal, const uint8_t* viewPos, uint32_t stride, const uint32_t* indices, uint32_t trianglesCount )
{
    constexpr bool NeedLighting = LightType != ELightType::eInvalid;
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || NeedLighting;

    const uint8_t* posW = pos + sizeof( int32_t ) * 2 + sizeof( float );

    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        uint32_t i0, i1, i2;
        if ( IsIndexed )
        { 
            i0 = indices[ i * 3 ];
            i1 = indices[ i * 3 + 1 ];
            i2 = indices[ i * 3 + 2 ];
        }
        else
        { 
            i0 = i * 3;
            i1 = i0 + 1;
            i2 = i1 + 1;
        }

        const uint32_t offset0 = i0 * stride;
        const uint32_t offset1 = i1 * stride;
        const uint32_t offset2 = i2 * stride;

        SRasterizerVertex v0( pos + offset0 );
        SRasterizerVertex v1( pos + offset1 );
        SRasterizerVertex v2( pos + offset2 );
        if ( NeedRcpw )
        {
            v0.SetRcpW( posW + offset0 );
            v1.SetRcpW( posW + offset1 );
            v2.SetRcpW( posW + offset2 );
        }
        if ( UseTexture )
        {   
            v0.SetTexcoords( texcoord + offset0 );
            v1.SetTexcoords( texcoord + offset1 );
            v2.SetTexcoords( texcoord + offset2 );
        }
        if ( UseVertexColor )
        {
            v0.SetColor( color + offset0 );
            v1.SetColor( color + offset1 );
            v2.SetColor( color + offset2 );
        }
        if ( NeedLighting )
        {
            v0.SetNormal( normal + offset0 );
            v1.SetNormal( normal + offset1 );
            v2.SetNormal( normal + offset2 );
            v0.SetViewPos( viewPos + offset0 );
            v1.SetViewPos( viewPos + offset1 );
            v2.SetViewPos( viewPos + offset2 );
        }
        RasterizeTriangle<UseTexture, UseVertexColor, LightType>( v0, v1 ,v2 );
    }
}


static uint32_t MakePipelineStateIndex( bool useTexture, bool useVertexColor, ELightType lightType )
{
    return ( useTexture ? 0x1 : 0 ) | ( useVertexColor ? 0x2 : 0 ) | ( lightType << 2 );
}

static uint32_t MakePipelineStateIndex( const SPipelineState& state )
{
    return MakePipelineStateIndex( state.m_UseTexture, state.m_UseVertexColor, state.m_LightType );
}

template <bool UseTexture, bool UseVertexColor, ELightType LightType>
SPipelineFunctionPointers GetPipelineFunctionPointers()
{
    constexpr bool NeedLighting = LightType != ELightType::eInvalid;
    SPipelineFunctionPointers ptrs;
    ptrs.m_VertexTransformFunction = TransformVertices<NeedLighting>;
    ptrs.m_PerspectiveDivisionFunction = PerspectiveDivision<UseTexture, UseVertexColor, NeedLighting, NeedLighting>;
    ptrs.m_RasterizerFunction = RasterizeTriangles<UseTexture, UseVertexColor, LightType, false>;
    ptrs.m_RasterizerFunctionIndexed = RasterizeTriangles<UseTexture, UseVertexColor, LightType, true>;
    return ptrs;
}

void Rasterizer::Initialize()
{
#define SET_PIPELINE_FUNCTION_POINTERS( useTexture, useVertexColor, lightType ) \
    { \
        uint32_t pipelineStateIndex = MakePipelineStateIndex( useTexture, useVertexColor, lightType ); \
        assert( pipelineStateIndex < MAX_PIPELINE_STATE_COUNT ); \
        s_PipelineFunctionPtrsTable[ pipelineStateIndex ] = GetPipelineFunctionPointers< useTexture, useVertexColor, lightType >(); \
    }

    SET_PIPELINE_FUNCTION_POINTERS( false, false, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( false, true, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( true, false, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( true, true, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( false, false, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( false, true, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( true, false, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( true, true, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( false, false, ELightType::ePoint )
    SET_PIPELINE_FUNCTION_POINTERS( false, true, ELightType::ePoint )
    SET_PIPELINE_FUNCTION_POINTERS( true, false, ELightType::ePoint )
    SET_PIPELINE_FUNCTION_POINTERS( true, true, ELightType::ePoint )
#undef SET_PIPELINE_FUNCTION_POINTERS
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

void Rasterizer::SetIndexStream( const uint32_t* indices )
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

void Rasterizer::SetPipelineState( const SPipelineState& state )
{
    s_PipelineState = state;
    s_PipelineFunctionPtrs = s_PipelineFunctionPtrsTable[ MakePipelineStateIndex( state ) ];
}

static uint32_t ComputeVertexLayout( const SPipelineState& pipelineState, uint32_t* texcoordOffset, uint32_t* colorOffset, uint32_t* normalOffset, uint32_t* viewPosOffset )
{
    uint32_t vertexSize = sizeof( float ) * 4;

    *texcoordOffset = vertexSize;
    if ( pipelineState.m_UseTexture )
    {
        vertexSize += sizeof( float ) * 2;
    }

    *colorOffset = vertexSize;
    if ( pipelineState.m_UseVertexColor )
    {
        vertexSize += sizeof( float ) * 3;
    }

    *normalOffset = vertexSize;
    if ( pipelineState.m_LightType != ELightType::eInvalid )
    {
        vertexSize += sizeof( float ) * 3;
    }

    *viewPosOffset = vertexSize;
    if ( pipelineState.m_LightType != ELightType::eInvalid )
    {
        vertexSize += sizeof( float ) * 3;
    }

    return vertexSize;
}

static void InternalDraw( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount, bool useIndex )
{
    const uint32_t verticesCount = s_StreamSourcePos.m_Size / s_StreamSourcePos.m_Stride; // It is caller's responsibility to make sure other streams contains same numbers of vertices
    const uint32_t roundedUpVerticesCount = MathHelper::DivideAndRoundUp( verticesCount, (uint32_t)SIMD_WIDTH ) * SIMD_WIDTH;

    // Compute intermediate vertex layout
    uint32_t texcoordOffset, colorOffset, normalOffset, viewPosOffset;
    const uint32_t vertexSize = ComputeVertexLayout( s_PipelineState, &texcoordOffset, &colorOffset, &normalOffset, &viewPosOffset );
    uint8_t* vertices = (uint8_t*)malloc( vertexSize * roundedUpVerticesCount );
    
    uint8_t* posStream = vertices;
    uint8_t* normalStream = vertices + normalOffset;
    uint8_t* viewPosStream = vertices + viewPosOffset;

    // Vertex transform
    {
        const uint8_t* inPos = s_StreamSourcePos.m_Data + s_StreamSourcePos.m_Offset + s_StreamSourcePos.m_Stride * baseVertexLocation;
        const uint8_t* inNormal = s_StreamSourceNormal.m_Data + s_StreamSourceNormal.m_Offset + s_StreamSourceNormal.m_Stride * baseVertexLocation;
        s_PipelineFunctionPtrs.m_VertexTransformFunction( inPos, inNormal, posStream, normalStream, viewPosStream, s_StreamSourcePos.m_Stride, s_StreamSourceNormal.m_Stride, vertexSize, roundedUpVerticesCount );
    }

    const uint8_t* sourceTexcoordStream = s_StreamSourceTex.m_Data + s_StreamSourceTex.m_Offset + s_StreamSourceTex.m_Stride * baseVertexLocation;
    const uint8_t* sourceColorStream = s_StreamSourceColor.m_Data + s_StreamSourceColor.m_Offset + s_StreamSourceColor.m_Stride * baseVertexLocation;
    uint8_t* texcoordStream = vertices + texcoordOffset;
    uint8_t* colorStream = vertices + colorOffset;
    s_PipelineFunctionPtrs.m_PerspectiveDivisionFunction( posStream, normalStream, viewPosStream,
        sourceTexcoordStream, texcoordStream,
        sourceColorStream, colorStream,
        vertexSize, s_StreamSourceTex.m_Stride, s_StreamSourceColor.m_Stride,
        roundedUpVerticesCount );

    if ( useIndex )
    { 
        s_PipelineFunctionPtrs.m_RasterizerFunctionIndexed( posStream, texcoordStream, colorStream, normalStream, viewPosStream, vertexSize, s_StreamSourceIndex + baseIndexLocation, trianglesCount );
    }
    else
    {
        s_PipelineFunctionPtrs.m_RasterizerFunction( posStream, texcoordStream, colorStream, normalStream, viewPosStream, vertexSize, nullptr, trianglesCount );
    }

    free( vertices );
}

void Rasterizer::Draw( uint32_t baseVertexIndex, uint32_t trianglesCount )
{
    InternalDraw( baseVertexIndex, 0, trianglesCount, false );
}

void Rasterizer::DrawIndexed( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount )
{
    InternalDraw( baseVertexLocation, baseIndexLocation, trianglesCount, true );
}
