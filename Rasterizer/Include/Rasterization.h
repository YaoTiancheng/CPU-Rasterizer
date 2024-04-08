#pragma once

namespace Rasterizer
{
    struct SStream
    {
        SStream() 
            : m_Offset( 0 )
            , m_Stride( 0 )
            , m_Size( 0 )
            , m_Data( nullptr )
        {}

        SStream( uint32_t offset, uint32_t stride, uint32_t size, uint8_t* data )
            : m_Offset( offset )
            , m_Stride( stride )
            , m_Size( size )
            , m_Data( data )
        {}

        uint32_t m_Offset;
        uint32_t m_Stride;
        uint32_t m_Size;
        uint8_t* m_Data;
    };

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

    enum ELightType
    {
        eInvalid,
        eDirectional,
    };

    struct SPipelineState
    {
        SPipelineState()
            : m_LightType( ELightType::eInvalid )
            , m_UseTexture( false )
            , m_UseVertexColor( false )
        {}
        
        SPipelineState( bool useTexture, bool useVertexColor, ELightType lightType )
            : m_LightType( lightType )
            , m_UseTexture( useTexture )
            , m_UseVertexColor( useVertexColor )
        {
        }

        ELightType m_LightType;
        bool m_UseTexture;
        bool m_UseVertexColor;
    };

    void Initialize();

    void SetPositionStream( const SStream& stream );

    void SetNormalStream( const SStream& stream );

    void SetTexcoordStream( const SStream& stream );

    void SetColorStream( const SStream& stream );

    void SetIndexStream( const uint32_t* indices );

    void SetViewProjectionMatrix( const float* matrix );

    void SetNormalMatrix( const float* matrix );

    void SetViewport( const SViewport& viewport );

    void SetRenderTarget( const SImage& image );

    void SetDepthTarget( const SImage& image );

    void SetBaseColor( const float* color );

    void SetLightPosition( const float* position );

    void SetLightColor( const float* color );

    void SetTexture( const SImage& image );

    void SetPipelineState( const SPipelineState& state );

    void Draw( uint32_t baseVertexIndex, uint32_t trianglesCount );

    void DrawIndexed( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount );
}