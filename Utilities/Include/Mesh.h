#pragma once

#include <cstdint>
#include <string>
#include <DirectXCollision.h>

class CImage;

struct SMeshSection
{
    uint32_t m_IndexLocation;
    uint32_t m_IndicesCount;
};

struct SMeshMaterial
{
    static SMeshMaterial GetDefault();

    std::string m_DiffuseTextureName;
    CImage* m_DiffuseTexture;
    float m_Diffuse[ 4 ];
    float m_Specular[ 3 ];
    float m_Power;
};

class CMesh
{
public:
    enum EVertexFormat : uint32_t
    {
        ePosition = 0x1,
        eNormal = 0x2,
        eColor = 0x4,
        eTexcoord = 0x8
    };

    CMesh() 
        : m_Vertices( nullptr )
        , m_Indices( nullptr )
        , m_Sections( nullptr )
        , m_Materials( nullptr )
        , m_VertexFormat( 0 )
        , m_NormalOffset( 0 )
        , m_ColorOffset( 0 )
        , m_TexcoordOffset( 0 )
        , m_VertexSize( 0 )
        , m_VerticesCount( 0 )
        , m_IndicesCount( 0 )
        , m_SectionsCount( 0 )
    {}

    bool AllocateVertices( uint32_t vertexFormat, uint32_t verticesCount );

    void FreeVertices();

    void AllocateIndices( uint32_t indicesCount );

    void FreeIndices();

    void AllocateSections( uint32_t sectionsCount );

    void FreeSections();

    void AllocateMaterials();

    void FreeMaterials();

    void FreeAll();

    uint8_t* GetVertices() const { return m_Vertices; }

    uint32_t* GetIndices() const { return m_Indices; }

    SMeshSection* GetSections() const { return m_Sections; }

    SMeshMaterial* GetMaterials() const { return m_Materials; }

    float* GetPosition( uint32_t index ) const { return (float*)( m_Vertices + index * m_VertexSize ); }

    float* GetNormal( uint32_t index ) const { return (float*)( m_Vertices + m_NormalOffset + index * m_VertexSize ); }

    float* GetColor( uint32_t index ) const { return (float*)( m_Vertices + m_ColorOffset + index * m_VertexSize ); }

    float* GetTexcoord( uint32_t index ) const { return (float*)( m_Vertices + m_TexcoordOffset + index * m_VertexSize ); }

    uint32_t GetNormalOffset() const { return m_NormalOffset; }

    uint32_t GetColorOffset() const { return m_ColorOffset; }

    uint32_t GetTexcoordOffset() const { return m_TexcoordOffset; }

    uint32_t GetVertexSize() const { return m_VertexSize; }

    uint32_t GetVerticesCount() const { return m_VerticesCount; }

    uint32_t GetIndicesCount() const { return m_IndicesCount; }

    uint32_t GetSectionsCount() const { return m_SectionsCount; }

    uint32_t GetVertexFormat() const { return m_VertexFormat; }

    void FlipCoordinateHandness();

    void FlipTexcoordsV();

    DirectX::BoundingBox ComputeBoundingBox() const;

    static uint32_t ComputeVertexLayout( uint32_t vertexFormat, uint32_t* positionOffset, uint32_t* normalOffset, uint32_t* colorOffset, uint32_t* texcoordOffset );

private:
    uint8_t* m_Vertices;
    uint32_t* m_Indices;
    SMeshSection* m_Sections;
    SMeshMaterial* m_Materials;

    uint32_t m_VertexFormat;
    uint32_t m_NormalOffset;
    uint32_t m_ColorOffset;
    uint32_t m_TexcoordOffset;
    uint32_t m_VertexSize;

    uint32_t m_VerticesCount;
    uint32_t m_IndicesCount;
    uint32_t m_SectionsCount;
};