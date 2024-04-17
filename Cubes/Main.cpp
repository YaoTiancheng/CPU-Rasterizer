
#include "PCH.h"
#include "Rasterizer.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static const uint32_t s_TexturesCount = 2;
static uint32_t s_CurrentTextureIndex = 0;
static bool s_AlphaTestEnabled = false;

static LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_KEYDOWN:
        if ( ( lParam & 0x40000000 ) == 0 )
        { 
            if ( wParam == 'T' )
            {
                s_CurrentTextureIndex = ( s_CurrentTextureIndex + 1 ) % s_TexturesCount;
            }
            else if ( wParam == 'A' )
            {
                s_AlphaTestEnabled = s_AlphaTestEnabled != true;
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;
    default:
        return DefWindowProc( hWnd, message, wParam, lParam );
    }
    return 0;
}

static HWND CreateAppWindow( HINSTANCE hInstance, uint32_t width, uint32_t height )
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof( WNDCLASSEX );

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor( nullptr, IDC_ARROW );
    wcex.hbrBackground = (HBRUSH)( COLOR_WINDOW + 1 );
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = s_WindowClassName;
    wcex.hIconSm = NULL;

    if ( !RegisterClassExW( &wcex ) )
    {
        return NULL;
    }

    const DWORD windowStyle = WS_OVERLAPPEDWINDOW & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX;

    RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect( &rect, windowStyle, FALSE );

    HWND hWnd = CreateWindowW( s_WindowClassName, L"Cubes", windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

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

static bool CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, Rasterizer::SImage* textures,
    uint8_t*& vertexBuffer, Rasterizer::SStream* posStream, Rasterizer::SStream* texcoordStream, uint32_t*& indices, uint32_t* triangleCount )
{
    struct SVertex
    {
        float m_X, m_Y, m_Z;
        float m_TexU, m_TexV;
    };

    const uint32_t vertexBufferSize = 24 * sizeof( SVertex ); 
    vertexBuffer = (uint8_t*)malloc( vertexBufferSize );

    SVertex* vertices = (SVertex*)vertexBuffer;
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

    posStream->m_Data = vertexBuffer;
    posStream->m_Offset = 0;
    posStream->m_Stride = sizeof( SVertex );
    posStream->m_Size = vertexBufferSize;

    texcoordStream->m_Data = vertexBuffer;
    texcoordStream->m_Offset = offsetof( SVertex, m_TexU );
    texcoordStream->m_Stride = sizeof( SVertex );
    texcoordStream->m_Size = vertexBufferSize;

    indices = (uint32_t*)malloc( 36 * 4 );
    indices[ 0 ] = 0; indices[ 1 ] = 1; indices[ 2 ] = 2;
    indices[ 3 ] = 1; indices[ 4 ] = 0; indices[ 5 ] = 3;
    indices[ 6 ] = 4; indices[ 7 ] = 5; indices[ 8 ] = 6;
    indices[ 9 ] = 5; indices[ 10 ] = 4; indices[ 11 ] = 7;
    indices[ 12 ] = 8; indices[ 13 ] = 9; indices[ 14 ] = 10;
    indices[ 15 ] = 9; indices[ 16 ] = 8; indices[ 17 ] = 11;
    indices[ 18 ] = 12; indices[ 19 ] = 13; indices[ 20 ] = 14;
    indices[ 21 ] = 13; indices[ 22 ] = 12; indices[ 23 ] = 15;
    indices[ 24 ] = 16; indices[ 25 ] = 17; indices[ 26 ] = 18;
    indices[ 27 ] = 17; indices[ 28 ] = 16; indices[ 29 ] = 19;
    indices[ 30 ] = 20; indices[ 31 ] = 21; indices[ 32 ] = 22;
    indices[ 33 ] = 21; indices[ 34 ] = 20; indices[ 35 ] = 23;

    *triangleCount = 12;

    renderTarget->m_Width = width;
    renderTarget->m_Height = height;
    renderTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    depthTarget->m_Width = width;
    depthTarget->m_Height = height;
    depthTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    // Load the texture from file
    ComPtr<IWICImagingFactory> WICImagingFactory;
    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)WICImagingFactory.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    const wchar_t* s_TextureFilenames[ s_TexturesCount ] = { L"Resources/BRICK_1A.PNG", L"Resources/foliage_21.PNG" };
    for ( uint32_t i = 0; i < s_TexturesCount; ++i )
    {
        if ( !CreateTextureFromFile( WICImagingFactory.Get(), s_TextureFilenames[ i ], &textures[ i ] ) )
        {
            return false;
        }
    }

    return true;
}

static void DestroyRenderData( Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, Rasterizer::SImage* textures, uint8_t* vertexBuffer, uint32_t* indices )
{
    free( vertexBuffer );
    free( indices );
    free( renderTarget->m_Bits );
    free( depthTarget->m_Bits );
    for ( uint32_t i = 0; i < s_TexturesCount; ++i )
    { 
        free( textures[ i ].m_Bits );
    }
}

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& texture, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, uint32_t triangleCount, float aspectRatio, float& roll, float& pitch, float& yall )
{
    yall += XMConvertToRadians( 0.5f );
    roll += XMConvertToRadians( 0.3f );

    ZeroMemory( renderTarget.m_Bits, renderTarget.m_Width * renderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SVector4 diffuseColors[] = { { 1.f, 1.f, 1.0f, 1.0f }, { 0.8f, 0.4f, 0.0f, 1.0f }, { 0.8f, 0.2f, 0.5f, 1.0f }, { 0.3f, 0.5f, 0.28f, 1.0f } };

    Rasterizer::SMatrix matrix;
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw( pitch, yall, roll );
    XMMATRIX viewMatrix = XMMatrixSet(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 10.f, 1.f );
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( 1.0f, aspectRatio, 2.f, 1000.f );

    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    Rasterizer::SetTexture( texture );

    Rasterizer::SPipelineState pipelineState( true, false );
    pipelineState.m_EnableAlphaTest = s_AlphaTestEnabled;
    Rasterizer::SetAlphaRef( 0x80 );
    Rasterizer::SetPipelineState( pipelineState );

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

                Rasterizer::DrawIndexed( 0, 0, triangleCount );
            }
        }
    }
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    const uint32_t width = 800;
    const uint32_t height = 600;
    const float aspectRatio = (float)width / height;

    HWND hWnd = CreateAppWindow( hInstance, width, height );
    if ( !hWnd )
    {
        return 0;
    }

    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<ID2D1HwndRenderTarget> d2dRenderTarget;
    ComPtr<ID2D1Bitmap> d2dBitmap;

    HRESULT hr = S_OK;
    hr = D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    const D2D1_SIZE_U d2dSize = { width, height };

    hr = d2dFactory->CreateHwndRenderTarget( D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties( hWnd, d2dSize ), d2dRenderTarget.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    hr = d2dRenderTarget->CreateBitmap( d2dSize, D2D1::BitmapProperties( D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ) ), d2dBitmap.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    ShowWindow( hWnd, nCmdShow );
    UpdateWindow( hWnd );

    Rasterizer::SImage renderTarget;
    Rasterizer::SImage depthTarget;
    Rasterizer::SImage textures[ s_TexturesCount ] = {};
    uint8_t* vertexBuffer;
    Rasterizer::SStream posStream, texcoordStream;
    uint32_t* indices;
    uint32_t triangleCount;
    if ( !CreateRenderData( width, height, &renderTarget, &depthTarget, textures, vertexBuffer, &posStream, &texcoordStream, indices, &triangleCount ) )
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
    Rasterizer::SetTexcoordStream( texcoordStream );
    Rasterizer::SetIndexStream( indices );
    Rasterizer::SetRenderTarget( renderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );

    float roll = 0.f, pitch = 0.f, yall = 0.f;

    MSG msg;
    bool looping = true;
    while ( looping )
    { 
        while ( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
        {
            if ( msg.message == WM_QUIT )
            {
                looping = false;
                break;
            }
            
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        }

        RenderImage( d2dBitmap.Get(), textures[ s_CurrentTextureIndex ], renderTarget, depthTarget, triangleCount, aspectRatio, roll, pitch, yall );

        D2D1_RECT_U d2dRect = { 0, 0, width, height };
        HRESULT hr = d2dBitmap->CopyFromMemory( &d2dRect, renderTarget.m_Bits, width * 4 );
        if ( FAILED( hr ) )
        {
            looping = false;
        }

        d2dRenderTarget->BeginDraw();
        d2dRenderTarget->DrawBitmap( d2dBitmap.Get() );
        hr = d2dRenderTarget->EndDraw();
        if ( FAILED( hr ) )
        {
            looping = false;
        }
    }

    DestroyRenderData( &renderTarget, &depthTarget, textures, vertexBuffer, indices );

    DestroyWindow( hWnd );

    return (int)msg.wParam;
}



