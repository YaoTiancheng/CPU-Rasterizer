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

void CScene::CalculateNodeWorldTransforms( std::vector<XMFLOAT4X3>* transforms )
{
    transforms->reserve( transforms->size() + m_Nodes.size() );
    for ( const SSceneNode& node : m_Nodes )
    {
        XMMATRIX worldMatrix = CalculateNodeWorldTransform( node );
        transforms->emplace_back();
        XMFLOAT4X3& matrix = transforms->back();
        XMStoreFloat4x3( &matrix, worldMatrix );
    }
}

void CScene::CalculateMeshSectionBoundingBoxes( std::vector<BoundingBox>* boxes )
{
    for ( int32_t meshNodeIndex : m_MeshNodes )
    {
        const SSceneNode& node = m_Nodes[ meshNodeIndex ];
        const SSceneMesh& mesh = m_Meshes[ node.m_Mesh ];
        boxes->reserve( boxes->size() + mesh.m_Sections.size() );
        for ( const SSceneMeshSection& section : mesh.m_Sections )
        {
            boxes->emplace_back();
            BoundingBox& box = boxes->back();
            box = CalculateMeshSectionBoundingBox( section );
        }
    }
}

void CScene::TransformMeshSectionBoundingBoxes( const std::vector<XMFLOAT4X3>& transforms, std::vector<BoundingBox>& boxes )
{
    uint32_t sectionIndex = 0;
    for ( int32_t meshNodeIndex : m_MeshNodes )
    {
        const SSceneNode& node = m_Nodes[ meshNodeIndex ];
        const SSceneMesh& mesh = m_Meshes[ node.m_Mesh ];
        for ( size_t section = 0; section < mesh.m_Sections.size(); ++section )
        {
            BoundingBox& box = boxes[ sectionIndex ];
            const XMMATRIX matrix = XMLoadFloat4x3( &transforms[ meshNodeIndex ] );
            box.Transform( box, matrix );
            sectionIndex++;
        }
    }
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
