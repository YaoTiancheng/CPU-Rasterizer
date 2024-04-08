#include "UtilitiesPCH.h"
#include "Mesh.h"

// Divides two integers and rounds up
static inline uint32_t DivideAndRoundUp( uint32_t dividend, uint32_t divisor )
{
    return ( dividend + divisor - 1u ) / divisor;
}

bool CMesh::AllocateVertices( uint32_t vertexFormat, uint32_t verticesCount )
{
    assert( m_Vertices == nullptr );

    if ( ( vertexFormat & EVertexFormat::ePosition ) == 0 )
    {
        return false;
    }

    uint32_t vertexSize, positionOffset, normalOffset, colorOffset, texcoordOffset;
    vertexSize = ComputeVertexLayout( vertexFormat, &positionOffset, &normalOffset, &colorOffset, &texcoordOffset );

    if ( vertexSize == 0 )
    {
        return false;
    }

    uint32_t roundedUpVerticesCount = DivideAndRoundUp( verticesCount, 4u ) * 4;
    m_Vertices = (uint8_t*)malloc( vertexSize * roundedUpVerticesCount );
    m_VertexFormat = vertexFormat;
    m_NormalOffset = normalOffset;
    m_ColorOffset = colorOffset;
    m_TexcoordOffset = texcoordOffset;
    m_VertexSize = vertexSize;
    m_VerticesCount = verticesCount;

    return true;
}

void CMesh::FreeVertices()
{
    free( m_Vertices );
    m_Vertices = nullptr;
    m_VertexFormat = 0;
    m_NormalOffset = 0;
    m_ColorOffset = 0;
    m_TexcoordOffset = 0;
    m_VertexSize = 0;
    m_VerticesCount = 0;
}

void CMesh::AllocateIndices( uint32_t indicesCount )
{
    assert( m_Indices == nullptr );
    m_Indices = (uint32_t*)malloc( 4 * indicesCount );
    m_IndicesCount = indicesCount;
}

void CMesh::FreeIndices()
{
    free( m_Indices );
    m_Indices = nullptr;
    m_IndicesCount = 0;
}

uint32_t CMesh::ComputeVertexLayout( uint32_t vertexFormat, uint32_t* positionOffset, uint32_t* normalOffset, uint32_t* colorOffset, uint32_t* texcoordOffset )
{
    uint32_t vertexSize = 0;
    *positionOffset = vertexSize;
    vertexSize += ( vertexFormat & EVertexFormat::ePosition ) != 0 ? sizeof( float ) * 3 : 0;
    *normalOffset = vertexSize;
    vertexSize += ( vertexFormat & EVertexFormat::eNormal ) != 0 ? sizeof( float ) * 3 : 0;
    *colorOffset = vertexSize;
    vertexSize += ( vertexFormat & EVertexFormat::eColor ) != 0 ? sizeof( float ) * 3 : 0;
    *texcoordOffset = vertexSize;
    vertexSize += ( vertexFormat & EVertexFormat::eTexcoord ) != 0 ? sizeof( float ) * 2 : 0;
    return vertexSize;
}
