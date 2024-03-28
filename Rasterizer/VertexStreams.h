#pragma once

struct SVertexStreams4
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
        m_Z = (float*)_aligned_malloc( streamSize, 16 );
        m_W = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
        _aligned_free( m_Z );
        _aligned_free( m_W );
    }

    SVertexStreams4& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        m_Z += num;
        m_W += num;
        return *this;
    }

    SVertexStreams4 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num, m_Z + num, m_W + num };
    }

    float* m_X = nullptr;
    float* m_Y = nullptr;
    float* m_Z = nullptr;
    float* m_W = nullptr;
};

struct SVertexStreams3
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
        m_Z = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
        _aligned_free( m_Z );
    }

    SVertexStreams3& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        m_Z += num;
        return *this;
    }

    SVertexStreams3 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num, m_Z + num };
    }

    float* m_X = nullptr;
    float* m_Y = nullptr;
    float* m_Z = nullptr;
};

struct SVertexStreams2
{
    void Allocate( uint64_t streamSize )
    {
        m_X = (float*)_aligned_malloc( streamSize, 16 );
        m_Y = (float*)_aligned_malloc( streamSize, 16 );
    }

    void Free()
    {
        _aligned_free( m_X );
        _aligned_free( m_Y );
    }

    SVertexStreams2& operator+=( uint32_t num )
    {
        m_X += num;
        m_Y += num;
        return *this;
    }

    SVertexStreams2 operator+( uint32_t num ) const
    {
        return { m_X + num, m_Y + num };
    }

    float* m_X = nullptr;
    float* m_Y = nullptr;
};