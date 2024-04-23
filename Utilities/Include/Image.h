#pragma once

class CImage
{
public:
    CImage() 
        : m_Data( nullptr )
        , m_Width( 0 )
        , m_Height( 0 )
    {}
    
    void Allocate( uint32_t width, uint32_t height );

    void Free();

    uint8_t* GetData() const { return m_Data; }

    uint32_t GetWidth() const { return m_Width; }

    uint32_t GetHeight() const { return m_Height; }

    uint32_t GetBytesPerPixel() const { return 4; }

    uint32_t GetPitch() const { return m_Width * GetBytesPerPixel(); }

private:
    uint8_t* m_Data;
    uint32_t m_Width, m_Height;
};