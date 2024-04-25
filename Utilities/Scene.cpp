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

BoundingSphere CScene::CalculateMeshSectionBoundingSphere( const SSceneMeshSection& section ) const
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    if ( section.m_PositionStream.m_Buffer != -1 )
    { 
        const SSceneBuffer& buffer = m_Buffers[ section.m_PositionStream.m_Buffer ];
        BoundingSphere::CreateFromPoints( sphere, section.m_PositionStream.m_ByteSize / section.m_PositionStream.m_ByteStride,
            (const XMFLOAT3*)( buffer.m_Data + section.m_PositionStream.m_ByteOffset ), section.m_PositionStream.m_ByteStride );
    }
    return sphere;
}

BoundingSphere CScene::CalculateMeshBoundingSphere( const SSceneMesh& mesh ) const
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    for ( const SSceneMeshSection& section : mesh.m_Sections )
    {
        BoundingSphere::CreateMerged( sphere, sphere, CalculateMeshSectionBoundingSphere( section ) );
    }
    return sphere;
}

BoundingSphere CScene::CalculateMeshNodeBoundingSphere( const SSceneNode& node ) const
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    if ( node.m_Mesh != -1 )
    {
        XMMATRIX worldMatrix = CalculateNodeWorldTransform( node );
        sphere = CalculateMeshBoundingSphere( m_Meshes[ node.m_Mesh ] );
        sphere.Transform( sphere, worldMatrix );
    }
    return sphere;
}

BoundingSphere CScene::CalculateBoundingSphere() const
{
    BoundingSphere sphere( XMFLOAT3( 0.f, 0.f, 0.f ), 0.f );
    for ( int32_t meshNodeIndex : m_MeshNodes )
    {
        const SSceneNode& node = m_Nodes[ meshNodeIndex ];
        BoundingSphere::CreateMerged( sphere, sphere, CalculateMeshNodeBoundingSphere( node ) );
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
