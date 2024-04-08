
#include "PCH.h"
#include "Rasterization.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

Rasterizer::SImage s_RenderTarget;
ComPtr<ID2D1HwndRenderTarget> s_D2dRenderTarget;
ComPtr<ID2D1Bitmap> s_D2dBitmap;

static LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_PAINT:
        if ( s_D2dBitmap.Get() && s_D2dRenderTarget.Get() && s_RenderTarget.m_Bits )
        {
            s_D2dRenderTarget->BeginDraw();
            s_D2dRenderTarget->DrawBitmap( s_D2dBitmap.Get() );
            s_D2dRenderTarget->EndDraw();
            ValidateRect( hWnd, NULL );
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

    HWND hWnd = CreateWindowW( s_WindowClassName, L"HelloTriangle", windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

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

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    const uint32_t width = 800;
    const uint32_t height = 600;

    HWND hWnd = CreateAppWindow( hInstance, width, height );
    if ( !hWnd )
    {
        return 0;
    }

    ComPtr<ID2D1Factory> d2dFactory;

    HRESULT hr = S_OK;
    hr = D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    const D2D1_SIZE_U d2dSize = { width, height };

    hr = d2dFactory->CreateHwndRenderTarget( D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties( hWnd, d2dSize ), s_D2dRenderTarget.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    hr = s_D2dRenderTarget->CreateBitmap( d2dSize, D2D1::BitmapProperties( D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ) ), s_D2dBitmap.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    ShowWindow( hWnd, nCmdShow );
    UpdateWindow( hWnd );

    Rasterizer::SImage depthTarget;
    uint8_t* vertexBuffer = nullptr;
    Rasterizer::SStream posStream, colorStream;
    if ( !CreateRenderData( width, height, &s_RenderTarget, &depthTarget, vertexBuffer, &posStream, &colorStream ) )
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
    Rasterizer::SetRenderTarget( s_RenderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );
    Rasterizer::SPipelineState pipelineState( false, true, Rasterizer::ELightType::eInvalid );
    Rasterizer::SetPipelineState( pipelineState );

    ZeroMemory( s_RenderTarget.m_Bits, s_RenderTarget.m_Width * s_RenderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::Draw( 0, 1 );

    D2D1_RECT_U d2dRect = { 0, 0, s_RenderTarget.m_Width, s_RenderTarget.m_Height };
    hr = s_D2dBitmap->CopyFromMemory( &d2dRect, s_RenderTarget.m_Bits, s_RenderTarget.m_Width * 4 );
    if ( FAILED( hr ) )
    {
        return false;
    }

    s_D2dRenderTarget->BeginDraw();
    s_D2dRenderTarget->DrawBitmap( s_D2dBitmap.Get() );
    if ( FAILED( s_D2dRenderTarget->EndDraw() ) )
    {
        return 0;
    }

    MSG msg;
    while ( GetMessage( &msg, NULL, 0, 0 ) )
    {
        TranslateMessage( &msg );
        DispatchMessage( &msg );
    }

    DestroyRenderData( &s_RenderTarget, &depthTarget, vertexBuffer );

    s_D2dBitmap.Reset();
    s_D2dRenderTarget.Reset();

    DestroyWindow( hWnd );

    return (int)msg.wParam;
}



