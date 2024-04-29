
#include "ModelViewerPCH.h"
#include "Rasterizer.h"
#include "Scene.h"
#include "SceneLoader.h"
#include "SceneRendering.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static HWND s_hWnd = NULL;
static CScene s_Scene;
static std::vector<SMeshDrawCommand> s_CachedMeshDrawCommands;
static XMFLOAT3 s_CameraLookAt = { 0.f, 0.f, 0.f };
static float s_CameraDistance = 0.f;

static void ComputeCameraLookAtAndDistance( const CScene&, XMFLOAT3*, float* );

static LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_KEYUP:
        if ( wParam == 'F' )
        {
            OPENFILENAMEA ofn;
            char filename[ MAX_PATH ];
            ZeroMemory( &ofn, sizeof( ofn ) );
            ofn.lStructSize = sizeof( ofn );
            ofn.hwndOwner = s_hWnd;
            ofn.lpstrFile = filename;
            ofn.lpstrFile[0] = '\0';
            ofn.nMaxFile = sizeof( filename );
            ofn.lpstrFilter = "glTF 2.0 (*.glb)\0*.glb\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if ( GetOpenFileNameA( &ofn ) == TRUE )
            {
                s_Scene.FreeAll();
                s_CachedMeshDrawCommands.clear();
                if ( LoadSceneFronGLTFFile( filename, &s_Scene ) )
                {
                    s_Scene.FlipCoordinateHandness();
                    ComputeCameraLookAtAndDistance( s_Scene, &s_CameraLookAt, &s_CameraDistance );
                    GenerateMeshDrawCommands( s_Scene, &s_CachedMeshDrawCommands );
                }
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

    HWND hWnd = CreateWindowW( s_WindowClassName, L"Model Viewer", windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

static void ComputeCameraLookAtAndDistance( const CScene& scene, XMFLOAT3* cameraLookAt, float* cameraDistance )
{
    BoundingSphere sphere = scene.CalculateBoundingSphere();
    *cameraLookAt = sphere.Center;
    *cameraDistance = sphere.Radius * 2.2f;
}

static void UpdateCamera( float& cameraPitch, float& cameraYall, float& cameraDistance )
{
    const float cameraOrbitingSensitivity = XMConvertToRadians( 1.f );
    if ( GetAsyncKeyState( VK_UP ) )
    {
        cameraPitch += cameraOrbitingSensitivity;
    }
    else if ( GetAsyncKeyState( VK_DOWN ) )
    {
        cameraPitch -= cameraOrbitingSensitivity;
    }
    if ( GetAsyncKeyState( VK_LEFT ) )
    {
        cameraYall += cameraOrbitingSensitivity;
    }
    else if ( GetAsyncKeyState( VK_RIGHT ) )
    {
        cameraYall -= cameraOrbitingSensitivity;
    }

    const float cameraMoveSensitivity = 0.04f;
    if ( GetAsyncKeyState( VK_SHIFT ) )
    {
        cameraDistance = std::max( 0.f, cameraDistance - cameraMoveSensitivity );
    }
    else if ( GetAsyncKeyState( VK_CONTROL ) )
    {
        cameraDistance += cameraMoveSensitivity;
    }
}

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, const std::vector<SMeshDrawCommand>& meshDrawCommands,
    float aspectRatio, float cameraPitch, float cameraYall, const XMFLOAT3& cameraLookAt, float cameraDistance )
{
    ZeroMemory( renderTarget.m_Bits, renderTarget.m_Width * renderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SMatrix matrix;

    XMMATRIX viewMatrix = XMMatrixTranslation( 0.f, 0.f, -cameraDistance );
    viewMatrix = XMMatrixMultiply( viewMatrix, XMMatrixRotationRollPitchYaw( cameraPitch, cameraYall, 0.f ) );
    viewMatrix = XMMatrixMultiply( viewMatrix, XMMatrixTranslation( cameraLookAt.x, cameraLookAt.y, cameraLookAt.z ) );
    viewMatrix = XMMatrixInverse( nullptr, viewMatrix );

    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( XMConvertToRadians( 40.0f ), aspectRatio, 2.f, 1000.f );
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    for ( const SMeshDrawCommand& command : meshDrawCommands )
    {
        XMMATRIX worldMatrix = XMLoadFloat4x3( &command.m_WorldMatrix );
        XMMATRIX worldViewMatrix = XMMatrixMultiply( worldMatrix, viewMatrix );
        XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, worldViewMatrix );
        Rasterizer::SetWorldViewTransform( matrix );

        Rasterizer::SetMaterial( command.m_Material );
        Rasterizer::SetTexture( command.m_DiffuseTexture );

        Rasterizer::SetPositionStream( command.m_PositionStream );
        Rasterizer::SetNormalStream( command.m_NormalStream );
        Rasterizer::SetColorStream( command.m_ColorStream );
        Rasterizer::SetTexcoordStream( command.m_TexcoordsStream );

        Rasterizer::SetCullMode( command.m_TwoSided ? Rasterizer::ECullMode::eNone : Rasterizer::ECullMode::eCullCW );
        Rasterizer::SetAlphaRef( command.m_AlphaRef );

        Rasterizer::SPipelineState pipelineState;
        pipelineState.m_UseTexture = command.m_DiffuseTexture.m_Bits != nullptr;
        pipelineState.m_UseVertexColor = command.m_ColorStream.m_Data != nullptr;
        pipelineState.m_EnableAlphaTest = command.m_AlphaTest;
        Rasterizer::SetPipelineState( pipelineState );

        if ( command.m_IndexStream.m_Data )
        {
            Rasterizer::SetIndexStream( command.m_IndexStream );
            Rasterizer::SetIndexType( command.m_IndexType );
            Rasterizer::DrawIndexed( 0, 0, command.m_PrimitiveCount );
        }
        else
        {
            Rasterizer::Draw( 0, command.m_PrimitiveCount );
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

    s_hWnd = CreateAppWindow( hInstance, width, height );
    if ( !s_hWnd )
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

    hr = d2dFactory->CreateHwndRenderTarget( D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties( s_hWnd, d2dSize ), d2dRenderTarget.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    hr = d2dRenderTarget->CreateBitmap( d2dSize, D2D1::BitmapProperties( D2D1::PixelFormat( DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE ) ), d2dBitmap.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return 0;
    }

    ShowWindow( s_hWnd, nCmdShow );
    UpdateWindow( s_hWnd );

    Rasterizer::SImage renderTarget, depthTarget;
    renderTarget.m_Width = width;
    renderTarget.m_Height = height;
    renderTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );
    depthTarget.m_Width = width;
    depthTarget.m_Height = height;
    depthTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    Rasterizer::SViewport viewport;
    viewport.m_Left = 0;
    viewport.m_Top = 0;
    viewport.m_Width = width;
    viewport.m_Height = height;

    Rasterizer::Initialize();
    Rasterizer::SetRenderTarget( renderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );

    float cameraPitch = 0.f, cameraYall = 0.f;

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

        UpdateCamera( cameraPitch, cameraYall, s_CameraDistance );
        RenderImage( d2dBitmap.Get(), renderTarget, depthTarget, s_CachedMeshDrawCommands, aspectRatio, cameraPitch, cameraYall, s_CameraLookAt, s_CameraDistance );

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

    free( renderTarget.m_Bits );
    free( depthTarget.m_Bits );
    s_Scene.FreeAll();

    DestroyWindow( s_hWnd );

    return (int)msg.wParam;
}



