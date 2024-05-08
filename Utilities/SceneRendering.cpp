#include "UtilitiesPCH.h"
#include "SceneRendering.h"
#include "Scene.h"

using namespace DirectX;

inline static Rasterizer::SStream TranslateSceneStream( const CScene& scene, const SSceneStream& stream )
{
    Rasterizer::SStream out;
    if ( stream.m_Buffer != -1 )
    { 
        out.m_Data = scene.m_Buffers[ stream.m_Buffer ].m_Data;
        out.m_Offset = stream.m_ByteOffset;
        out.m_Stride = stream.m_ByteStride;
        out.m_Size = stream.m_ByteSize;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

inline static Rasterizer::SMaterial TranslateSceneMaterial( const CScene& scene, int32_t materialIndex )
{
    Rasterizer::SMaterial out;
    if ( materialIndex != -1 )
    { 
        const SSceneMaterial& material = scene.m_Materials[ materialIndex ];
        out.m_Diffuse = Rasterizer::SVector4( (float*)&material.m_Diffuse );
        out.m_Specular = Rasterizer::SVector3( (float*)&material.m_Specular );
        out.m_Power = material.m_Power;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

inline static Rasterizer::SImage TranslateSceneImage( const CScene& scene, int32_t imageIndex )
{
    Rasterizer::SImage out;
    if ( imageIndex != -1 )
    { 
        const SSceneImage& image = scene.m_Images[ imageIndex ];
        out.m_Bits = image.m_Data;
        out.m_Width = image.m_Width;
        out.m_Height = image.m_Height;
    }
    else
    {
        ZeroMemory( &out, sizeof( out ) );
    }
    return out;
}

void GenerateMeshDrawCommands( const CScene& scene, const std::vector<XMFLOAT4X3>& nodeWorldTransforms, const std::vector<DirectX::BoundingBox>* meshSectionBoundingBoxes, std::vector<SMeshDrawCommand>* commands )
{
    uint32_t sectionIndex = 0;
    for ( int32_t meshNodeIndex : scene.m_MeshNodes )
    {
        const SSceneNode& meshNode = scene.m_Nodes[ meshNodeIndex ];
        const XMFLOAT4X3& worldTransform = nodeWorldTransforms[ meshNodeIndex ];
        const SSceneMesh& mesh = scene.m_Meshes[ meshNode.m_Mesh ];
        commands->reserve( commands->size() + mesh.m_Sections.size() );
        for ( const SSceneMeshSection& section : mesh.m_Sections )
        {
            commands->emplace_back();
            SMeshDrawCommand& newCommand = commands->back();
            newCommand.m_WorldMatrix = worldTransform;
            newCommand.m_BoundingBox = meshSectionBoundingBoxes ? (*meshSectionBoundingBoxes)[ sectionIndex ] : BoundingBox();
            newCommand.m_PositionStream = TranslateSceneStream( scene, section.m_PositionStream );
            newCommand.m_NormalStream = TranslateSceneStream( scene, section.m_NormalStream );
            newCommand.m_TexcoordsStream = TranslateSceneStream( scene, section.m_TexcoordsTream );
            newCommand.m_ColorStream = TranslateSceneStream( scene, section.m_ColorStream );
            newCommand.m_IndexStream = TranslateSceneStream( scene, section.m_IndexStream );
            newCommand.m_IndexType = section.m_Is32bitIndex ? Rasterizer::EIndexType::e32bit : Rasterizer::EIndexType::e16bit;
            newCommand.m_PrimitiveCount = section.m_PrimitivesCount;
            newCommand.m_Material = TranslateSceneMaterial( scene, section.m_Material );
            if ( section.m_Material != -1 )
            { 
                const SSceneMaterial& material = scene.m_Materials[ section.m_Material ];
                newCommand.m_DiffuseTexture = TranslateSceneImage( scene, material.m_DiffuseTexture );
                newCommand.m_AlphaRef = (uint8_t)( material.m_AlphaThreshold * 255.f + 0.5f );
                newCommand.m_AlphaTest = material.m_AlphaTest;
                newCommand.m_AlphaBlend = material.m_AlphaBlend;
                newCommand.m_TwoSided = material.m_TwoSided;
            }
            else
            {
                newCommand.m_DiffuseTexture = TranslateSceneImage( scene, -1 );
                newCommand.m_AlphaRef = 0;
                newCommand.m_AlphaTest = false;
                newCommand.m_AlphaBlend = false;
                newCommand.m_TwoSided = false;
            }
            ++sectionIndex;
        }
    }
}