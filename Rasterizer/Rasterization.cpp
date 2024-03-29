#include "PCH.h"
#include "Rasterization.h"
#include "ImageOps.inl"
#include "SIMDMath.inl"
#include "MathHelper.h"

#define SIMD_WIDTH 4
#define MAX_PIPELINE_STATE_COUNT 8

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

    int32_t m_X;
    int32_t m_Y;
    float m_Z;
    float m_Rcpw;
    float m_TexcoordU;
    float m_TexcoordV;
    float m_ColorR;
    float m_ColorG;
    float m_ColorB;
    float m_NormalX;
    float m_NormalY;
    float m_NormalZ;
};

typedef void (*TransformVerticesToRasterizerCoordinatesFunctionPtr)( uint8_t*, uint8_t*, const uint8_t*, uint8_t*, const uint8_t*, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t );
typedef void (*RasterizeFunctionPtr)( const uint8_t*, const uint8_t*, const uint8_t*, const uint8_t*, uint32_t, const uint32_t*, uint32_t );

struct SPipelineFunctionPointers
{
    TransformVerticesToRasterizerCoordinatesFunctionPtr m_TransformVerticesToRasterizerCoordinatesFunction;
    RasterizeFunctionPtr m_RasterizerFunction;
    RasterizeFunctionPtr m_RasterizerFunctionIndexed;
};

static SPipelineFunctionPointers s_PipelineFunctionPtrsTable[ MAX_PIPELINE_STATE_COUNT ] = { 0 };

static SPipelineState s_PipelineState;
static SPipelineFunctionPointers s_PipelineFunctionPtrs;

static float s_ViewProjectionMatrix[ 16 ] =
    { 
        1.f, 0.f, 0.f, 0.f, 
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f 
    };
static float s_NormalMatrix[ 9 ] =
    {
        1.f, 0.f, 0.f,
        0.f, 1.f, 0.f,
        0.f, 0.f, 1.f,
    };
static float s_BaseColor[ 4 ] = { 1.f, 1.f, 1.f, 1.f };
static float s_LightPosition[ 4 ] = { 0.f, 0.f, 0.f, 0.f };
static float s_LightColor[ 4 ] = { 1.f, 1.f, 1.f, 1.f };

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

static void __vectorcall TransformPos3Stream( __m128 m00, __m128 m01, __m128 m02, __m128 m03,
    __m128 m10, __m128 m11, __m128 m12, __m128 m13,
    __m128 m20, __m128 m21, __m128 m22, __m128 m23,
    __m128 m30, __m128 m31, __m128 m32, __m128 m33,
    const uint8_t* inPos, uint32_t inStride, uint32_t count,
    uint8_t* outPos, uint32_t outStride )
{
    assert( count % SIMD_WIDTH == 0 );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = GatherFloat4( inPos, inStride );
        __m128 y = GatherFloat4( sizeof( float ) + inPos, inStride );
        __m128 z = GatherFloat4( sizeof( float ) * 2 + inPos, inStride );
        __m128 dotX, dotY, dotZ, dotW;
        SIMDMath::Vec3DotVec4( x, y, z, m00, m10, m20, m30, dotX );
        SIMDMath::Vec3DotVec4( x, y, z, m01, m11, m21, m31, dotY );
        SIMDMath::Vec3DotVec4( x, y, z, m02, m12, m22, m32, dotZ );
        SIMDMath::Vec3DotVec4( x, y, z, m03, m13, m23, m33, dotW );
        ScatterFloat4( dotX, outPos, outStride );
        ScatterFloat4( dotY, sizeof( float ) + outPos, outStride );
        ScatterFloat4( dotZ, sizeof( float ) * 2 + outPos, outStride );
        ScatterFloat4( dotW, sizeof( float ) * 3 + outPos, outStride );

        inPos += SIMD_WIDTH * inStride;
        outPos += SIMD_WIDTH * outStride;
    }
}

static void __vectorcall AffineTransformVec3Stream( __m128 m00, __m128 m01, __m128 m02,
    __m128 m10, __m128 m11, __m128 m12,
    __m128 m20, __m128 m21, __m128 m22,
    const uint8_t* inVec, uint32_t inStride, uint32_t count,
    uint8_t* outVec, uint32_t outStride )
{
    assert( count % SIMD_WIDTH == 0 );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = GatherFloat4( inVec, inStride );
        __m128 y = GatherFloat4( sizeof( float ) + inVec, inStride );
        __m128 z = GatherFloat4( sizeof( float ) * 2 + inVec, inStride );
        __m128 dotX, dotY, dotZ;
        SIMDMath::Vec3DotVec3( x, y, z, m00, m10, m20, dotX );
        SIMDMath::Vec3DotVec3( x, y, z, m01, m11, m21, dotY );
        SIMDMath::Vec3DotVec3( x, y, z, m02, m12, m22, dotZ );
        ScatterFloat4( dotX, outVec, outStride );
        ScatterFloat4( dotY, sizeof( float ) + outVec, outStride );
        ScatterFloat4( dotZ, sizeof( float ) * 2 + outVec, outStride );

        inVec += SIMD_WIDTH * inStride;
        outVec += SIMD_WIDTH * outStride;
    }
}

template <bool UseTexture, bool UseVertexColor, bool UseNormal>
static void TransformVerticesToRasterizerCoordinates( uint8_t* pos, uint8_t* normal,
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

    float z_row = BarycentricInterplation( v0.m_Z, v1.m_Z, v2.m_Z, bw0_row, bw1_row, bw2_row ); // Z at the minimum of the bounding box
    float z_a = BarycentricInterplation( v0.m_Z, v1.m_Z, v2.m_Z, ba12, ba20, ba01 ); // Horizontal Z increment
    float z_b = BarycentricInterplation( v0.m_Z, v1.m_Z, v2.m_Z, bb12, bb20, bb01 ); // Vertical Z increment
    
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || LightType != ELightType::eInvalid;
    float rcpw_row, rcpw_a, rcpw_b;
    if ( NeedRcpw )
    { 
        rcpw_row = BarycentricInterplation( v0.m_Rcpw, v1.m_Rcpw, v2.m_Rcpw, bw0_row, bw1_row, bw2_row ); // rcpw at the minimum of the bounding box
        rcpw_a = BarycentricInterplation( v0.m_Rcpw, v1.m_Rcpw, v2.m_Rcpw, ba12, ba20, ba01 ); // Horizontal rcpw increment
        rcpw_b = BarycentricInterplation( v0.m_Rcpw, v1.m_Rcpw, v2.m_Rcpw, bb12, bb20, bb01 ); // Vertical rcpw increment
    }

    float texU_row, texU_a, texU_b;
    float texV_row, texV_a, texV_b;
    if ( UseTexture )
    { 
        texU_row = BarycentricInterplation( v0.m_TexcoordU, v1.m_TexcoordU, v2.m_TexcoordU, bw0_row, bw1_row, bw2_row ); // texU at the minimum of the bounding box
        texU_a = BarycentricInterplation( v0.m_TexcoordU, v1.m_TexcoordU, v2.m_TexcoordU, ba12, ba20, ba01 ); // Horizontal texU increment
        texU_b = BarycentricInterplation( v0.m_TexcoordU, v1.m_TexcoordU, v2.m_TexcoordU, bb12, bb20, bb01 ); // Vertical texU increment

        texV_row = BarycentricInterplation( v0.m_TexcoordV, v1.m_TexcoordV, v2.m_TexcoordV, bw0_row, bw1_row, bw2_row ); // texV at the minimum of the bounding box
        texV_a = BarycentricInterplation( v0.m_TexcoordV, v1.m_TexcoordV, v2.m_TexcoordV, ba12, ba20, ba01 ); // Horizontal texV increment
        texV_b = BarycentricInterplation( v0.m_TexcoordV, v1.m_TexcoordV, v2.m_TexcoordV, bb12, bb20, bb01 ); // Vertical texV increment
    }

    float colorR_row, colorR_a, colorR_b;
    float colorG_row, colorG_a, colorG_b;
    float colorB_row, colorB_a, colorB_b;
    if ( UseVertexColor )
    {
        colorR_row = BarycentricInterplation( v0.m_ColorR, v1.m_ColorR, v2.m_ColorR, bw0_row, bw1_row, bw2_row ); // colorR at the minimum of the bounding box
        colorR_a = BarycentricInterplation( v0.m_ColorR, v1.m_ColorR, v2.m_ColorR, ba12, ba20, ba01 ); // Horizontal colorR increment
        colorR_b = BarycentricInterplation( v0.m_ColorR, v1.m_ColorR, v2.m_ColorR, bb12, bb20, bb01 ); // Vertical colorR increment

        colorG_row = BarycentricInterplation( v0.m_ColorG, v1.m_ColorG, v2.m_ColorG, bw0_row, bw1_row, bw2_row ); // colorG at the minimum of the bounding box
        colorG_a = BarycentricInterplation( v0.m_ColorG, v1.m_ColorG, v2.m_ColorG, ba12, ba20, ba01 ); // Horizontal colorG increment
        colorG_b = BarycentricInterplation( v0.m_ColorG, v1.m_ColorG, v2.m_ColorG, bb12, bb20, bb01 ); // Vertical colorG increment

        colorB_row = BarycentricInterplation( v0.m_ColorB, v1.m_ColorB, v2.m_ColorB, bw0_row, bw1_row, bw2_row ); // colorB at the minimum of the bounding box
        colorB_a = BarycentricInterplation( v0.m_ColorB, v1.m_ColorB, v2.m_ColorB, ba12, ba20, ba01 ); // Horizontal colorB increment
        colorB_b = BarycentricInterplation( v0.m_ColorB, v1.m_ColorB, v2.m_ColorB, bb12, bb20, bb01 ); // Vertical colorB increment
    }

    float normalX_row, normalX_a, normalX_b;
    float normalY_row, normalY_a, normalY_b;
    float normalZ_row, normalZ_a, normalZ_b;
    if ( LightType != ELightType::eInvalid )
    {
        normalX_row = BarycentricInterplation( v0.m_NormalX, v1.m_NormalX, v2.m_NormalX, bw0_row, bw1_row, bw2_row ); // normalX at the minimum of the bounding box
        normalX_a = BarycentricInterplation( v0.m_NormalX, v1.m_NormalX, v2.m_NormalX, ba12, ba20, ba01 ); // Horizontal normalX increment
        normalX_b = BarycentricInterplation( v0.m_NormalX, v1.m_NormalX, v2.m_NormalX, bb12, bb20, bb01 ); // Vertical normalX increment

        normalY_row = BarycentricInterplation( v0.m_NormalY, v1.m_NormalY, v2.m_NormalY, bw0_row, bw1_row, bw2_row ); // normalY at the minimum of the bounding box
        normalY_a = BarycentricInterplation( v0.m_NormalY, v1.m_NormalY, v2.m_NormalY, ba12, ba20, ba01 ); // Horizontal normalY increment
        normalY_b = BarycentricInterplation( v0.m_NormalY, v1.m_NormalY, v2.m_NormalY, bb12, bb20, bb01 ); // Vertical normalY increment

        normalZ_row = BarycentricInterplation( v0.m_NormalZ, v1.m_NormalZ, v2.m_NormalZ, bw0_row, bw1_row, bw2_row ); // normalZ at the minimum of the bounding box
        normalZ_a = BarycentricInterplation( v0.m_NormalZ, v1.m_NormalZ, v2.m_NormalZ, ba12, ba20, ba01 ); // Horizontal normalZ increment
        normalZ_b = BarycentricInterplation( v0.m_NormalZ, v1.m_NormalZ, v2.m_NormalZ, bb12, bb20, bb01 ); // Vertical normalZ increment
    }

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

        float rcpw = 0.f;
        if ( NeedRcpw )
        {
            rcpw = rcpw_row;
        }

        float texU, texV;
        if ( UseTexture )
        { 
            texU = texU_row;
            texV = texV_row;
        }

        float colorR, colorG, colorB;
        if ( UseVertexColor )
        {
            colorR = colorR_row;
            colorG = colorG_row;
            colorB = colorB_row;
        }

        float normalX, normalY, normalZ;
        if ( LightType != ELightType::eInvalid )
        {
            normalX = normalX_row;
            normalY = normalY_row;
            normalZ = normalZ_row;
        }

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
                        float texcoordU = texU * w;
                        float texcoordV = texV * w;
                        SampleTexture_PointClamp( s_Texture, texcoordU, texcoordV, &r, &g, &b, &a );
                    }

                    if ( UseVertexColor )
                    {
                        float vertexColorR = colorR * w;
                        float vertexColorG = colorG * w;
                        float vertexColorB = colorB * w;
                        r *= vertexColorR;
                        g *= vertexColorG;
                        b *= vertexColorB;
                    }

                    r *= s_BaseColor[ 0 ];
                    g *= s_BaseColor[ 1 ];
                    b *= s_BaseColor[ 2 ];
                    a *= s_BaseColor[ 3 ];

                    if ( LightType != ELightType::eInvalid )
                    {
                        float vertexNormalX = normalX * w;
                        float vertexNormalY = normalY * w;
                        float vertexNormalZ = normalZ * w;

                        // Re-normalize
                        float length = vertexNormalX * vertexNormalX + vertexNormalY * vertexNormalY + vertexNormalZ * vertexNormalZ;
                        float rcpDenorm = 1.0f / sqrtf( length );
                        vertexNormalX *= rcpDenorm;
                        vertexNormalY *= rcpDenorm;
                        vertexNormalZ *= rcpDenorm;

                        float NdotL = vertexNormalX * s_LightPosition[ 0 ] + vertexNormalY * s_LightPosition[ 1 ] + vertexNormalZ * s_LightPosition[ 2 ];
                        NdotL = std::max( 0.f, NdotL );

                        r = r * s_LightColor[ 0 ] * NdotL;
                        g = g * s_LightColor[ 1 ] * NdotL;
                        b = b * s_LightColor[ 2 ] * NdotL;
                    }

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

            z += z_a;

            if ( NeedRcpw )
            { 
                rcpw += rcpw_a;
            }

            if ( UseTexture )
            { 
                texU += texU_a;
                texV += texV_a;
            }

            if ( UseVertexColor )
            {
                colorR += colorR_a;
                colorG += colorG_a;
                colorB += colorB_a;
            }

            if ( LightType != ELightType::eInvalid )
            {
                normalX += normalX_a;
                normalY += normalY_a;
                normalZ += normalZ_a;
            }
        }

        w0_row += b12;
        w1_row += b20;
        w2_row += b01;

        z_row += z_b;

        if ( NeedRcpw )
        { 
            rcpw_row += rcpw_b;
        }

        if ( UseTexture )
        { 
            texU_row += texU_b;
            texV_row += texV_b;
        }

        if ( UseVertexColor )
        {
            colorR_row += colorR_b;
            colorG_row += colorG_b;
            colorB_row += colorB_b;
        }

        if ( LightType != ELightType::eInvalid )
        {
            normalX_row += normalX_b;
            normalY_row += normalY_b;
            normalZ_row += normalZ_b;
        }
    }
}

template <bool UseTexture, bool UseVertexColor, ELightType LightType, bool IsIndexed>
static void RasterizeTriangles( const uint8_t* pos, const uint8_t* texcoord, const uint8_t* color, const uint8_t* normal, uint32_t stride, const uint32_t* indices, uint32_t trianglesCount )
{
    constexpr bool NeedRcpw = UseTexture || UseVertexColor || LightType != ELightType::eInvalid;

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
        if ( LightType != ELightType::eInvalid )
        {
            v0.SetNormal( normal + offset0 );
            v1.SetNormal( normal + offset1 );
            v2.SetNormal( normal + offset2 );
        }
        RasterizeTriangle<UseTexture, UseVertexColor, LightType>( v0, v1 ,v2 );
    }
}


static uint32_t MakePipelineStateIndex( bool useTexture, bool useVertexColor, ELightType lightType )
{
    return ( useTexture ? 0x1 : 0 ) | ( useVertexColor ? 0x10 : 0 ) | ( lightType << 2 );
}

static uint32_t MakePipelineStateIndex( const SPipelineState& state )
{
    return MakePipelineStateIndex( state.m_UseTexture, state.m_UseVertexColor, state.m_LightType );
}

template <bool UseTexture, bool UseVertexColor, ELightType LightType>
SPipelineFunctionPointers GetPipelineFunctionPointers()
{
    SPipelineFunctionPointers ptrs;
    ptrs.m_TransformVerticesToRasterizerCoordinatesFunction = TransformVerticesToRasterizerCoordinates<UseTexture, UseVertexColor, LightType != ELightType::eInvalid>;
    ptrs.m_RasterizerFunction = RasterizeTriangles<UseTexture, UseVertexColor, LightType, false>;
    ptrs.m_RasterizerFunctionIndexed = RasterizeTriangles<UseTexture, UseVertexColor, LightType, true>;
    return ptrs;
}

void Rasterizer::Initialize()
{
#define SET_PIPELINE_FUNCTION_POINTERS( useTexture, useVertexColor, lightType ) s_PipelineFunctionPtrsTable[ MakePipelineStateIndex( useTexture, useVertexColor, lightType ) ] = GetPipelineFunctionPointers< useTexture, useVertexColor, lightType >();
    SET_PIPELINE_FUNCTION_POINTERS( false, false, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( false, true, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( true, false, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( true, true, ELightType::eInvalid )
    SET_PIPELINE_FUNCTION_POINTERS( false, false, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( false, true, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( true, false, ELightType::eDirectional )
    SET_PIPELINE_FUNCTION_POINTERS( true, true, ELightType::eDirectional )
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

void Rasterizer::SetViewProjectionMatrix( const float* matrix )
{
    memcpy( s_ViewProjectionMatrix, matrix, sizeof( s_ViewProjectionMatrix ) );
}

void Rasterizer::SetNormalMatrix( const float* matrix )
{
    memcpy( s_NormalMatrix, matrix, sizeof( s_NormalMatrix ) );
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

void Rasterizer::SetLightPosition( const float* position )
{
    memcpy( s_LightPosition, position, sizeof( s_LightPosition ) );
}

void Rasterizer::SetLightColor( const float* color )
{
    memcpy( s_LightColor, color, sizeof( s_LightColor ) );
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

static uint32_t ComputeVertexLayout( const SPipelineState& pipelineState, uint32_t* texcoordOffset, uint32_t* colorOffset, uint32_t* normalOffset )
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

    return vertexSize;
}

static void InternalDraw( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount, bool useIndex )
{
    const uint32_t verticesCount = trianglesCount * 3;
    const uint32_t roundedUpVerticesCount = MathHelper::DivideAndRoundUp( verticesCount, (uint32_t)SIMD_WIDTH ) * SIMD_WIDTH;

    // Compute intermediate vertex layout
    uint32_t texcoordOffset, colorOffset, normalOffset;
    const uint32_t vertexSize = ComputeVertexLayout( s_PipelineState, &texcoordOffset, &colorOffset, &normalOffset );
    uint8_t* vertices = (uint8_t*)malloc( vertexSize * roundedUpVerticesCount );
    
    uint8_t* posStream = vertices;

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

        const uint8_t* sourcePosStream = s_StreamSourcePos.m_Data + s_StreamSourcePos.m_Offset + s_StreamSourcePos.m_Stride * baseVertexLocation;
        TransformPos3Stream( m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33,
            sourcePosStream, s_StreamSourcePos.m_Stride, roundedUpVerticesCount, posStream, vertexSize );
    }

    uint8_t* normalStream = vertices + normalOffset;

    if ( s_PipelineState.m_LightType != ELightType::eInvalid )
    {
        // transform normals
        __m128 m00 = _mm_set1_ps( s_NormalMatrix[ 0 ] );
        __m128 m01 = _mm_set1_ps( s_NormalMatrix[ 1 ] );
        __m128 m02 = _mm_set1_ps( s_NormalMatrix[ 2 ] );
        __m128 m10 = _mm_set1_ps( s_NormalMatrix[ 3 ] );
        __m128 m11 = _mm_set1_ps( s_NormalMatrix[ 4 ] );
        __m128 m12 = _mm_set1_ps( s_NormalMatrix[ 5 ] );
        __m128 m20 = _mm_set1_ps( s_NormalMatrix[ 6 ] );
        __m128 m21 = _mm_set1_ps( s_NormalMatrix[ 7 ] );
        __m128 m22 = _mm_set1_ps( s_NormalMatrix[ 8 ] );

        const uint8_t* sourceNormalStream = s_StreamSourceNormal.m_Data + s_StreamSourceNormal.m_Offset + s_StreamSourceNormal.m_Stride * baseVertexLocation;
        AffineTransformVec3Stream( m00, m01, m02, m10, m11, m12, m20, m21, m22, 
            sourceNormalStream, s_StreamSourceNormal.m_Stride, roundedUpVerticesCount, normalStream, vertexSize );
    }

    const uint8_t* sourceTexcoordStream = s_StreamSourceTex.m_Data + s_StreamSourceTex.m_Offset + s_StreamSourceTex.m_Stride * baseVertexLocation;
    const uint8_t* sourceColorStream = s_StreamSourceColor.m_Data + s_StreamSourceColor.m_Offset + s_StreamSourceColor.m_Stride * baseVertexLocation;
    uint8_t* texcoordStream = vertices + texcoordOffset;
    uint8_t* colorStream = vertices + colorOffset;
    s_PipelineFunctionPtrs.m_TransformVerticesToRasterizerCoordinatesFunction( posStream, normalStream,
        sourceTexcoordStream, texcoordStream,
        sourceColorStream, colorStream,
        vertexSize, s_StreamSourceTex.m_Stride, s_StreamSourceColor.m_Stride,
        roundedUpVerticesCount );

    if ( useIndex )
    { 
        s_PipelineFunctionPtrs.m_RasterizerFunctionIndexed( posStream, texcoordStream, colorStream, normalStream, vertexSize, s_StreamSourceIndex + baseIndexLocation, trianglesCount );
    }
    else
    {
        s_PipelineFunctionPtrs.m_RasterizerFunction( posStream, texcoordStream, colorStream, normalStream, vertexSize, nullptr, trianglesCount );
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
