#include "PCH.h"
#include "Rasterization.h"
#include "ImageOps.inl"
#include "SIMDMath.inl"
#include "MathHelper.h"

#define SIMD_WIDTH 4

static const int32_t s_SubpixelStep = 16; // 4 bits sub-pixel precision

struct SRasterizerVertex
{
    SRasterizerVertex( int32_t x, int32_t y, float z ) : m_X( x ), m_Y( y ), m_Z( z ) {}
    void SetRcpW( float rcpw ) { m_Rcpw = rcpw; }
    void SetTexcoords( float u, float v ) { m_TexcoordU = u; m_TexcoordV = v; }
    void SetColor( float r, float g, float b ) { m_ColorR = r; m_ColorG = g; m_ColorB = b; }

    int32_t m_X;
    int32_t m_Y;
    float m_Z;
    float m_Rcpw;
    float m_TexcoordU;
    float m_TexcoordV;
    float m_ColorR;
    float m_ColorG;
    float m_ColorB;
};

struct SVertexStreams4
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
        m_Z = (float*)_aligned_malloc( streamSize, 16 );
        m_W = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
        _aligned_free( m_Z );
        _aligned_free( m_W );
    }

    SVertexStreams4& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        m_Z += num;
        m_W += num;
        return *this;
    }

    SVertexStreams4 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num, m_Z + num, m_W + num };
    }

    float* m_X;
    float* m_Y;
    float* m_Z;
    float* m_W;
};

struct SVertexStreams3
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
        m_Z = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
        _aligned_free( m_Z );
    }

    SVertexStreams3& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        m_Z += num;
        return *this;
    }

    SVertexStreams3 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num, m_Z + num };
    }

    float* m_X;
    float* m_Y;
    float* m_Z;
};

struct SVertexStreams2
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
    }

    SVertexStreams2& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        return *this;
    }

    SVertexStreams2 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num };
    }

    float* m_X;
    float* m_Y;
};

typedef void (*TransformVerticesToRasterizerCoordinatesFunctionPtr)( SVertexStreams4, SVertexStreams2, SVertexStreams2, SVertexStreams3, SVertexStreams3, uint32_t );
typedef void (*RasterizeFunctionPtr)( SVertexStreams4, SVertexStreams2, SVertexStreams3, uint32_t );

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
static const float* s_StreamSourceColorR = nullptr;
static const float* s_StreamSourceColorG = nullptr;
static const float* s_StreamSourceColorB = nullptr;

static SImage s_RenderTarget = { 0 };
static SImage s_DepthTarget = { 0 };
static SImage s_Texture = { 0 };

static void __vectorcall TransformVec3Stream( __m128 m00, __m128 m01, __m128 m02, __m128 m03,
    __m128 m10, __m128 m11, __m128 m12, __m128 m13,
    __m128 m20, __m128 m21, __m128 m22, __m128 m23,
    __m128 m30, __m128 m31, __m128 m32, __m128 m33,
    SVertexStreams3 inPos, uint32_t count,
    SVertexStreams4 outPos )
{
    assert( count % SIMD_WIDTH == 0 );

    uint32_t batchCount = count / SIMD_WIDTH;
    for ( uint32_t i = 0; i < batchCount; ++i )
    {
        __m128 x = _mm_load_ps( inPos.m_X );
        __m128 y = _mm_load_ps( inPos.m_Y );
        __m128 z = _mm_load_ps( inPos.m_Z );
        __m128 dotX, dotY, dotZ, dotW;
        SIMDMath::Vec3DotVec4( x, y, z, m00, m10, m20, m30, dotX );
        SIMDMath::Vec3DotVec4( x, y, z, m01, m11, m21, m31, dotY );
        SIMDMath::Vec3DotVec4( x, y, z, m02, m12, m22, m32, dotZ );
        SIMDMath::Vec3DotVec4( x, y, z, m03, m13, m23, m33, dotW );
        _mm_store_ps( outPos.m_X, dotX );
        _mm_store_ps( outPos.m_Y, dotY );
        _mm_store_ps( outPos.m_Z, dotZ );
        _mm_store_ps( outPos.m_W, dotW );

        inPos += SIMD_WIDTH;
        outPos += SIMD_WIDTH;
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

template <bool UseTexture, bool UseVertexColor>
static void TransformVerticesToRasterizerCoordinates( SVertexStreams4 pos, SVertexStreams2 inTex, SVertexStreams2 outTex, SVertexStreams3 inColor, SVertexStreams3 outColor, uint32_t count )
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
        __m128 x = _mm_load_ps( pos.m_X );
        __m128 y = _mm_load_ps( pos.m_Y );
        __m128 z = _mm_load_ps( pos.m_Z );
        __m128 w = _mm_load_ps( pos.m_W );
        __m128 one = { 1.f, 1.f, 1.f, 1.f };
        __m128 rcpw = _mm_div_ps( one, w );
        __m128 half = { .5f, .5f, .5f, .5f };
        __m128 texU, texV;
        if ( UseTexture )
        {
            texU = _mm_load_ps( inTex.m_X );
            texV = _mm_load_ps( inTex.m_Y );
        }
        __m128 colorR, colorG, colorB;
        if ( UseVertexColor )
        {
            colorR = _mm_load_ps( inColor.m_X );
            colorG = _mm_load_ps( inColor.m_Y );
            colorB = _mm_load_ps( inColor.m_Z );
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

        _mm_store_si128( (__m128i*)pos.m_X, xi );
        _mm_store_si128( (__m128i*)pos.m_Y, yi );
        _mm_store_ps( pos.m_Z, z );

        constexpr bool NeedRcpw = UseTexture || UseVertexColor;
        if ( NeedRcpw )
        { 
            _mm_store_ps( pos.m_W, rcpw );
        }

        if ( UseTexture )
        { 
            _mm_store_ps( outTex.m_X, texU );
            _mm_store_ps( outTex.m_Y, texV );
        }

        if ( UseVertexColor )
        {
            _mm_store_ps( outColor.m_X, colorR );
            _mm_store_ps( outColor.m_Y, colorG );
            _mm_store_ps( outColor.m_Z, colorB );
        }

        pos += SIMD_WIDTH;

        if ( UseTexture )
        { 
            inTex += SIMD_WIDTH;
            outTex += SIMD_WIDTH;
        }

        if ( UseVertexColor )
        {
            inColor += SIMD_WIDTH;
            outColor += SIMD_WIDTH;
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

template <bool UseTexture, bool UseVertexColor>
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
    
    constexpr bool NeedRcpw = UseTexture || UseVertexColor;
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
    }
}

template <bool UseTexture, bool UseVertexColor>
static void RasterizeTriangles( SVertexStreams4 pos, SVertexStreams2 texcoord, SVertexStreams3 color, uint32_t trianglesCount )
{
    constexpr bool NeedRcpw = UseTexture || UseVertexColor;

    const int32_t* posXi = (int32_t*)pos.m_X;
    const int32_t* posYi = (int32_t*)pos.m_Y;
    for ( uint32_t i = 0; i < trianglesCount; ++i )
    {
        uint32_t vertexIndexBase = i * 3;
        SRasterizerVertex v0( posXi[ vertexIndexBase ], posYi[ vertexIndexBase ], pos.m_Z[ vertexIndexBase ] );
        SRasterizerVertex v1( posXi[ vertexIndexBase + 1 ], posYi[ vertexIndexBase + 1 ], pos.m_Z[ vertexIndexBase + 1 ] );
        SRasterizerVertex v2( posXi[ vertexIndexBase + 2 ], posYi[ vertexIndexBase + 2 ], pos.m_Z[ vertexIndexBase + 2 ] );
        if ( NeedRcpw )
        {
            v0.SetRcpW( pos.m_W[ vertexIndexBase ] );
            v1.SetRcpW( pos.m_W[ vertexIndexBase + 1 ] );
            v2.SetRcpW( pos.m_W[ vertexIndexBase + 2 ] );
        }
        if ( UseTexture )
        {   
            v0.SetTexcoords( texcoord.m_X[ vertexIndexBase ], texcoord.m_Y[ vertexIndexBase ] );
            v1.SetTexcoords( texcoord.m_X[ vertexIndexBase + 1 ], texcoord.m_Y[ vertexIndexBase + 1 ] );
            v2.SetTexcoords( texcoord.m_X[ vertexIndexBase + 2 ], texcoord.m_Y[ vertexIndexBase + 2 ] );
        }
        if ( UseVertexColor )
        {
            v0.SetColor( color.m_X[ vertexIndexBase ], color.m_Y[ vertexIndexBase ], color.m_Z[ vertexIndexBase ] );
            v1.SetColor( color.m_X[ vertexIndexBase + 1 ], color.m_Y[ vertexIndexBase + 1 ], color.m_Z[ vertexIndexBase + 1 ] );
            v2.SetColor( color.m_X[ vertexIndexBase + 2 ], color.m_Y[ vertexIndexBase + 2 ], color.m_Z[ vertexIndexBase + 2 ] );
        }
        RasterizeTriangle<UseTexture, UseVertexColor>( v0, v1 ,v2 );
    }
}

#define INSTANTIATE_PIPELINESTATE_FUNCTIONS( useTexture, useVertexColor ) \
    template void TransformVerticesToRasterizerCoordinates<useTexture, useVertexColor>( SVertexStreams4, SVertexStreams2, SVertexStreams2, SVertexStreams3, SVertexStreams3, uint32_t ); \
    template void RasterizeTriangles<useTexture, useVertexColor>( SVertexStreams4, SVertexStreams2, SVertexStreams3, uint32_t );

    INSTANTIATE_PIPELINESTATE_FUNCTIONS( false, false )
    INSTANTIATE_PIPELINESTATE_FUNCTIONS( true, false )
    INSTANTIATE_PIPELINESTATE_FUNCTIONS( false, true )
    INSTANTIATE_PIPELINESTATE_FUNCTIONS( true, true )
#undef INSTANTIATE_PIPELINESTATE_FUNCTIONS

#define IMPLEMENT_PIPELINESTATES( useTexture, useVertexColor ) \
    const SPipelineStates TGetPipelineStates<useTexture, useVertexColor>::s_States = { (void*)&TransformVerticesToRasterizerCoordinates<useTexture, useVertexColor>, (void*)&RasterizeTriangles<useTexture, useVertexColor> };

    IMPLEMENT_PIPELINESTATES( false, false )
    IMPLEMENT_PIPELINESTATES( true, false )
    IMPLEMENT_PIPELINESTATES( false, true )
    IMPLEMENT_PIPELINESTATES( true, true )
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

void Rasterizer::SetColorStreams( const float* r, const float* g, const float* b )
{
    s_StreamSourceColorR = r;
    s_StreamSourceColorG = g;
    s_StreamSourceColorB = b;
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
    SVertexStreams4 posStream;
    posStream.Allocate( streamSize );
    
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

        const SVertexStreams3 sourcePosStream = { const_cast<float*>( s_StreamSourcePosX ), const_cast<float*>( s_StreamSourcePosY ), const_cast<float*>( s_StreamSourcePosZ ) };
        TransformVec3Stream( m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33,
            sourcePosStream + baseVertexIndex,
            roundedUpVerticesCount,
            posStream );
    }

    SVertexStreams2 texStream;
    SVertexStreams3 colorStream;
    texStream.Allocate( streamSize );
    colorStream.Allocate( streamSize );

    const SVertexStreams2 sourceTexStream = { const_cast<float*>( s_StreamSourceTexU ), const_cast<float*>( s_StreamSourceTexV ) };
    const SVertexStreams3 sourceColorStream = { const_cast<float*>( s_StreamSourceColorR ), const_cast<float*>( s_StreamSourceColorG ), const_cast<float*>( s_StreamSourceColorB ) };
    s_TransformVerticesToRasterizerCoordinatesFunction( posStream,
        sourceTexStream + baseVertexIndex, texStream,
        sourceColorStream + baseVertexIndex, colorStream,
        roundedUpVerticesCount );

    s_RasterizerFunction( posStream, texStream, colorStream, trianglesCount );

    posStream.Free();
    texStream.Free();
    colorStream.Free();
}
