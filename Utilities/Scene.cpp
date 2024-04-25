#include "UtilitiesPCH.h"
#include "Scene.h"

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