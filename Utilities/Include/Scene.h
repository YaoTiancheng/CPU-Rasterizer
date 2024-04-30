#pragma once

#include <vector>
#include <DirectXMath.h>
#include <DirectXCollision.h>

struct SSceneNode
{
    int32_t m_Mesh = -1;
    int32_t m_Parent = -1;
    DirectX::XMFLOAT4X3 m_LocalTransform;
};

struct SSceneMaterial
{
    int32_t m_DiffuseTexture = -1;
    DirectX::XMFLOAT4 m_Diffuse;
    DirectX::XMFLOAT3 m_Specular;
    float m_Power = 0.f;
    float m_AlphaThreshold = 0.f;
    bool m_TwoSided = false;
    bool m_AlphaTest = false;
    bool m_AlphaBlend = false;
};

struct SSceneBuffer
{
    uint8_t* m_Data = nullptr;
};

struct SSceneStream
{
    int32_t m_Buffer = -1;
    uint32_t m_ByteOffset = 0;
    uint32_t m_ByteSize = 0;
    uint32_t m_ByteStride = 0;
};

struct SSceneMeshSection
{
    SSceneStream m_PositionStream;
    SSceneStream m_NormalStream;
    SSceneStream m_ColorStream;
    SSceneStream m_TexcoordsTream;
    SSceneStream m_IndexStream;
    bool m_Is32bitIndex = false;
    uint32_t m_PrimitivesCount = 0;
    int32_t m_Material = -1;
};

struct SSceneMesh
{
    std::vector<SSceneMeshSection> m_Sections;
};

struct SSceneImage
{
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint8_t* m_Data = nullptr;
};

class CScene
{
public:
    void FlipCoordinateHandness();

    void FreeAll();

    DirectX::BoundingBox CalculateMeshSectionBoundingBox( const SSceneMeshSection& section ) const;

    DirectX::BoundingBox CalculateMeshBoundingBox( const SSceneMesh& mesh ) const;

    DirectX::BoundingBox CalculateMeshNodeBoundingBox( const SSceneNode& node ) const;

    DirectX::BoundingSphere CalculateBoundingSphere() const;

    DirectX::XMMATRIX __vectorcall CalculateNodeWorldTransform( const SSceneNode& node ) const;

    std::vector<SSceneNode> m_Nodes;
    std::vector<SSceneMesh> m_Meshes;
    std::vector<SSceneMaterial> m_Materials;
    std::vector<SSceneBuffer> m_Buffers;
    std::vector<SSceneImage> m_Images;

    std::vector<int32_t> m_MeshNodes;
};