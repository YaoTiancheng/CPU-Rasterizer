
#include "ModelViewerPCH.h"
#include "Rasterizer.h"
#include "Mesh.h"
#include "Image.h"
#include "MeshLoader.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static HWND s_hWnd = NULL;
static CMesh s_Mesh;
static std::vector<CImage> s_Textures;
static XMFLOAT3 s_MeshOffset( 0.f, 0.f, 0.f );
static float s_CameraDistance = 0.f;

static void BindMesh( const CMesh& );
static void FreeMeshAndTextures();
static void ComputeMeshOffsetAndCameraDistance( const CMesh&, XMFLOAT3*, float* );

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
            ofn.lpstrFilter = "Wavefront OBJ Files (*.obj)\0*.OBJ\0";
            ofn.nFilterIndex = 1;
            ofn.lpstrFileTitle = NULL;
            ofn.nMaxFileTitle = 0;
            ofn.lpstrInitialDir = NULL;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

            if ( GetOpenFileNameA( &ofn ) == TRUE )
            {
                FreeMeshAndTextures();
                if ( LoadMeshFromObjFile( filename, &s_Mesh, &s_Textures ) )
                {
                    s_Mesh.FlipCoordinateHandness();
                    BindMesh( s_Mesh );
                    ComputeMeshOffsetAndCameraDistance( s_Mesh, &s_MeshOffset, &s_CameraDistance );
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

static void BindMesh( const CMesh& mesh )
{
    const uint32_t vertexFormat = mesh.GetVertexFormat();
    const uint32_t vertexSize = mesh.GetVertexSize();
    const uint32_t vertexBufferSize = mesh.GetVertexSize() * mesh.GetVerticesCount();

    {
        Rasterizer::SStream stream;
        stream.m_Data = mesh.GetVertices();
        stream.m_Offset = 0;
        stream.m_Stride = vertexSize;
        stream.m_Size = vertexBufferSize;
        Rasterizer::SetPositionStream( stream );
    }
    
    if ( ( vertexFormat & CMesh::EVertexFormat::eNormal ) != 0 )
    {
        Rasterizer::SStream stream;
        stream.m_Data = mesh.GetVertices();
        stream.m_Offset = mesh.GetNormalOffset();
        stream.m_Stride = vertexSize;
        stream.m_Size = vertexBufferSize;
        Rasterizer::SetNormalStream( stream );
    }

    if ( ( vertexFormat & CMesh::EVertexFormat::eColor ) != 0 )
    {
        Rasterizer::SStream stream;
        stream.m_Data = mesh.GetVertices();
        stream.m_Offset = mesh.GetColorOffset();
        stream.m_Stride = vertexSize;
        stream.m_Size = vertexBufferSize;
        Rasterizer::SetColorStream( stream );
    }

    if ( ( vertexFormat & CMesh::EVertexFormat::eTexcoord ) != 0 )
    {
        Rasterizer::SStream stream;
        stream.m_Data = mesh.GetVertices();
        stream.m_Offset = mesh.GetTexcoordOffset();
        stream.m_Stride = vertexSize;
        stream.m_Size = vertexBufferSize;
        Rasterizer::SetTexcoordStream( stream );
    }

    Rasterizer::SetIndexStream( mesh.GetIndices() );
}

static void FreeMeshAndTextures()
{
    s_Mesh.FreeAll();

    for ( CImage& texture : s_Textures )
    {
        texture.Free();
    }
    s_Textures.clear();
}

static void ComputeMeshOffsetAndCameraDistance( const CMesh& mesh, XMFLOAT3* meshOffset, float* cameraDistance )
{
    const BoundingBox bbox = s_Mesh.ComputeBoundingBox();
    *meshOffset = bbox.Center;
    const float lengthDiagonal = std::sqrt( bbox.Extents.x * bbox.Extents.x + bbox.Extents.y * bbox.Extents.y + bbox.Extents.z * bbox.Extents.z );
    *cameraDistance = lengthDiagonal * 2.2f;
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

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, const CMesh& mesh, float aspectRatio,
    const XMFLOAT3& meshOffset, float cameraPitch, float cameraYall, float cameraDistance )
{
    ZeroMemory( renderTarget.m_Bits, renderTarget.m_Width * renderTarget.m_Height * 4 );
    float* depthBit = (float*)depthTarget.m_Bits;
    for ( uint32_t i = 0; i < depthTarget.m_Width * depthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SMatrix matrix;

    XMMATRIX worldMatrix = XMMatrixTranslation( -meshOffset.x, -meshOffset.y, -meshOffset.z );
    XMMATRIX viewMatrix = XMMatrixInverse( nullptr, XMMatrixMultiply( XMMatrixTranslation( 0.f, 0.f, -cameraDistance ), XMMatrixRotationRollPitchYaw( cameraPitch, cameraYall, 0.f ) ) );
    XMMATRIX worldViewMatrix = XMMatrixMultiply( worldMatrix, viewMatrix );
    
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, worldViewMatrix );
    Rasterizer::SetWorldViewTransform( matrix );

    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( XMConvertToRadians( 40.0f ), aspectRatio, 2.f, 1000.f );
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    for ( uint32_t sectionIndex = 0; sectionIndex < mesh.GetSectionsCount(); ++sectionIndex )
    {
        const SMeshSection& section = mesh.GetSections()[ sectionIndex ];
        const SMeshMaterial& srcMaterial = mesh.GetMaterials()[ sectionIndex ];
        Rasterizer::SMaterial material;
        material.m_Diffuse = Rasterizer::SVector4( (float*)srcMaterial.m_Diffuse );
        material.m_Specular = Rasterizer::SVector3( (float*)srcMaterial.m_Specular );
        material.m_Power = srcMaterial.m_Power;
        Rasterizer::SetMaterial( material );

        const CImage* srcTexture = srcMaterial.m_DiffuseTexture;
        if ( srcTexture )
        { 
            Rasterizer::SImage texture;
            texture.m_Bits = srcTexture->GetData();
            texture.m_Width = srcTexture->GetWidth();
            texture.m_Height = srcTexture->GetHeight();
            Rasterizer::SetTexture( texture );
        }

        Rasterizer::SPipelineState pipelineState;
        pipelineState.m_UseTexture = srcTexture != nullptr;
        Rasterizer::SetPipelineState( pipelineState );

        Rasterizer::DrawIndexed( 0, section.m_IndexLocation, section.m_IndicesCount / 3 );
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
        RenderImage( d2dBitmap.Get(), renderTarget, depthTarget, s_Mesh, aspectRatio, s_MeshOffset, cameraPitch, cameraYall, s_CameraDistance );

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
    FreeMeshAndTextures();

    DestroyWindow( s_hWnd );

    return (int)msg.wParam;
}



