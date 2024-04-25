#include "UtilitiesPCH.h"
#include "Image.h"

using namespace Microsoft::WRL;

static ComPtr<IWICImagingFactory> s_WICFactory;

bool LoadImageFromFile( const char* filename, CImage* image )
{
    if ( !s_WICFactory )
    {
        if ( FAILED( CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)s_WICFactory.GetAddressOf() ) ) )
        {
            return false;
        }
    }

    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> convertedFrame;

    wchar_t wideFilename[ MAX_PATH ];
    {
        size_t numCharConverted = 0;
        const size_t filenameLength = strlen( filename ) + 1;
        mbstowcs_s( &numCharConverted, wideFilename, filename, filenameLength );
        if ( numCharConverted != filenameLength )
        {
            return false;
        }
    }

    if ( FAILED( s_WICFactory->CreateDecoderFromFilename( wideFilename, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( decoder->GetFrame( 0, frame.GetAddressOf() ) ) )
    {
        return false;
    }

    uint32_t width = 0, height = 0;
    if ( FAILED( frame->GetSize( &width, &height ) ) )
    {
        return false;
    }
    
    if ( FAILED( s_WICFactory->CreateFormatConverter( convertedFrame.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( convertedFrame->Initialize( frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom ) ) )
    {
        return false;
    }

    image->Allocate( width, height );
    convertedFrame->CopyPixels( nullptr, image->GetPitch(), image->GetPitch() * image->GetHeight(), (BYTE*)image->GetData() );
    return true;
}