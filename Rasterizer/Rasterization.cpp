#include "PCH.h"
#include "Rasterization.h"
#include "ImageOps.inl"
#include "SIMDMath.inl"
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

typedef void (*TransformVerticesToRasterizerCoordinatesFunctionPtr)( float* inX, float* inY, float* inZ, float* inW, const float* inTexU, const float* inTexV, float* outTexU, float* outTexV, uint32_t count );
typedef void (*RasterizeFunctionPtr)( const float* inX, const float* inY, const float* inZ, const float* inW, const float* inTexU, const float* inTexV, uint32_t triangleCount );

using namespace Rasterizer;

static TransformVerticesToRasterizerCoordinatesFunctionPtr s_TransformVerticesToRasterizerCoordinatesFunction = nullptr;
static RasterizeFunctionPtr s_RasterizerFunction = nullptr;

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
static SImage s_Texture = { 0 };

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

template <bool UseTexture>
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
        __m128 one = { 1.f, 1.f, 1.f, 1.f };
        __m128 rcpw = _mm_div_ps( one, w );
        __m128 half = { .5f, .5f, .5f, .5f };
        __m128 texU, texV;
        if (UseTexture)
        {
            texU = _mm_load_ps(inTexU);
            texV = _mm_load_ps(inTexV);
        }

        x = _mm_fmadd_ps( x, rcpw, one ); // x = x / w - (-1)
        x = _mm_fmadd_ps( x, vHalfRasterizerWidth, half ); // Add 0.5 for rounding
        __m128i xi = _mm_cvttps_epi32( x );
        xi = _mm_add_epi32( xi, vOffsetX );

        y = _mm_fmadd_ps( y, rcpw, one ); // y = y / w - (-1)
        y = _mm_fmadd_ps( y, vHalfRasterizerHeight, half ); // Add 0.5 for rounding
        __m128i yi = _mm_cvttps_epi32( y );
        yi = _mm_add_epi32( yi, vOffsetY );

        z = _mm_mul_ps( z, rcpw );

        if ( UseTexture )
        { 
            texU = _mm_mul_ps( texU, rcpw );
            texV = _mm_mul_ps( texV, rcpw );
        }

        _mm_store_si128( (__m128i*)inX, xi );
        _mm_store_si128( (__m128i*)inY, yi );
        _mm_store_ps( inZ, z );

        constexpr bool NeedRcpw = UseTexture;
        if ( NeedRcpw )
        { 
            _mm_store_ps( inW, rcpw );
        }

        if ( UseTexture )
        { 
            _mm_store_ps( outTexU, texU );
            _mm_store_ps( outTexV, texV );
        }

        inX += SIMD_WIDTH;
        inY += SIMD_WIDTH;
        inZ += SIMD_WIDTH;
        inW += SIMD_WIDTH;

        if ( UseTexture )
        { 
            inTexU += SIMD_WIDTH;
            inTexV += SIMD_WIDTH;
            outTexU += SIMD_WIDTH;
            outTexV += SIMD_WIDTH;
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

template <bool UseTexture>
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
    
    constexpr bool NeedRcpw = UseTexture;
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
                    
                    float r, g, b, a;
                    if ( UseTexture )
                    { 
                        float texcoordU = texU * w;
                        float texcoordV = texV * w;
                        SampleTexture_PointClamp( s_Texture, texcoordU, texcoordV, &r, &g, &b, &a );
                        r *= s_BaseColor[ 0 ];
                        g *= s_BaseColor[ 1 ];
                        b *= s_BaseColor[ 2 ];
                        a *= s_BaseColor[ 3 ];
                    }
                    else
                    {
                        r = s_BaseColor[ 0 ];
                        g = s_BaseColor[ 1 ];
                        b = s_BaseColor[ 2 ];
                        a = s_BaseColor[ 3 ];
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
    }
}

template <bool UseTexture>
static void RasterizeTriangles( const float* inX, const float* inY, const float* inZ, const float* inW, const float* inTexU, const float* inTexV, uint32_t trianglesCount )
{
    constexpr bool NeedRcpw = UseTexture;

    const int32_t* inXi = (int32_t*)inX;
    const int32_t* inYi = (int32_t*)inY;
    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        uint32_t vertexIndexBase = i * 3;
        SRasterizerVertex v0 = { inXi[ vertexIndexBase ], inYi[ vertexIndexBase ], inZ[ vertexIndexBase ], NeedRcpw ? inW[ vertexIndexBase ] : 0.f, UseTexture ? inTexU[ vertexIndexBase ] : 0.f, UseTexture ? inTexV[ vertexIndexBase ] : 0.f };
        SRasterizerVertex v1 = { inXi[ vertexIndexBase + 1 ], inYi[ vertexIndexBase + 1 ], inZ[ vertexIndexBase + 1 ], NeedRcpw ? inW[ vertexIndexBase + 1 ] : 0.f, UseTexture ? inTexU[ vertexIndexBase + 1 ] : 0.f, UseTexture ? inTexV[ vertexIndexBase + 1 ] : 0.f };
        SRasterizerVertex v2 = { inXi[ vertexIndexBase + 2 ], inYi[ vertexIndexBase + 2 ], inZ[ vertexIndexBase + 2 ], NeedRcpw ? inW[ vertexIndexBase + 2 ] : 0.f, UseTexture ? inTexU[ vertexIndexBase + 2 ] : 0.f, UseTexture ? inTexV[ vertexIndexBase + 2 ] : 0.f };
        RasterizeTriangle<UseTexture>( v0, v1 ,v2 );
    }
}

#define INSTANTIATE_PIPELINESTATE_FUNCTIONS( useTexture ) \
    template void TransformVerticesToRasterizerCoordinates<useTexture>( float* inX, float* inY, float* inZ, float* inW, const float* inTexU, const float* inTexV, float* outTexU, float* outTexV, uint32_t count ); \
    template void RasterizeTriangles<useTexture>( const float* inX, const float* inY, const float* inZ, const float* inW, const float* inTexU, const float* inTexV, uint32_t triangleCount );

    INSTANTIATE_PIPELINESTATE_FUNCTIONS( false )
    INSTANTIATE_PIPELINESTATE_FUNCTIONS( true )
#undef INSTANTIATE_PIPELINESTATE_FUNCTIONS

#define IMPLEMENT_PIPELINESTATES( useTexture ) \
    const SPipelineStates TGetPipelineStates<useTexture>::s_States = { (void*)&TransformVerticesToRasterizerCoordinates<useTexture>, (void*)&RasterizeTriangles<useTexture> };

    IMPLEMENT_PIPELINESTATES( false )
    IMPLEMENT_PIPELINESTATES( true )
#undef IMPLEMENT_PIPELINESTATES


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

void Rasterizer::SetTexture( const SImage& image )
{
    s_Texture = image;
}

void Rasterizer::SetPipelineStates( const SPipelineStates& states )
{
    s_TransformVerticesToRasterizerCoordinatesFunction = (TransformVerticesToRasterizerCoordinatesFunctionPtr)states.s_RasterizerVertexState;
    s_RasterizerFunction = (RasterizeFunctionPtr)states.s_RasterizerState;
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

    s_TransformVerticesToRasterizerCoordinatesFunction( streamPosX, streamPosY, streamPosZ, streamPosW, s_StreamSourceTexU + baseVertexIndex, s_StreamSourceTexV + baseVertexIndex, streamTexU, streamTexV, roundedUpVerticesCount );

    s_RasterizerFunction( streamPosX, streamPosY, streamPosZ, streamPosW, streamTexU, streamTexV, trianglesCount );

    _aligned_free( streamPosX );
    _aligned_free( streamPosY );
    _aligned_free( streamPosZ );
    _aligned_free( streamPosW );
    _aligned_free( streamTexU );
    _aligned_free( streamTexV );
}
