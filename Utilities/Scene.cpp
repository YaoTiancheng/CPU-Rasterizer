#include "UtilitiesPCH.h"
#include "Scene.h"

using namespace DirectX;

void CScene::FlipCoordinateHandness()
{
    for ( SSceneNode& node : m_Nodes )
    {
        if ( node.m_Parent == -1 )
        {
            node.m_LocalTransform._11 = -node.m_LocalTransform._11;
            node.m_LocalTransform._21 = -node.m_LocalTransform._21;
            node.m_LocalTransform._21 = -node.m_LocalTransform._21;
            node.m_LocalTransform._31 = -node.m_LocalTransform._31;
        }
    }
}

void CScene::FreeAll()
{
    for ( SSceneBuffer& buffer : m_Buffers )
    {
        free( buffer.m_Data );
    }

    for ( SSceneImage& image : m_Images )
    {
        free( image.m_Data );
    }

    m_Nodes.clear();
    m_Meshes.clear();
    m_Materials.clear();
    m_Buffers.clear();
    m_Images.clear();
    m_MeshNodes.clear();
}

BoundingBox CScene::CalculateMeshSectionBoundingBox( const SSceneMeshSection& section ) const
{
    BoundingBox box( XMFLOAT3( 0.f, 0.f, 0.f ), XMFLOAT3( 0.f, 0.f, 0.f ) );
    if ( section.m_PositionStream.m_Buffer != -1 )
    {
        const SSceneBuffer& buffer = m_Buffers[ section.m_PositionStream.m_Buffer ];
        BoundingBox::CreateFromPoints( box, section.m_PositionStream.m_ByteSize / section.m_PositionStream.m_ByteStride,
            (const XMFLOAT3*)( buffer.m_Data + section.m_PositionStream.m_ByteOffset ), section.m_PositionStream.m_ByteStride );
    }
    return box;
}

BoundingBox CScene::CalculateMeshBoundingBox( const SSceneMesh& mesh ) const
{
    BoundingBox box( XMFLOAT3( 0.f, 0.f, 0.f ), XMFLOAT3( 0.f, 0.f, 0.f ) );
    if ( !mesh.m_Sections.empty() )
    {
        box = CalculateMeshSectionBoundingBox( mesh.m_Sections.front() );
        for ( size_t i = 1; i < mesh.m_Sections.size(); ++i )
        {
            BoundingBox::CreateMerged( box, box, CalculateMeshSectionBoundingBox( mesh.m_Sections[ i ] ) );
        }
    }
    return box;
}

BoundingBox CScene::CalculateMeshNodeBoundingBox( const SSceneNode& node ) const
{
    BoundingBox box( XMFLOAT3( 0.f, 0.f, 0.f ), XMFLOAT3( 0.f, 0.f, 0.f ) );
    if ( node.m_Mesh != -1 )
    {
        XMMATRIX worldMatrix = CalculateNodeWorldTransform( node );
        box = CalculateMeshBoundingBox( m_Meshes[ node.m_Mesh ] );
        box.Transform( box, worldMatrix );
    }
    return box;
}

BoundingSphere CScene::CalculateBoundingSphere() const
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    if ( !m_MeshNodes.empty() )
    { 
        BoundingBox box = CalculateMeshNodeBoundingBox( m_Nodes[ m_MeshNodes.front() ] );
        for ( size_t i = 1; i < m_MeshNodes.size(); ++i )
        {
            const SSceneNode& node = m_Nodes[ m_MeshNodes[ i ] ];
            BoundingBox::CreateMerged( box, box, CalculateMeshNodeBoundingBox( node ) );
        }
        BoundingSphere::CreateFromBoundingBox( sphere, box );
    }
    return sphere;
}

XMMATRIX __vectorcall CScene::CalculateNodeWorldTransform( const SSceneNode& node ) const
{
    const SSceneNode* currentNode = &node;
    XMMATRIX worldMatrix = XMLoadFloat4x3( &currentNode->m_LocalTransform );
    while ( currentNode->m_Parent != -1 )
    {
        currentNode = &m_Nodes[ currentNode->m_Parent ];
        XMMATRIX parentMatrix = XMLoadFloat4x3( &currentNode->m_LocalTransform );
        worldMatrix = XMMatrixMultiply( worldMatrix, parentMatrix );
    }
    return worldMatrix;
}
