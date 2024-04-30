
#include "PCH.h"
#include "Rasterizer.h"
#include "DemoApp.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static bool CreateTextureFromFile( IWICImagingFactory* factory, const wchar_t* filename, Rasterizer::SImage* texture )
{
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> convertedFrame;

    if ( FAILED( factory->CreateDecoderFromFilename( filename, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( decoder->GetFrame( 0, frame.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( frame->GetSize( &texture->m_Width, &texture->m_Height ) ) )
    {
        return false;
    }
    
    if ( FAILED( factory->CreateFormatConverter( convertedFrame.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( convertedFrame->Initialize( frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom ) ) )
    {
        return false;
    }

    const uint32_t byteSize = texture->m_Width * texture->m_Height * 4;
    texture->m_Bits = (uint8_t*)malloc( byteSize );
    return SUCCEEDED( convertedFrame->CopyPixels( nullptr, texture->m_Width * 4, byteSize, (BYTE*)texture->m_Bits ) );
}

class CDemoApp_Cubes : public CDemoApp
{
    using CDemoApp::CDemoApp;

    virtual bool OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) override;
    virtual bool OnInit() override;
    virtual void OnDestroy() override;
    virtual void OnUpdate() override;

    bool CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SStream* posStream, Rasterizer::SStream* texcoordStream, Rasterizer::SStream* indexStream );
    void DestroyRenderData();

    static const uint32_t s_TexturesCount = 2;

    Rasterizer::SImage m_RenderTarget;
    Rasterizer::SImage m_DepthTarget;
    Rasterizer::SImage m_Textures[ s_TexturesCount ] = {};
    uint8_t* m_VertexBuffer = nullptr;
    uint16_t* m_Indices = nullptr;
    uint32_t m_TriangleCount = 0;

    uint32_t m_CurrentTextureIndex = 0;
    bool m_AlphaTestEnabled = false;
    Rasterizer::ECullMode m_CullMode = Rasterizer::ECullMode::eCullCW;

    float m_Roll = 0.f, m_Pitch = 0.f, m_Yall = 0.f;
};

bool CDemoApp_Cubes::OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_KEYDOWN:
        if ( ( lParam & 0x40000000 ) == 0 )
        { 
            if ( wParam == 'T' )
            {
                m_CurrentTextureIndex = ( m_CurrentTextureIndex + 1 ) % s_TexturesCount;
            }
            else if ( wParam == 'A' )
            {
                m_AlphaTestEnabled = m_AlphaTestEnabled != true;
            }
            else if ( wParam == 'C' )
            {
                m_CullMode = (Rasterizer::ECullMode)( ( (uint32_t)m_CullMode + 1 ) % 3 );
            }
        }
        break;
    }
    return false;
}

bool CDemoApp_Cubes::CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SStream* posStream, Rasterizer::SStream* texcoordStream, Rasterizer::SStream* indexStream )
{
    struct SVertex
    {
        float m_X, m_Y, m_Z;
        float m_TexU, m_TexV;
    };

    const uint32_t vertexBufferSize = 24 * sizeof( SVertex ); 
    m_VertexBuffer = (uint8_t*)malloc( vertexBufferSize );

    SVertex* vertices = (SVertex*)m_VertexBuffer;
    // Front 
    vertices[ 0 ] = { 1.f, -1.f, -1.f, 1.f, 1.f };
    vertices[ 1 ] = { -1.f, 1.f, -1.f, 0.f, 0.f };
    vertices[ 2 ] = { -1.f, -1.f, -1.f, 0.f, 1.f };
    vertices[ 3 ] = { 1.f, 1.f, -1.f, 1.f, 0.f };

    // Left 
    vertices[ 4 ] = { -1.f, -1.f, -1.f, 1.f, 1.f };
    vertices[ 5 ] = { -1.f, 1.f, 1.f, 0.f, 0.f };
    vertices[ 6 ] = { -1.f, -1.f, 1.f, 0.f, 1.f };
    vertices[ 7 ] = { -1.f, 1.f, -1.f, 1.f, 0.f };

    // Right 
    vertices[ 8 ] = { 1.f, -1.f, 1.f, 1.f, 1.f };
    vertices[ 9 ] = { 1.f, 1.f, -1.f, 0.f, 0.f };
    vertices[ 10 ] = { 1.f, -1.f, -1.f, 0.f, 1.f };
    vertices[ 11 ] = { 1.f, 1.f, 1.f, 1.f, 0.f };

    // Back 
    vertices[ 12 ] = { -1.f, -1.f, 1.f, 1.f, 1.f };
    vertices[ 13 ] = { 1.f, 1.f, 1.f, 0.f, 0.f };
    vertices[ 14 ] = { 1.f, -1.f, 1.f, 0.f, 1.f };
    vertices[ 15 ] = { -1.f, 1.f, 1.f, 1.f, 0.f };

    // Top 
    vertices[ 16 ] = { 1.f, 1.f, -1.f, 1.f, 1.f };
    vertices[ 17 ] = { -1.f, 1.f, 1.f, 0.f, 0.f };
    vertices[ 18 ] = { -1.f, 1.f, -1.f, 0.f, 1.f };
    vertices[ 19 ] = { 1.f, 1.f, 1.f, 1.f, 0.f };

    // Bottom 
    vertices[ 20 ] = { -1.f, -1.f, -1.f, 1.f, 1.f };
    vertices[ 21 ] = { 1.f, -1.f, 1.f, 0.f, 0.f };
    vertices[ 22 ] = { 1.f, -1.f, -1.f, 0.f, 1.f };
    vertices[ 23 ] = { -1.f, -1.f, 1.f, 1.f, 0.f };

    posStream->m_Data = m_VertexBuffer;
    posStream->m_Offset = 0;
    posStream->m_Stride = sizeof( SVertex );
    posStream->m_Size = vertexBufferSize;

    texcoordStream->m_Data = m_VertexBuffer;
    texcoordStream->m_Offset = offsetof( SVertex, m_TexU );
    texcoordStream->m_Stride = sizeof( SVertex );
    texcoordStream->m_Size = vertexBufferSize;

    m_Indices = (uint16_t*)malloc( 36 * 2 );
    m_Indices[ 0 ] = 0; m_Indices[ 1 ] = 1; m_Indices[ 2 ] = 2;
    m_Indices[ 3 ] = 1; m_Indices[ 4 ] = 0; m_Indices[ 5 ] = 3;
    m_Indices[ 6 ] = 4; m_Indices[ 7 ] = 5; m_Indices[ 8 ] = 6;
    m_Indices[ 9 ] = 5; m_Indices[ 10 ] = 4; m_Indices[ 11 ] = 7;
    m_Indices[ 12 ] = 8; m_Indices[ 13 ] = 9; m_Indices[ 14 ] = 10;
    m_Indices[ 15 ] = 9; m_Indices[ 16 ] = 8; m_Indices[ 17 ] = 11;
    m_Indices[ 18 ] = 12; m_Indices[ 19 ] = 13; m_Indices[ 20 ] = 14;
    m_Indices[ 21 ] = 13; m_Indices[ 22 ] = 12; m_Indices[ 23 ] = 15;
    m_Indices[ 24 ] = 16; m_Indices[ 25 ] = 17; m_Indices[ 26 ] = 18;
    m_Indices[ 27 ] = 17; m_Indices[ 28 ] = 16; m_Indices[ 29 ] = 19;
    m_Indices[ 30 ] = 20; m_Indices[ 31 ] = 21; m_Indices[ 32 ] = 22;
    m_Indices[ 33 ] = 21; m_Indices[ 34 ] = 20; m_Indices[ 35 ] = 23;

    indexStream->m_Data = (uint8_t*)m_Indices;
    indexStream->m_Offset = 0;
    indexStream->m_Size = 36 * 2;
    indexStream->m_Stride = 2;

    m_TriangleCount = 12;

    m_RenderTarget.m_Width = width;
    m_RenderTarget.m_Height = height;
    m_RenderTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    m_DepthTarget.m_Width = width;
    m_DepthTarget.m_Height = height;
    m_DepthTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    ComPtr<IWICImagingFactory> WICImagingFactory;
    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)WICImagingFactory.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    const wchar_t* s_TextureFilenames[ s_TexturesCount ] = { L"Resources/BRICK_1A.PNG", L"Resources/foliage_21.PNG" };
    for ( uint32_t i = 0; i < s_TexturesCount; ++i )
    {
        if ( !CreateTextureFromFile( WICImagingFactory.Get(), s_TextureFilenames[ i ], &m_Textures[ i ] ) )
        {
            return false;
        }
    }

    return true;
}

void CDemoApp_Cubes::DestroyRenderData()
{
    free( m_VertexBuffer );
    free( m_Indices );
    free( m_RenderTarget.m_Bits );
    free( m_DepthTarget.m_Bits );
    for ( uint32_t i = 0; i < s_TexturesCount; ++i )
    { 
        free( m_Textures[ i ].m_Bits );
    }
}

bool CDemoApp_Cubes::OnInit()
{
    Rasterizer::SStream posStream, texcoordStream, indexStream;

    uint32_t width, height;
    GetSwapChainSize( &width, &height );

    if ( !CreateRenderData( width, height, &posStream, &texcoordStream, &indexStream ) )
    {
        return false;
    }

    Rasterizer::SViewport viewport;
    viewport.m_Left = 0;
    viewport.m_Top = 0;
    viewport.m_Width = width;
    viewport.m_Height = height;

    Rasterizer::Initialize();
    Rasterizer::SetPositionStream( posStream );
    Rasterizer::SetTexcoordStream( texcoordStream );
    Rasterizer::SetIndexStream( indexStream );
    Rasterizer::SetRenderTarget( m_RenderTarget );
    Rasterizer::SetDepthTarget( m_DepthTarget );
    Rasterizer::SetViewport( viewport );

    return true;
}

void CDemoApp_Cubes::OnDestroy()
{
    DestroyRenderData();
}

void CDemoApp_Cubes::OnUpdate()
{
    m_Yall += XMConvertToRadians( 0.5f );
    m_Roll += XMConvertToRadians( 0.3f );

    ZeroMemory( m_RenderTarget.m_Bits, m_RenderTarget.m_Width * m_RenderTarget.m_Height * 4 );
    float* depthBit = (float*)m_DepthTarget.m_Bits;
    for ( uint32_t i = 0; i < m_DepthTarget.m_Width * m_DepthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SVector4 diffuseColors[] = { { 1.f, 1.f, 1.0f, 1.0f }, { 0.8f, 0.4f, 0.0f, 1.0f }, { 0.8f, 0.2f, 0.5f, 1.0f }, { 0.3f, 0.5f, 0.28f, 1.0f } };

    Rasterizer::SMatrix matrix;
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw( m_Pitch, m_Yall, m_Roll );
    XMMATRIX viewMatrix = XMMatrixSet(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 10.f, 1.f );
    const float aspectRatio = (float)m_RenderTarget.m_Width / m_RenderTarget.m_Height;
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( 1.0f, aspectRatio, 2.f, 1000.f );

    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    Rasterizer::SetTexture( m_Textures[ m_CurrentTextureIndex ] );

    Rasterizer::SPipelineState pipelineState( true, false );
    pipelineState.m_EnableAlphaTest = m_AlphaTestEnabled;
    Rasterizer::SetPipelineState( pipelineState );

    Rasterizer::SetAlphaRef( 0x80 );
    Rasterizer::SetCullMode( m_CullMode );

    XMINT3 cubeCount( 3, 3, 3 );
    XMFLOAT3 cubeSpacing( 3.f, 3.f, 3.f );
    XMFLOAT3 cubeCenterMin( -( cubeCount.x - 1 ) * cubeSpacing.x * 0.5f, -( cubeCount.y - 1 ) * cubeSpacing.y * 0.5f, -( cubeCount.z - 1 ) * cubeSpacing.z * 0.5f );
    for ( int32_t z = 0; z < cubeCount.z; ++z )
    {
        for ( int32_t y = 0; y < cubeCount.y; ++y )
        {
            for ( int32_t x = 0; x < cubeCount.x; ++x )
            {
                const int32_t index = z * cubeCount.x * cubeCount.y + y * cubeCount.x + x;
                Rasterizer::SetMaterialDiffuse( diffuseColors[ index % 4 ] );

                XMFLOAT3 center( cubeCenterMin.x + cubeSpacing.x * x, cubeCenterMin.y + cubeSpacing.y * y, cubeCenterMin.z + cubeSpacing.z * z );
                XMMATRIX translationMatrix = XMMatrixTranslation( center.x, center.y, center.z );
                XMMATRIX worldMatrix = XMMatrixMultiply( translationMatrix, rotationMatrix );
                XMMATRIX worldViewMatrix = XMMatrixMultiply( worldMatrix, viewMatrix );
                XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, worldViewMatrix );
                Rasterizer::SetWorldViewTransform( matrix );

                Rasterizer::DrawIndexed( 0, 0, m_TriangleCount );
            }
        }
    }

    CopyToSwapChain( m_RenderTarget );
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    CDemoApp_Cubes demoApp( L"Cubes", hInstance, 800, 600 );
    int returnCode = 0;
    if ( demoApp.Initialize() )
    {
        returnCode = demoApp.Execute();
    }
    demoApp.Destroy();
    return returnCode;
}



