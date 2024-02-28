#pragma once

namespace Rasterizer
{
    struct SViewport
    {
        uint32_t m_Left;
        uint32_t m_Top;
        uint32_t m_Width;
        uint32_t m_Height;
    };

    struct SImage
    {
        uint8_t* m_Bits;
        uint32_t m_Width;
        uint32_t m_Height;
    };

    void SetPositionStreams( const float* x, const float* y, const float* z );

    void SetTexcoordStreams( const float* texU, const float* texV );

    void SetViewProjectionMatrix( const float* matrix );

    void SetViewport( const SViewport& viewport );

    void SetRenderTarget( const SImage& image );

    void SetDepthTarget( const SImage& image );

    void SetBaseColor( const float* color );

    void Draw( uint32_t baseVertexIndex, uint32_t trianglesCount );
}