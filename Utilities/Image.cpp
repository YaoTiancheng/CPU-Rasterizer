#include "UtilitiesPCH.h"
#include "Image.h"

void CImage::Allocate( uint32_t width, uint32_t height )
{
    assert( m_Data == nullptr );
    m_Data = (uint8_t*)malloc( width * height * 4 );
    m_Width = width;
    m_Height = height;
}

void CImage::Free()
{
    free( m_Data );
    m_Width = 0;
    m_Height = 0;
}
