#pragma once

struct SPipelineState;

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

    struct SPipelineStates
    {
        const void* s_RasterizerVertexState;
        const void* s_RasterizerState;
    };

    template <bool UseTexture, bool UseVertexColor>
    struct TGetPipelineStates
    {
        static const SPipelineStates s_States;
    };

    #define RASTERIZER_SPECIALIZE_GETPIPELINESTATES( useTexture, useVertexColor ) \
        template <> \
        struct TGetPipelineStates<useTexture, useVertexColor> \
        { \
            static const SPipelineStates s_States; \
        };

    RASTERIZER_SPECIALIZE_GETPIPELINESTATES( false, false )
    RASTERIZER_SPECIALIZE_GETPIPELINESTATES( true, false )
    RASTERIZER_SPECIALIZE_GETPIPELINESTATES( false, true )
    RASTERIZER_SPECIALIZE_GETPIPELINESTATES( true, true )

    #undef RASTERIZER_SPECIALIZE_GETPIPELINESTATES

    void SetPositionStreams( const float* x, const float* y, const float* z );

    void SetTexcoordStreams( const float* texU, const float* texV );

    void SetColorStreams( const float* r, const float* g, const float* b );

    void SetViewProjectionMatrix( const float* matrix );

    void SetViewport( const SViewport& viewport );

    void SetRenderTarget( const SImage& image );

    void SetDepthTarget( const SImage& image );

    void SetBaseColor( const float* color );

    void SetTexture( const SImage& image );

    void SetPipelineStates( const SPipelineStates& states );

    void Draw( uint32_t baseVertexIndex, uint32_t trianglesCount );
}