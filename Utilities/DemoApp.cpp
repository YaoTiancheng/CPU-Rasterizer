#include "UtilitiesPCH.h"
#include "DemoApp.h"

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_DESTROY:
        PostQuitMessage( 0 );
        break;
    default:
        {
            CDemoApp* app = (CDemoApp*)GetWindowLongPtr( hWnd, 0 );
            if ( app && app->OnWndProc( hWnd, message, wParam, lParam ) )
            {
                return 0;
            }
            else
            {
                return DefWindowProc( hWnd, message, wParam, lParam );
            }
        }
    }
    return 0;
}

static HWND CreateAppWindow( const wchar_t* title, HINSTANCE hInstance, uint32_t width, uint32_t height )
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof( WNDCLASSEX );

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = sizeof( CDemoApp* );
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

    HWND hWnd = CreateWindowW( s_WindowClassName, title, windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

static bool CreateD2DContext( HWND hWnd, uint32_t width, uint32_t height, ID2D1Factory** factory, ID2D1HwndRenderTarget** renderTarget, ID2D1Bitmap** bitmap )
{
    HRESULT hr = D2D1CreateFactory( D2D1_FACTORY_TYPE_SINGLE_THREADED, factory );
    if ( FAILED( hr ) )
    {
        return false;
    }

    const D2D1_SIZE_U D2DSize = { width, height };
    hr = ( *factory )->CreateHwndRenderTarget( D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties( hWnd, D2DSize ), renderTarget );
    if ( FAILED( hr ) )
    {
        return false;
    }

    hr = ( *renderTarget )->CreateBitmap( D2DSize, D2D1::BitmapProperties( D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ) ), bitmap );
    if ( FAILED( hr ) )
    {
        return false;
    }

    return true;
}

CDemoApp::CDemoApp( const wchar_t* name, HINSTANCE hInstance, uint32_t width, uint32_t height )
    : m_hWnd( NULL )
{
    m_hWnd = CreateAppWindow( name, hInstance, width, height );
    SetWindowLongPtr( m_hWnd, 0, (LONG_PTR)this );
}

CDemoApp::~CDemoApp()
{
}

bool CDemoApp::Initialize()
{
    if ( FAILED( CoInitialize( NULL ) ) )
    {
        return false;
    }

    RECT rc;
    GetClientRect( m_hWnd, &rc );

    const uint32_t width = rc.right - rc.left;
    const uint32_t height = rc.bottom - rc.top;

    if ( !CreateD2DContext( m_hWnd, width, height, m_D2DFactory.GetAddressOf(), m_D2DRenderTarget.GetAddressOf(), m_D2DBitmap.GetAddressOf() ) )
    {
        return false;
    }

    if ( !OnInit() )
    {
        return false;
    }

    ShowWindow( m_hWnd, SW_SHOW );
    UpdateWindow( m_hWnd );

    return true;
}

void CDemoApp::Destroy()
{
    OnDestroy();

    m_D2DBitmap.Reset();
    m_D2DRenderTarget.Reset();
    m_D2DFactory.Reset();

    CoUninitialize();

    DestroyWindow( m_hWnd );
    m_hWnd = NULL;
}

int CDemoApp::Execute()
{
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

        OnUpdate();

        m_D2DRenderTarget->BeginDraw();
        m_D2DRenderTarget->DrawBitmap( m_D2DBitmap.Get() );
        m_D2DRenderTarget->EndDraw();
    }

    return (int)msg.wParam;
}

void CDemoApp::GetSwapChainSize( uint32_t* width, uint32_t* height )
{
    const D2D1_SIZE_U pixelSize = m_D2DRenderTarget->GetPixelSize();
    *width = pixelSize.width;
    *height = pixelSize.height;
}

bool CDemoApp::CopyToSwapChain( Rasterizer::SImage image )
{
    D2D1_RECT_U D2DRect = { 0, 0, image.m_Width, image.m_Height };
    return m_D2DBitmap->CopyFromMemory( &D2DRect, image.m_Bits, image.m_Width * 4 );
}
