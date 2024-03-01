
#include "PCH.h"
#include "Rasterization.h"
#include "MathHelper.h"

using namespace Microsoft::WRL;
using namespace DirectX;

static const wchar_t* s_WindowClassName = L"RasterizerWindow";

struct SVertexBuffer
{
    struct SIterator
    {
        SIterator& SetVertex( float posX, float posY, float posZ, float texU, float texV )
        {
            *m_PosX = posX;
            *m_PosY = posY;
            *m_PosZ = posZ;
            *m_TexU = texU;
            *m_TexV = texV;
            return *this;
        }

        void MoveToNext()
        {
            ++m_PosX;
            ++m_PosY;
            ++m_PosZ;
            ++m_TexU;
            ++m_TexV;
        }

        float* m_PosX;
        float* m_PosY;
        float* m_PosZ;
        float* m_TexU;
        float* m_TexV;
    };

    SVertexBuffer()
        : m_Data( nullptr )
        , m_VerticesCount( 0 )
        , m_RoundedVerticesCount( 0 )
    {
    }

    void Allocate( uint32_t verticesCount )
    {
        assert( m_Data == nullptr && m_VerticesCount == 0 && m_RoundedVerticesCount == 0 );
        m_RoundedVerticesCount = MathHelper::DivideAndRoundUp( verticesCount, 4u ) * 4;
        const uint64_t bufferSize = m_RoundedVerticesCount * sizeof( float ) * 5;
        m_Data = (uint8_t*)_aligned_malloc( bufferSize, 16 );
        m_VerticesCount = verticesCount;
    }

    void Free()
    {
        _aligned_free( m_Data );
        m_VerticesCount = 0;
        m_RoundedVerticesCount = 0;
        m_Data = nullptr;
    }

    SIterator GetBeginIterator() { return SIterator{ GetPosX(), GetPosY(), GetPosZ(), GetTexU(), GetTexV() }; }

    uint32_t GetVerticesCount() const { return m_VerticesCount; }

    float* GetPosX() const { return (float*)m_Data; }
    float* GetPosY() const { return (float*)m_Data + m_RoundedVerticesCount; }
    float* GetPosZ() const { return (float*)m_Data + m_RoundedVerticesCount * 2; }
    float* GetTexU() const { return (float*)m_Data + m_RoundedVerticesCount * 3; }
    float* GetTexV() const { return (float*)m_Data + m_RoundedVerticesCount * 4; }

    uint32_t m_VerticesCount;
    uint64_t m_RoundedVerticesCount;
    uint8_t* m_Data;
};

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

    HWND hWnd = CreateWindowW( s_WindowClassName, L"CPU-Rasterizer", windowStyle,
        CW_USEDEFAULT, 0, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );

    return hWnd;
}

static bool CreateRenderData( uint32_t width, uint32_t height, Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, Rasterizer::SImage* texture, SVertexBuffer* vertexBuffer, uint32_t* triangleCount )
{
    vertexBuffer->Allocate( 36 );

    SVertexBuffer::SIterator vertexIter = vertexBuffer->GetBeginIterator();

    // Front 
    vertexIter.SetVertex( 1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, -1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, -1.f, -1.f, 0.f, 1.f ).MoveToNext();
    // Front 
    vertexIter.SetVertex( 1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, -1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, -1.f, 0.f, 0.f ).MoveToNext();

    // Left 
    vertexIter.SetVertex( -1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, -1.f, 1.f, 0.f, 1.f ).MoveToNext();
    // Left 
    vertexIter.SetVertex( -1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, -1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();

    // Right 
    vertexIter.SetVertex( 1.f, -1.f, 1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, -1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, -1.f, -1.f, 0.f, 1.f ).MoveToNext();
    // Right 
    vertexIter.SetVertex( 1.f, -1.f, 1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, 1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, -1.f, 0.f, 0.f ).MoveToNext();

    // Back 
    vertexIter.SetVertex( -1.f, -1.f, 1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, -1.f, 1.f, 0.f, 1.f ).MoveToNext();
    // Back 
    vertexIter.SetVertex( -1.f, -1.f, 1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, 1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();

    // Top 
    vertexIter.SetVertex( 1.f, 1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, -1.f, 0.f, 1.f ).MoveToNext();
    // Top 
    vertexIter.SetVertex( 1.f, 1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, 1.f, 1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, 1.f, 1.f, 0.f, 0.f ).MoveToNext();

    // Bottom 
    vertexIter.SetVertex( -1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, -1.f, 1.f, 0.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, -1.f, -1.f, 0.f, 1.f ).MoveToNext();
    // Bottom
    vertexIter.SetVertex( -1.f, -1.f, -1.f, 1.f, 1.f ).MoveToNext();
    vertexIter.SetVertex( -1.f, -1.f, 1.f, 1.f, 0.f ).MoveToNext();
    vertexIter.SetVertex( 1.f, -1.f, 1.f, 0.f, 0.f ).MoveToNext();

    *triangleCount = 12;

    renderTarget->m_Width = width;
    renderTarget->m_Height = height;
    renderTarget->m_Bits = (uint8_t*)_aligned_malloc( width * height * 4, 16 );

    depthTarget->m_Width = width;
    depthTarget->m_Height = height;
    depthTarget->m_Bits = (uint8_t*)_aligned_malloc( width * height * 4, 16 );

    texture->m_Width = 4;
    texture->m_Height = 4;
    texture->m_Bits = (uint8_t*)_aligned_malloc( texture->m_Width * texture->m_Height * 4, 16 );

    // Load the texture from file
    ComPtr<IWICImagingFactory> WICImagingFactory;
    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)WICImagingFactory.GetAddressOf() );
    if ( FAILED( hr ) )
    {
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> convertedFrame;

    if ( FAILED( WICImagingFactory->CreateDecoderFromFilename( L"Resources/DirectX12Ultimate.png", NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() ) ) )
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
    
    if ( FAILED( WICImagingFactory->CreateFormatConverter( convertedFrame.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( convertedFrame->Initialize( frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom ) ) )
    {
        return false;
    }

    const uint32_t textureByteSize = texture->m_Width * texture->m_Height * 4;
    texture->m_Bits = (uint8_t*)_aligned_malloc( textureByteSize, 16 );
    return SUCCEEDED( convertedFrame->CopyPixels( nullptr, texture->m_Width * 4, textureByteSize, (BYTE*)texture->m_Bits ) );
}

static void DestroyRenderData( Rasterizer::SImage* renderTarget, Rasterizer::SImage* depthTarget, Rasterizer::SImage* texture, SVertexBuffer* vertexBuffer )
{
    vertexBuffer->Free();
    _aligned_free( renderTarget->m_Bits );
    _aligned_free( depthTarget->m_Bits );
    _aligned_free( texture->m_Bits );
}

static void RenderImage( ID2D1Bitmap* d2dBitmap, Rasterizer::SImage& renderTarget, Rasterizer::SImage& depthTarget, uint32_t triangleCount, float aspectRatio, float& roll, float& pitch, float& yall )
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

    XMFLOAT4 baseColor[] = { { 1.f, 1.f, 1.0f, 1.0f }, { 0.8f, 0.4f, 0.0f, 1.0f }, { 0.8f, 0.2f, 0.5f, 1.0f }, { 0.3f, 0.5f, 0.28f, 1.0f } };

    XMMATRIX viewMatrix = XMMatrixSet( 1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 10.f, 1.f );
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH( 1.0f, aspectRatio, 2.f, 1000.f );
    XMMATRIX viewProjectionMatrix = XMMatrixMultiply( viewMatrix, projectionMatrix );
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw( pitch, yall, roll );

    XMINT3 cubeCount( 3, 3, 3 );
    XMFLOAT3 cubeSpacing( 3.f, 3.f, 3.f );
    XMFLOAT3 cubeCenterMin( -( cubeCount.x - 1 ) * cubeSpacing.x * 0.5f, -( cubeCount.y - 1 ) * cubeSpacing.y * 0.5f, -( cubeCount.z - 1 ) * cubeSpacing.z * 0.5f );
    float matrix[ 16 ];
    for ( int32_t z = 0; z < cubeCount.z; ++z )
    {
        for ( int32_t y = 0; y < cubeCount.y; ++y )
        {
            for ( int32_t x = 0; x < cubeCount.x; ++x )
            {
                const int32_t index = z * cubeCount.x * cubeCount.y + y * cubeCount.x + x;
                Rasterizer::SetBaseColor( (float*)&baseColor[ index % 4 ] );

                XMFLOAT3 center( cubeCenterMin.x + cubeSpacing.x * x, cubeCenterMin.y + cubeSpacing.y * y, cubeCenterMin.z + cubeSpacing.z * z );
                XMMATRIX translationMatrix = XMMatrixTranslation( center.x, center.y, center.z );
                XMMATRIX worldViewProjectionMatrix = XMMatrixMultiply( XMMatrixMultiply( translationMatrix, rotationMatrix ), viewProjectionMatrix );
                XMStoreFloat4x4( (XMFLOAT4X4*)matrix, worldViewProjectionMatrix );
                Rasterizer::SetViewProjectionMatrix( matrix );

                Rasterizer::Draw( 0, triangleCount );
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
    Rasterizer::SImage texture;
    SVertexBuffer vertexBuffer;
    uint32_t triangleCount;
    CreateRenderData( width, height, &renderTarget, &depthTarget, &texture, &vertexBuffer, &triangleCount );

    Rasterizer::SViewport viewport;
    viewport.m_Left = 0;
    viewport.m_Top = 0;
    viewport.m_Width = width;
    viewport.m_Height = height;

    Rasterizer::SetPositionStreams( vertexBuffer.GetPosX(), vertexBuffer.GetPosY(), vertexBuffer.GetPosZ() );
    Rasterizer::SetTexcoordStreams( vertexBuffer.GetTexU(), vertexBuffer.GetTexV() );
    Rasterizer::SetRenderTarget( renderTarget );
    Rasterizer::SetDepthTarget( depthTarget );
    Rasterizer::SetViewport( viewport );
    Rasterizer::SetTexture( texture );

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

        RenderImage( d2dBitmap.Get(), renderTarget, depthTarget, triangleCount, aspectRatio, roll, pitch, yall );

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

    DestroyRenderData( &renderTarget, &depthTarget, &texture, &vertexBuffer );

    DestroyWindow( hWnd );

    return (int)msg.wParam;
}



