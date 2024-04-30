#pragma once

namespace Rasterizer
{
    struct alignas( 16 ) SMatrix
    {
        SMatrix() = default;
    
        SMatrix( float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13, 
            float m20, float m21, float m22, float m23, 
            float m30, float m31, float m32, float m33 )
            : m_00( m00 ), m_01( m01 ), m_02( m02 ), m_03( m03 )
            , m_10( m10 ), m_11( m11 ), m_12( m12 ), m_13( m13 )
            , m_20( m20 ), m_21( m21 ), m_22( m22 ), m_23( m23 )
            , m_30( m30 ), m_31( m31 ), m_32( m32 ), m_33( m33 )
        {
        }

        union
        {
            struct
            {
                float m_00, m_01, m_02, m_03;
                float m_10, m_11, m_12, m_13;
                float m_20, m_21, m_22, m_23;
                float m_30, m_31, m_32, m_33;
            };
            float m_Data[ 16 ];
        };
    };

    struct SVector4
    {
        SVector4() = default;

        SVector4( float x, float y, float z, float w )
            : m_X( x ), m_Y( y ), m_Z( z ), m_W( w )
        {}

        explicit SVector4( float* data )
        {
            memcpy( m_Data, data, sizeof( m_Data ) );
        }

        union
        {
            struct
            {
                float m_X, m_Y, m_Z, m_W;
            };
            float m_Data[ 4 ];
        };
    };

    struct SVector3
    {
        SVector3() = default;

        SVector3( float x, float y, float z )
            : m_X( x ), m_Y( y ), m_Z( z )
        {}

        explicit SVector3( float* data )
        {
            memcpy( m_Data, data, sizeof( m_Data ) );
        }

        union
        {
            struct
            {
                float m_X, m_Y, m_Z;
            };
            float m_Data[ 3 ];
        };
    };

    struct SLight
    {
        SVector3 m_Position;
        SVector3 m_Diffuse;
        SVector3 m_Specular;
        SVector3 m_Ambient;
    };

    struct SMaterial
    {
        SVector4 m_Diffuse;
        SVector3 m_Specular;
        float m_Power;
    };

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

    enum class ECullMode : uint8_t
    {
        eNone, eCullCW, eCullCCW
    };

    enum class EIndexType : uint8_t
    {
        e16bit, e32bit
    };

    enum class ELightingModel : uint32_t
    {
        eUnlit,
        eLambert,
        eBlinnPhong,
        eCount
    };

    enum class ELightType : uint32_t
    {
        eDirectional,
        ePoint,
        eCount
    };

    struct SPipelineState
    {
        SPipelineState()
            : m_LightingModel( ELightingModel::eUnlit )
            , m_LightType( ELightType::eDirectional )
            , m_UseTexture( false )
            , m_UseVertexColor( false )
            , m_EnableAlphaTest( false )
            , m_EnableAlphaBlend( false )
        {}
        
        SPipelineState( bool useTexture, bool useVertexColor, bool enableAlphaTest = false, bool enableAlphaBlend = false
            , ELightingModel lightingModel = ELightingModel::eUnlit, ELightType lightType = ELightType::eDirectional )
            : m_LightingModel( lightingModel )
            , m_LightType( lightType )
            , m_UseTexture( useTexture )
            , m_UseVertexColor( useVertexColor )
            , m_EnableAlphaTest( enableAlphaTest )
            , m_EnableAlphaBlend( enableAlphaBlend )
        {
        }

        ELightingModel m_LightingModel;
        ELightType m_LightType;
        bool m_UseTexture;
        bool m_UseVertexColor;
        bool m_EnableAlphaTest;
        bool m_EnableAlphaBlend;
    };

    void Initialize();

    void SetPositionStream( const SStream& stream );

    void SetNormalStream( const SStream& stream );

    void SetTexcoordStream( const SStream& stream );

    void SetColorStream( const SStream& stream );

    void SetIndexStream( const SStream& stream );

    void SetWorldViewTransform( const SMatrix& matrix );

    void SetProjectionTransform( const SMatrix& matrix );

    void SetViewport( const SViewport& viewport );

    void SetRenderTarget( const SImage& image );

    void SetDepthTarget( const SImage& image );

    void SetMaterialDiffuse( SVector4 color );

    void SetMaterial( const SMaterial& material );

    void SetLight( const SLight& light );

    void SetTexture( const SImage& image );

    void SetAlphaRef( uint8_t value );

    void SetCullMode( ECullMode mode );

    void SetIndexType( EIndexType type );

    void SetPipelineState( const SPipelineState& state );

    void Draw( uint32_t baseVertexIndex, uint32_t trianglesCount );

    void DrawIndexed( uint32_t baseVertexLocation, uint32_t baseIndexLocation, uint32_t trianglesCount );
}