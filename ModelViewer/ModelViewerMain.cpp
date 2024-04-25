
#include "ModelViewerPCH.h"
#include "Rasterizer.h"
#include "Scene.h"
#include "SceneLoader.h"

using namespace Microsoft::WRL;
using namespace DirectX;

struct SMeshDrawCommand
{
    XMFLOAT4X3 m_WorldMatrix;
    Rasterizer::SStream m_PositionStream;
    Rasterizer::SStream m_NormalStream;
    Rasterizer::SStream m_TexcoordsStream;
    Rasterizer::SStream m_ColorStream;
    Rasterizer::SStream m_IndexStream;
    Rasterizer::EIndexType m_IndexType;
    uint32_t m_PrimitiveCount;
    Rasterizer::SMaterial m_Material;
    Rasterizer::SImage m_DiffuseTexture;
    uint8_t m_AlphaRef;
    bool m_AlphaTest;
    bool m_TwoSided;
};

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

static HWND s_hWnd = NULL;
static CScene s_Scene;
static std::vector<SMeshDrawCommand> s_CachedMeshDrawCommands;
static float s_CameraDistance = 0.f;

static void CacheMeshDrawCommands( const CScene&, const XMFLOAT3& offset, std::vector<SMeshDrawCommand>* );
static void ComputeMeshOffsetAndCameraDistance( const CScene&, XMFLOAT3*, float* );

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
                    XMFLOAT3 offset;
                    ComputeMeshOffsetAndCameraDistance( s_Scene, &offset, &s_CameraDistance );
                    CacheMeshDrawCommands( s_Scene, offset, &s_CachedMeshDrawCommands );
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

inline Rasterizer::SStream TranslateSceneStream( const CScene& scene, const SSceneStream& stream )
{
    Rasterizer::SStream out;
    if ( stream.m_Buffer != -1 )
    { 
        out.m_Data = scene.m_Buffers[ stream.m_Buffer ].m_Data;
        out.m_Offset = stream.m_ByteOffset;
        out.m_Stride = stream.m_ByteStride;
        out.m_Size = stream.m_ByteSize;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

inline Rasterizer::SMaterial TranslateSceneMaterial( const CScene& scene, int32_t materialIndex )
{
    Rasterizer::SMaterial out;
    if ( materialIndex != -1 )
    { 
        const SSceneMaterial& material = scene.m_Materials[ materialIndex ];
        out.m_Diffuse = Rasterizer::SVector4( (float*)&material.m_Diffuse );
        out.m_Specular = Rasterizer::SVector3( (float*)&material.m_Specular );
        out.m_Power = material.m_Power;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

inline Rasterizer::SImage TranslateSceneImage( const CScene& scene, int32_t imageIndex )
{
    Rasterizer::SImage out;
    if ( imageIndex != -1 )
    { 
        const SSceneImage& image = scene.m_Images[ imageIndex ];
        out.m_Bits = image.m_Data;
        out.m_Width = image.m_Width;
        out.m_Height = image.m_Height;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

static void CacheMeshDrawCommands( const CScene& scene, const XMFLOAT3& offset, std::vector<SMeshDrawCommand>* commands )
{
    for ( int32_t meshNodeIndex : scene.m_MeshNodes )
    {
        const SSceneNode* meshNode = &scene.m_Nodes[ meshNodeIndex ];
        const SSceneNode* node = meshNode;
        XMMATRIX worldMatrix = XMLoadFloat4x3( &node->m_LocalTransform );
        while ( node->m_Parent != -1 )
        {
            node = &scene.m_Nodes[ node->m_Parent ];
            XMMATRIX parentMatrix = XMLoadFloat4x3( &node->m_LocalTransform );
            worldMatrix = XMMatrixMultiply( worldMatrix, parentMatrix );
        }
        worldMatrix = XMMatrixMultiply( worldMatrix, XMMatrixTranslation( -offset.x, -offset.y, -offset.z ) );

        XMFLOAT4X3A matrix;
        XMStoreFloat4x3A( &matrix, worldMatrix );

        const SSceneMesh& mesh = scene.m_Meshes[ meshNode->m_Mesh ];
        commands->reserve( commands->size() + mesh.m_Sections.size() );
        for ( const SSceneMeshSection& section : mesh.m_Sections )
        {
            commands->emplace_back();
            SMeshDrawCommand& newCommand = commands->back();
            newCommand.m_WorldMatrix = matrix;
            newCommand.m_PositionStream = TranslateSceneStream( scene, section.m_PositionStream );
            newCommand.m_NormalStream = TranslateSceneStream( scene, section.m_NormalStream );
            newCommand.m_TexcoordsStream = TranslateSceneStream( scene, section.m_TexcoordsTream );
            newCommand.m_ColorStream = TranslateSceneStream( scene, section.m_ColorStream );
            newCommand.m_IndexStream = TranslateSceneStream( scene, section.m_IndexStream );
            newCommand.m_IndexType = section.m_Is32bitIndex ? Rasterizer::EIndexType::e32bit : Rasterizer::EIndexType::e16bit;
            newCommand.m_PrimitiveCount = section.m_PrimitivesCount;
            newCommand.m_Material = TranslateSceneMaterial( scene, section.m_Material );
            if ( section.m_Material != -1 )
            { 
                const SSceneMaterial& material = scene.m_Materials[ section.m_Material ];
                newCommand.m_DiffuseTexture = TranslateSceneImage( scene, material.m_DiffuseTexture );
                newCommand.m_AlphaRef = (uint8_t)( material.m_AlphaThreshold * 255.f + 0.5f );
                newCommand.m_AlphaTest = material.m_AlphaTest;
                newCommand.m_TwoSided = material.m_TwoSided;
            }
            else
            {
                newCommand.m_DiffuseTexture = TranslateSceneImage( scene, -1 );
                newCommand.m_AlphaRef = 0;
                newCommand.m_AlphaTest = false;
                newCommand.m_TwoSided = false;
            }
        }
    }
}

static void ComputeMeshOffsetAndCameraDistance( const CScene& scene, XMFLOAT3* meshOffset, float* cameraDistance )
{
    BoundingSphere sphere = scene.CalculateBoundingSphere();
    *meshOffset = sphere.Center;
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

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, const std::vector<SMeshDrawCommand>& meshDrawCommands, float aspectRatio,
    float cameraPitch, float cameraYall, float cameraDistance )
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
    viewMatrix = XMMatrixInverse( nullptr, XMMatrixMultiply( viewMatrix, XMMatrixRotationRollPitchYaw( cameraPitch, cameraYall, 0.f ) ) );

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
        RenderImage( d2dBitmap.Get(), renderTarget, depthTarget, s_CachedMeshDrawCommands, aspectRatio, cameraPitch, cameraYall, s_CameraDistance );

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



