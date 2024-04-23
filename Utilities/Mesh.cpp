#include "UtilitiesPCH.h"
#include "Mesh.h"

using namespace DirectX;

// Divides two integers and rounds up
static inline uint32_t DivideAndRoundUp( uint32_t dividend, uint32_t divisor )
{
    return ( dividend + divisor - 1u ) / divisor;
}

SMeshMaterial SMeshMaterial::GetDefault()
{
    SMeshMaterial material;
    material.m_DiffuseTexture = nullptr;
    material.m_Diffuse[ 0 ] = 1.f;
    material.m_Diffuse[ 1 ] = 0.f;
    material.m_Diffuse[ 2 ] = 1.f;
    material.m_Diffuse[ 3 ] = 1.f;
    material.m_Specular[ 0 ] = 0.f;
    material.m_Specular[ 1 ] = 0.f;
    material.m_Specular[ 2 ] = 0.f;
    material.m_Power = 1.f;
    return material;
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

void CMesh::AllocateSections( uint32_t sectionsCount )
{
    assert( m_Sections == nullptr );
    m_Sections = (SMeshSection*)malloc( sectionsCount * sizeof( SMeshSection ) );
    m_SectionsCount = sectionsCount;
}

void CMesh::FreeSections()
{
    free( m_Sections );
    m_Sections = nullptr;
    m_SectionsCount = 0;
}

void CMesh::AllocateMaterials( )
{
    assert( m_Materials == nullptr );
    m_Materials = new SMeshMaterial[ m_SectionsCount ];
}

void CMesh::FreeMaterials()
{
    delete[] m_Materials;
    m_Materials = nullptr;
}

void CMesh::FreeAll()
{
    FreeVertices();
    FreeIndices();
    FreeSections();
    FreeMaterials();
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

void CMesh::FlipCoordinateHandness()
{
    // Flip position and normal against the yz plane
    float* position = GetPosition( 0 );
    float* normal = GetNormal( 0 );
    for ( uint32_t i = 0; i < m_VerticesCount; ++i )
    {
        position[ 0 ] = -position[ 0 ];
        normal[ 0 ] = -normal[ 0 ];
        position = (float*)( (uint8_t*)position + m_VertexSize );
        normal = (float*)( (uint8_t*)normal + m_VertexSize );
    }
}

BoundingBox CMesh::ComputeBoundingBox() const
{
    BoundingBox box;
    const float* pos = GetPosition( 0 );
    BoundingBox::CreateFromPoints( box, m_VerticesCount, (const XMFLOAT3*)pos, m_VertexSize );
    return box;
}
