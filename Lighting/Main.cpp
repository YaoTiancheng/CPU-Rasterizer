
#include "PCH.h"
#include "Rasterizer.h"
#include "Mesh.h"
#include "MeshLoader.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static LRESULT CALLBACK WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
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

    HWND hWnd = CreateWindowW( s_WindowClassName, L"Lighting", windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

static bool CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget,
    CMesh* mesh, Rasterizer::SStream* posStream, Rasterizer::SStream* normalStream )
{
    if ( !LoadMeshFromObjFile( "Resources/Teapot.obj", mesh, CMesh::EVertexFormat::ePosition | CMesh::EVertexFormat::eNormal ) )
    {
        return false;
    }

    // The teapot.obj is in right hand coordinate, convert it to left hand coordinate
    mesh->FlipCoordinateHandness();

    posStream->m_Data = mesh->GetVertices();
    posStream->m_Offset = 0;
    posStream->m_Stride = mesh->GetVertexSize();
    posStream->m_Size = mesh->GetVertexSize() * mesh->GetVerticesCount();

    normalStream->m_Data = mesh->GetVertices();
    normalStream->m_Offset = mesh->GetNormalOffset();
    normalStream->m_Stride = mesh->GetVertexSize();
    normalStream->m_Size = mesh->GetVertexSize() * mesh->GetVerticesCount();

    renderTarget->m_Width = width;
    renderTarget->m_Height = height;
    renderTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    depthTarget->m_Width = width;
    depthTarget->m_Height = height;
    depthTarget->m_Bits = (uint8_t*)malloc( width * height * 4 );

    return true;
}

static void DestroyRenderData( Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, CMesh* mesh )
{
    mesh->FreeVertices();
    mesh->FreeIndices();
    free( renderTarget->m_Bits );
    free( depthTarget->m_Bits );
}

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, uint32_t triangleCount, float aspectRatio, float& roll, float& pitch, float& yall )
{
    yall += XMConvertToRadians( 0.5f );
    pitch += XMConvertToRadians( 0.1f );

    ZeroMemory( renderTarget.m_Bits, renderTarget.m_Width * renderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SMatrix matrix;

    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw( pitch, yall, roll );
    XMMATRIX worldMatrix = XMMatrixMultiply( XMMatrixTranslation( 0.f, -1.2f, 0.f ), rotationMatrix );
    XMMATRIX viewMatrix = XMMatrixSet(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 7.5f, 1.f );
    XMMATRIX worldViewMatrix = XMMatrixMultiply( worldMatrix, viewMatrix );
    
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, worldViewMatrix );
    Rasterizer::SetWorldViewTransform( matrix );

    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( XMConvertToRadians( 50.0f ), aspectRatio, 2.f, 1000.f );
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    Rasterizer::SLight light;
    light.m_Diffuse = Rasterizer::SVector3( 1.f, 1.f, 1.f );
    light.m_Specular = Rasterizer::SVector3( 1.f, 1.f, 1.f );
    light.m_Ambient = Rasterizer::SVector3( 0.05f, 0.06f, 0.05f );
    // Transform the light from world space to view space
    XMVECTOR lightDirection = XMVector3TransformNormal( XMVectorSet( 0.f, 1.f, 0.f, 0.f ), viewMatrix );
    XMStoreFloat3( (XMFLOAT3*)light.m_Position.m_Data, lightDirection );
    Rasterizer::SetLight( light );

    Rasterizer::SMaterial material;
    material.m_Diffuse = Rasterizer::SVector4( 0.5f, 0.6f, 0.5f, 1.f );
    material.m_Specular = Rasterizer::SVector3( 0.5f, 0.5f, 0.5f );
    material.m_Power = 40.f;
    Rasterizer::SetMaterial( material );

    Rasterizer::DrawIndexed( 0, 0, triangleCount );         
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
    CMesh mesh;
    Rasterizer::SStream posStream, normalStream;
    if ( !CreateRenderData( width, height, &renderTarget, &depthTarget, &mesh, &posStream, &normalStream ) )
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
    Rasterizer::SetNormalStream( normalStream );
    Rasterizer::SetIndexStream( mesh.GetIndices() );
    Rasterizer::SetRenderTarget( renderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );
    Rasterizer::SPipelineState pipelineState( false, false, Rasterizer::ELightType::eDirectional );
    Rasterizer::SetPipelineState( pipelineState );

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

        RenderImage( d2dBitmap.Get(), renderTarget, depthTarget, mesh.GetIndicesCount() / 3, aspectRatio, roll, pitch, yall );

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

    DestroyRenderData( &renderTarget, &depthTarget, &mesh );

    DestroyWindow( hWnd );

    return (int)msg.wParam;
}



