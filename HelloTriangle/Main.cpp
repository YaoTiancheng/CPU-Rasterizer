
#include "PCH.h"
#include "Rasterizer.h"
#include "DemoApp.h"

class CDemoApp_HelloTriangle : public CDemoApp
{
    using CDemoApp::CDemoApp;

    virtual bool OnInit() override;
};

static bool CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget,
    uint8_t*& vertexBuffer, Rasterizer::SStream* posStream, Rasterizer::SStream* colorStream )
{
    struct SVertex
    {
        float m_X, m_Y, m_Z;
        float m_R, m_G, m_B;
    };

    const uint32_t vertexBufferSize = 4 * sizeof( SVertex );
    vertexBuffer = (uint8_t*)malloc( vertexBufferSize );

    SVertex* vertices = (SVertex*)vertexBuffer;
    vertices[ 0 ] = { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f };
    vertices[ 1 ] = { 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f };
    vertices[ 2 ] = { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f };

    posStream->m_Data = vertexBuffer;
    posStream->m_Offset = 0;
    posStream->m_Stride = sizeof( SVertex );
    posStream->m_Size = vertexBufferSize;

    colorStream->m_Data = vertexBuffer;
    colorStream->m_Offset = offsetof( SVertex, m_R );
    colorStream->m_Stride = sizeof( SVertex );
    colorStream->m_Size = vertexBufferSize;

    renderTarget->m_Width = width;
    renderTarget->m_Height = height;
    renderTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    depthTarget->m_Width = width;
    depthTarget->m_Height = height;
    depthTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    return true;
}

static void DestroyRenderData( Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, uint8_t* vertexBuffer )
{
    free( vertexBuffer );
    free( renderTarget->m_Bits );
    free( depthTarget->m_Bits );
}

bool CDemoApp_HelloTriangle::OnInit()
{
    uint32_t width, height;
    GetSwapChainSize( &width, &height );

    Rasterizer::SImage renderTarget;
    Rasterizer::SImage depthTarget;
    uint8_t* vertexBuffer = nullptr;
    Rasterizer::SStream posStream, colorStream;
    if ( !CreateRenderData( width, height, &renderTarget, &depthTarget, vertexBuffer, &posStream, &colorStream ) )
    {
        return 0;
    }

    Rasterizer::SViewport viewport;
    viewport.m_Left = 0;
    viewport.m_Top = 0;
    viewport.m_Width = width;
    viewport.m_Height = height;

    Rasterizer::Initialize();
    Rasterizer::SetPositionStream( posStream );
    Rasterizer::SetColorStream( colorStream );
    Rasterizer::SetRenderTarget( renderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );
    Rasterizer::SPipelineState pipelineState( false, true );
    Rasterizer::SetPipelineState( pipelineState );

    ZeroMemory( renderTarget.m_Bits, renderTarget.m_Width * renderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::Draw( 0, 1 );

    CopyToSwapChain( renderTarget );

    DestroyRenderData( &renderTarget, &depthTarget, vertexBuffer );

    return true;
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    CDemoApp_HelloTriangle demoApp( L"Hello Triangle", hInstance, 800, 600, false );
    int returnCode = 0;
    if ( demoApp.Initialize() )
    {
        returnCode = demoApp.Execute();
    }
    demoApp.Destroy();
    return returnCode;
}