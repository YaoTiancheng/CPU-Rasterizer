
#include "ModelViewerPCH.h"
#include "Rasterizer.h"
#include "DemoApp.h"
#include "Scene.h"
#include "SceneLoader.h"
#include "SceneRendering.h"

using namespace DirectX;

struct SMeshDrawCommandInfo
{
    const SMeshDrawCommand* m_Command;
    float m_DistanceToCamera;
};

class CDemoApp_ModelViewer : public CDemoApp
{
    enum class ECameraMode
    {
        Orbit, Fly
    };

    using CDemoApp::CDemoApp;

    virtual bool OnInit() override;
    virtual void OnDestroy() override;
    virtual void OnUpdate() override;
    virtual bool OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) override;

    void ComputeCameraLookAtAndDistance( const std::vector<BoundingBox>& meshSectionBoundingBoxes );
    void UpdateCamera();

    Rasterizer::SImage m_RenderTarget, m_DepthTarget;
    CScene m_Scene;
    std::vector<SMeshDrawCommand> m_CachedMeshDrawCommands;
    size_t m_TranslucentMeshDrawCommandsStart = 0;
    XMFLOAT3 m_CameraLookAt = { 0.f, 0.f, 0.f };
    float m_CameraDistance = 0.f;
    float m_CameraPitch = 0.f, m_CameraYall = 0.f;
    ECameraMode m_CameraMode = ECameraMode::Orbit;
};

bool CDemoApp_ModelViewer::OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
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
            ofn.hwndOwner = m_hWnd;
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
                m_Scene.FreeAll();
                m_CachedMeshDrawCommands.clear();
                m_TranslucentMeshDrawCommandsStart = 0;
                if ( LoadSceneFronGLTFFile( filename, &m_Scene ) )
                {
                    m_Scene.FlipCoordinateHandness();
                    std::vector<XMFLOAT4X3> nodeWorldTransforms;
                    std::vector<BoundingBox> meshSectionBoundingBoxes;
                    m_Scene.CalculateNodeWorldTransforms( &nodeWorldTransforms );
                    m_Scene.CalculateMeshSectionBoundingBoxes( &meshSectionBoundingBoxes ); // Local space bounding boxes
                    m_Scene.TransformMeshSectionBoundingBoxes( nodeWorldTransforms, meshSectionBoundingBoxes ); // World space bounding boxes
                    ComputeCameraLookAtAndDistance( meshSectionBoundingBoxes );
                    GenerateMeshDrawCommands( m_Scene, nodeWorldTransforms, &meshSectionBoundingBoxes, &m_CachedMeshDrawCommands );
                    // Order all opaque draws before translucent ones.
                    if ( !m_CachedMeshDrawCommands.empty() )
                    { 
                        auto iterTranslucent = std::partition( m_CachedMeshDrawCommands.begin(), m_CachedMeshDrawCommands.end(), 
                            []( const SMeshDrawCommand& command ) { return !command.m_AlphaBlend; } );
                        m_TranslucentMeshDrawCommandsStart = std::distance( m_CachedMeshDrawCommands.begin(), iterTranslucent );
                    }
                }
            }
        }
        else if ( wParam == 'C' )
        {
            m_CameraMode = m_CameraMode == ECameraMode::Orbit ? ECameraMode::Fly : ECameraMode::Orbit;
        }
        break;
    }
    return false;
}

void CDemoApp_ModelViewer::ComputeCameraLookAtAndDistance( const std::vector<BoundingBox>& meshSectionBoundingBoxes )
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    if ( !meshSectionBoundingBoxes.empty() )
    {
        BoundingBox mergedBox = meshSectionBoundingBoxes.front();
        for ( const BoundingBox& box : meshSectionBoundingBoxes )
        {
            BoundingBox::CreateMerged( mergedBox, mergedBox, box );
        }
        BoundingSphere::CreateFromBoundingBox( sphere, mergedBox );
    }
    m_CameraLookAt = sphere.Center;
    m_CameraDistance = sphere.Radius * 2.2f;
}

void CDemoApp_ModelViewer::UpdateCamera()
{
    const float cameraOrbitingSensitivity = XMConvertToRadians( 1.f ) * ( m_CameraMode == ECameraMode::Orbit ? 1 : -1 );
    if ( GetAsyncKeyState( VK_UP ) )
    {
        m_CameraPitch += cameraOrbitingSensitivity;
    }
    else if ( GetAsyncKeyState( VK_DOWN ) )
    {
        m_CameraPitch -= cameraOrbitingSensitivity;
    }
    if ( GetAsyncKeyState( VK_LEFT ) )
    {
        m_CameraYall += cameraOrbitingSensitivity;
    }
    else if ( GetAsyncKeyState( VK_RIGHT ) )
    {
        m_CameraYall -= cameraOrbitingSensitivity;
    }

    const float cameraMoveSensitivity = 0.04f;
    if ( GetAsyncKeyState( VK_SUBTRACT ) )
    {
        m_CameraDistance = std::max( 0.f, m_CameraDistance - cameraMoveSensitivity );
    }
    else if ( GetAsyncKeyState( VK_ADD ) )
    {
        m_CameraDistance += cameraMoveSensitivity;
    }

    const XMMATRIX cameraRotationMatrix = XMMatrixRotationRollPitchYaw( m_CameraPitch, m_CameraYall, 0.f );
    const XMVECTOR cameraRight = XMVector3Transform( g_XMIdentityR0, cameraRotationMatrix );
    const XMVECTOR cameraUp = XMVector3Transform( g_XMIdentityR1, cameraRotationMatrix );
    const XMVECTOR cameraForward = XMVector3Transform( g_XMIdentityR2, cameraRotationMatrix );
    XMVECTOR cameraLookAt = XMLoadFloat3( &m_CameraLookAt );
    if ( GetAsyncKeyState( 'A' ) )
    {
        cameraLookAt -= cameraRight * cameraMoveSensitivity;
    }
    else if ( GetAsyncKeyState( 'D' ))
    {
        cameraLookAt += cameraRight * cameraMoveSensitivity;
    }
    if ( GetAsyncKeyState( 'W' ) )
    {
        cameraLookAt += cameraForward * cameraMoveSensitivity;
    }
    else if ( GetAsyncKeyState( 'S' ))
    {
        cameraLookAt -= cameraForward * cameraMoveSensitivity;
    }
    if ( GetAsyncKeyState( 'Q' ) )
    {
        cameraLookAt += cameraUp * cameraMoveSensitivity;
    }
    else if ( GetAsyncKeyState( 'E' ))
    {
        cameraLookAt -= cameraUp * cameraMoveSensitivity;
    }
    XMStoreFloat3( &m_CameraLookAt, cameraLookAt );
}

void CDemoApp_ModelViewer::OnUpdate()
{
    UpdateCamera();

    XMMATRIX cameraWorldMatrix = XMMatrixTranslation( 0.f, 0.f, m_CameraMode == ECameraMode::Orbit ? -m_CameraDistance : 0.f );
    cameraWorldMatrix = XMMatrixMultiply( cameraWorldMatrix, XMMatrixRotationRollPitchYaw( m_CameraPitch, m_CameraYall, 0.f ) );
    cameraWorldMatrix = XMMatrixMultiply( cameraWorldMatrix, XMMatrixTranslation( m_CameraLookAt.x, m_CameraLookAt.y, m_CameraLookAt.z ) );

    std::vector<SMeshDrawCommandInfo> commandsInfo;

    {
        const XMVECTOR cameraPosition = cameraWorldMatrix.r[ 3 ];
        commandsInfo.reserve( m_CachedMeshDrawCommands.size() );
        for ( const SMeshDrawCommand& command : m_CachedMeshDrawCommands )
        {
            commandsInfo.emplace_back();
            SMeshDrawCommandInfo& commandInfo = commandsInfo.back();
            commandInfo.m_Command = &command;
            const XMFLOAT3 position = command.m_BoundingBox.Center;
            XMStoreFloat( &commandInfo.m_DistanceToCamera, XMVector3LengthSq( XMLoadFloat3( &position ) - cameraPosition ) );
        }

        // Sort opaque draws front to back to minimize overdraw
        std::sort( commandsInfo.begin(), commandsInfo.begin() + m_TranslucentMeshDrawCommandsStart, 
            []( const SMeshDrawCommandInfo& infoA, const SMeshDrawCommandInfo& infoB ) { return infoA.m_DistanceToCamera < infoB.m_DistanceToCamera; } );
        // Sort translucent draws back to front to maintain correct blending result
        std::sort( commandsInfo.begin() + m_TranslucentMeshDrawCommandsStart, commandsInfo.end(),
            []( const SMeshDrawCommandInfo& infoA, const SMeshDrawCommandInfo& infoB ) { return infoA.m_DistanceToCamera > infoB.m_DistanceToCamera; } );
    }

    ZeroMemory( m_RenderTarget.m_Bits, m_RenderTarget.m_Width * m_RenderTarget.m_Height * 4 );
    float* depthBit = (float*)m_DepthTarget.m_Bits;
    for ( uint32_t i = 0; i < m_DepthTarget.m_Width * m_DepthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SMatrix matrix;

    XMMATRIX viewMatrix = XMMatrixInverse( nullptr, cameraWorldMatrix );

    const float aspectRatio = (float)m_RenderTarget.m_Width / m_RenderTarget.m_Height;
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( XMConvertToRadians( 40.0f ), aspectRatio, 1.f, 1000.f );
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    for ( const SMeshDrawCommandInfo& commandInfo : commandsInfo )
    {
        const SMeshDrawCommand& command = *commandInfo.m_Command;
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
        Rasterizer::SetEnableDepthWrite( !command.m_AlphaBlend );

        Rasterizer::SPipelineState pipelineState;
        pipelineState.m_UseTexture = command.m_DiffuseTexture.m_Bits != nullptr;
        pipelineState.m_UseVertexColor = command.m_ColorStream.m_Data != nullptr;
        pipelineState.m_EnableAlphaTest = command.m_AlphaTest;
        pipelineState.m_EnableAlphaBlend = command.m_AlphaBlend;
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

    CopyToSwapChain( m_RenderTarget );
}

bool CDemoApp_ModelViewer::OnInit()
{
    uint32_t width, height;
    GetSwapChainSize( &width, &height );

    m_RenderTarget.m_Width = width;
    m_RenderTarget.m_Height = height;
    m_RenderTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );
    m_DepthTarget.m_Width = width;
    m_DepthTarget.m_Height = height;
    m_DepthTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    Rasterizer::SViewport viewport;
    viewport.m_Left = 0;
    viewport.m_Top = 0;
    viewport.m_Width = width;
    viewport.m_Height = height;

    Rasterizer::Initialize();
    Rasterizer::SetRenderTarget( m_RenderTarget );
    Rasterizer::SetDepthTarget( m_DepthTarget );
    Rasterizer::SetViewport( viewport );

    return true;
}

void CDemoApp_ModelViewer::OnDestroy()
{
    m_Scene.FreeAll();
    m_CachedMeshDrawCommands.clear();
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    CDemoApp_ModelViewer demoApp( L"Model Viewer", hInstance, 800, 600 );
    int returnCode = 0;
    if ( demoApp.Initialize() )
    {
        returnCode = demoApp.Execute();
    }
    demoApp.Destroy();
    return returnCode;
}