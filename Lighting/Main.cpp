
#include "PCH.h"
#include "Rasterizer.h"
#include "DemoApp.h"
#include "Scene.h"
#include "SceneLoader.h"
#include "SceneRendering.h"

using namespace DirectX;

class CDemoApp_Lighting : public CDemoApp
{
    using CDemoApp::CDemoApp;

    virtual bool OnInit() override;
    virtual void OnDestroy() override;
    virtual void OnUpdate() override;
    virtual bool OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) override;

    bool CreateRenderData( uint32_t width, uint32_t height );
    void DestroyRenderData();

    uint32_t m_LightOrbitMode = 0;
    Rasterizer::ELightingModel m_LightingModel = Rasterizer::ELightingModel::eBlinnPhong;
    Rasterizer::ELightType m_LightType = Rasterizer::ELightType::eDirectional;
    float m_LightOrbitAngle = 0.f;

    Rasterizer::SImage m_RenderTarget;
    Rasterizer::SImage m_DepthTarget;
    CScene m_Scene;
    std::vector<SMeshDrawCommand> m_MeshDrawCommands;
};

bool CDemoApp_Lighting::OnWndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    switch ( message )
    {
    case WM_KEYDOWN:
        if ( ( lParam & 0x40000000 ) == 0 )
        { 
            if ( wParam == 'O' )
            {
                m_LightOrbitMode ^= 1u;
            }
            else if ( wParam == 'L' )
            {
                m_LightType = m_LightType == Rasterizer::ELightType::eDirectional ? Rasterizer::ELightType::ePoint : Rasterizer::ELightType::eDirectional;
            }
            else if ( wParam == 'M' )
            {
                m_LightingModel = (Rasterizer::ELightingModel)( ( (uint32_t)m_LightingModel + 1 ) % (uint32_t)Rasterizer::ELightingModel::eCount );
            }
        }
        break;
    }
    return false;
}

bool CDemoApp_Lighting::CreateRenderData( uint32_t width, uint32_t height )
{
    if ( !LoadSceneFronGLTFFile( "Resources/Teapot.glb", &m_Scene ) )
    {
        return false;
    }

    // The teapot.obj is in right hand coordinate, convert it to left hand coordinate
    m_Scene.FlipCoordinateHandness();

    std::vector<XMFLOAT4X3> nodeWorldTransforms;
    CalculateNodeWorldTransforms( m_Scene, &nodeWorldTransforms );
    GenerateMeshDrawCommands( m_Scene, nodeWorldTransforms, &m_MeshDrawCommands );

    m_RenderTarget.m_Width = width;
    m_RenderTarget.m_Height = height;
    m_RenderTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    m_DepthTarget.m_Width = width;
    m_DepthTarget.m_Height = height;
    m_DepthTarget.m_Bits = (uint8_t*)malloc( width * height * 4 );

    return true;
}

void CDemoApp_Lighting::DestroyRenderData()
{
    m_Scene.FreeAll();
    free( m_RenderTarget.m_Bits );
    free( m_DepthTarget.m_Bits );
}

void CDemoApp_Lighting::OnUpdate()
{
    m_LightOrbitAngle += XMConvertToRadians( 0.5f );

    ZeroMemory( m_RenderTarget.m_Bits, m_RenderTarget.m_Width * m_RenderTarget.m_Height * 4 );
    float* depthBit = (float*)m_DepthTarget.m_Bits;
    for ( uint32_t i = 0; i < m_DepthTarget.m_Width * m_DepthTarget.m_Height; ++i )
    {
        *depthBit = 1.f;
        ++depthBit;
    }

    Rasterizer::SMatrix matrix;

    const float aspectRatio = (float)m_RenderTarget.m_Width / m_RenderTarget.m_Height;
    const XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( XMConvertToRadians( 40.0f ), aspectRatio, 2.f, 1000.f );
    XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, projectionMatrix );
    Rasterizer::SetProjectionTransform( matrix );

    const XMMATRIX viewMatrix = XMMatrixSet(
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 9.0f, 1.f );

    Rasterizer::SLight light;
    light.m_Diffuse = Rasterizer::SVector3( 1.f, 1.f, 1.f );
    light.m_Specular = Rasterizer::SVector3( 1.f, 1.f, 1.f );
    light.m_Ambient = Rasterizer::SVector3( 0.05f, 0.06f, 0.05f );
    XMMATRIX rotationMatrix = m_LightOrbitMode == 0 ? XMMatrixRotationRollPitchYaw( 0.f, m_LightOrbitAngle, 0.f ) : XMMatrixRotationRollPitchYaw( m_LightOrbitAngle, 0.f, 0.f );
    XMMATRIX lightWorldViewMatrix = XMMatrixMultiply( rotationMatrix, viewMatrix );
    XMVECTOR lightPosition = m_LightOrbitMode == 0 ? XMVectorSet( -3.1f, 0.f, 0.f, 0.f ) : XMVectorSet( 0.f, 3.1f, 0.f, 0.f );
    lightPosition = m_LightType == Rasterizer::ELightType::eDirectional ? 
        XMVector3Normalize( XMVector3TransformNormal( lightPosition, lightWorldViewMatrix ) ) :
        XMVector3Transform( lightPosition, lightWorldViewMatrix );
    XMStoreFloat3( (XMFLOAT3*)light.m_Position.m_Data, lightPosition );
    Rasterizer::SetLight( light );

    Rasterizer::SMaterial material;
    material.m_Diffuse = Rasterizer::SVector4( 0.5f, 0.6f, 0.5f, 1.f );
    material.m_Specular = Rasterizer::SVector3( 0.5f, 0.5f, 0.5f );
    material.m_Power = 40.f;
    Rasterizer::SetMaterial( material );

    Rasterizer::SPipelineState pipelineState;
    pipelineState.m_LightingModel = m_LightingModel;
    pipelineState.m_LightType = m_LightType;
    Rasterizer::SetPipelineState( pipelineState );

    for ( const SMeshDrawCommand& command : m_MeshDrawCommands )
    {
        Rasterizer::SetPositionStream( command.m_PositionStream );
        Rasterizer::SetNormalStream( command.m_NormalStream );
        Rasterizer::SetColorStream( command.m_ColorStream );
        Rasterizer::SetTexcoordStream( command.m_TexcoordsStream );
        Rasterizer::SetCullMode( command.m_TwoSided ? Rasterizer::ECullMode::eNone : Rasterizer::ECullMode::eCullCW );

        XMMATRIX worldMatrix = XMLoadFloat4x3( &command.m_WorldMatrix );
        XMMATRIX worldViewMatrix = XMMatrixMultiply( worldMatrix, viewMatrix );
        XMStoreFloat4x4A( (XMFLOAT4X4A*)&matrix, worldViewMatrix );
        Rasterizer::SetWorldViewTransform( matrix );

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

bool CDemoApp_Lighting::OnInit()
{
    uint32_t width, height;
    GetSwapChainSize( &width, &height );

    if ( !CreateRenderData( width, height ) )
    {
        return false;
    }

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

void CDemoApp_Lighting::OnDestroy()
{
    DestroyRenderData();
}

int APIENTRY wWinMain( _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow )
{
    UNREFERENCED_PARAMETER( hPrevInstance );
    UNREFERENCED_PARAMETER( lpCmdLine );

    CDemoApp_Lighting demoApp( L"Lighting", hInstance, 800, 600 );
    int returnCode = 0;
    if ( demoApp.Initialize() )
    {
        returnCode = demoApp.Execute();
    }
    demoApp.Destroy();
    return returnCode;
}



