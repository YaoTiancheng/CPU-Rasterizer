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

    struct SPipelineState
    {
        SPipelineState()
            : m_UseTexture( false )
            , m_UseVertexColor( false )
        {}
        
        SPipelineState( bool useTexture, bool useVertexColor )
            : m_UseTexture( useTexture )
            , m_UseVertexColor( useVertexColor )
        {
        }

        bool m_UseTexture;
        bool m_UseVertexColor;
    };

    void Initialize();

    void SetPositionStreams( const float* x, const float* y, const float* z );

    void SetTexcoordStreams( const float* texU, const float* texV );

    void SetColorStreams( const float* r, const float* g, const float* b );

    void SetIndexStream( const uint32_t* indices );

    void SetViewProjectionMatrix( const float* matrix );

    void SetViewport( const SViewport& viewport );

    void SetRenderTarget( const SImage& image );

    void SetDepthTarget( const SImage& image );

    void SetBaseColor( const float* color );

    void SetTexture( const SImage& image );

    void SetPipelineState( const SPipelineState& state );

    void Draw( uint32_t baseVertexIndex, uint32_t trianglesCount );

    void DrawIndexed( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount );
}