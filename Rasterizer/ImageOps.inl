#pragma once

static inline void R8G8B8A8Unorm_To_Float( uint32_t rgba, float* r, float* g, float* b, float* a )
{
    const float denorm = 1.f / 255.f;
    *a = ( rgba >> 24 & 0xFF ) * denorm;
    *r = ( rgba >> 16 & 0xFF ) * denorm;
    *g = ( rgba >> 8 & 0xFF ) * denorm;
    *b = ( rgba & 0xFF ) * denorm;
}

/*  Bilinear filtering
    |----|----|
    | v0 | v1 |
    |----|----|
    | v2 | v3 |
    |----|----|
    alpha is the horizontal weight, increases from left to right
    beta is the vertical weight, increases from top to bottom
 */
static inline float BilinearInterpolation( float alpha, float beta, float v0, float v1, float v2, float v3 )
{
    float vx0 = ( -v0 ) * alpha + ( v1 * alpha + v0 );
    float vx1 = ( -v2 ) * alpha + ( v3 * alpha + v2 );
    return ( -vx0 ) * beta + ( vx1 * beta + vx0 );
}

static inline void SampleTexture_PointClamp( const Rasterizer::SImage& texture, float texU, float texV, float* r, float* g, float* b, float* a )
{
    const int32_t maxX = texture.m_Width - 1;
    const int32_t maxY = texture.m_Height - 1;
    const int32_t texelPosX = std::max( std::min( int32_t( texU * texture.m_Width ), maxX ), 0 );
    const int32_t texelPosY = std::max( std::min( int32_t( texV * texture.m_Height ), maxY ), 0 );
    const uint32_t texel = ( (uint32_t*)texture.m_Bits )[ texelPosY * texture.m_Width + texelPosX ];
    R8G8B8A8Unorm_To_Float( texel, r, g, b, a );
}

static inline void SampleTexture_LinearClamp( const Rasterizer::SImage& texture, float texU, float texV, float* r, float* g, float* b, float* a )
{
    const float texelPosXf = texU * texture.m_Width - 0.5f;
    const float texelPosYf = texV * texture.m_Height - 0.5f;
    int32_t texelPosMinX = (int32_t)std::floorf( texelPosXf );
    int32_t texelPosMinY = (int32_t)std::floorf( texelPosYf );
    int32_t texelPosMaxX = texelPosMinX + 1;
    int32_t texelPosMaxY = texelPosMinY + 1;
    const float texelFractionX = texelPosXf - texelPosMinX;
    const float texelFractionY = texelPosYf - texelPosMinY;

    // Clamp to texture border
    const int32_t maxX = texture.m_Width - 1;
    const int32_t maxY = texture.m_Height - 1;
    texelPosMinX = std::max( 0, texelPosMinX );
    texelPosMinY = std::max( 0, texelPosMinY );
    texelPosMaxX = std::min( maxX, texelPosMaxX );
    texelPosMaxY = std::min( maxY, texelPosMaxY );

    uint32_t* texelBits = (uint32_t*)texture.m_Bits;
    uint32_t rgba0 = texelBits[ texelPosMinY * texture.m_Width + texelPosMinX ];
    uint32_t rgba1 = texelBits[ texelPosMinY * texture.m_Width + texelPosMaxX ];
    uint32_t rgba2 = texelBits[ texelPosMaxY * texture.m_Width + texelPosMinX ];
    uint32_t rgba3 = texelBits[ texelPosMaxY * texture.m_Width + texelPosMaxX ];

    float r0, g0, b0, a0;
    float r1, g1, b1, a1;
    float r2, g2, b2, a2;
    float r3, g3, b3, a3;
    R8G8B8A8Unorm_To_Float( rgba0, &r0, &g0, &b0, &a0 );
    R8G8B8A8Unorm_To_Float( rgba1, &r1, &g1, &b1, &a1 );
    R8G8B8A8Unorm_To_Float( rgba2, &r2, &g2, &b2, &a2 );
    R8G8B8A8Unorm_To_Float( rgba3, &r3, &g3, &b3, &a3 );

    *r = BilinearInterpolation( texelFractionX, texelFractionY, r0, r1, r2, r3 );
    *g = BilinearInterpolation( texelFractionX, texelFractionY, g0, g1, g2, g3 );
    *b = BilinearInterpolation( texelFractionX, texelFractionY, b0, b1, b2, b3 );
    *a = BilinearInterpolation( texelFractionX, texelFractionY, a0, a1, a2, a3 );
}