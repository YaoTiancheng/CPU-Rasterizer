#include "UtilitiesPCH.h"
#include "MeshLoader.h"
#include "Mesh.h"
#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobjloader/tiny_obj_loader.h"

inline void hash_combine( std::size_t& seed ) { }

template <typename T, typename... Rest>
inline void hash_combine( std::size_t& seed, const T& v, Rest... rest )
{
    std::hash<T> hasher;
    seed ^= hasher( v ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 );
    hash_combine( seed, rest... );
}

namespace std
{
    template<> struct hash<tinyobj::index_t>
    {
        std::size_t operator()( tinyobj::index_t const& s ) const noexcept
        {
            size_t h = 0;
            hash_combine( h, s.vertex_index, s.normal_index, s.texcoord_index );
            return h;
        }
    };

    template<> struct equal_to<tinyobj::index_t>
    {
        bool operator()( const tinyobj::index_t& lhs, const tinyobj::index_t& rhs ) const
        {
            return lhs.vertex_index == rhs.vertex_index
                && lhs.normal_index == rhs.normal_index
                && lhs.texcoord_index == rhs.texcoord_index;
        }
    };
}

bool LoadMeshFromObjFile( const std::filesystem::path& filename, CMesh* mesh, uint32_t allowedVertexAttributes )
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    const std::string filenameString = filename.u8string();
    const std::string materialBaseDir = filename.parent_path().u8string();
    if ( !tinyobj::LoadObj( &attrib, &shapes, &materials, &err, filenameString.c_str(), materialBaseDir.c_str(), true ) )
    {
        std::cerr << err.c_str();
        return false;
    }

    std::unordered_set<tinyobj::index_t> uniqueIndexSet;
    std::vector<int> materialIds;

    constexpr uint32_t maxVerticesCount = std::numeric_limits<uint32_t>::max();
    constexpr uint32_t maxIndicesCount = std::numeric_limits<uint32_t>::max();

    // Loop over all vertices to generate a set of unique indices. Meanwhile fill the materialId array
    for ( size_t s = 0; s < shapes.size(); ++s )
    {
        const tinyobj::shape_t& shape = shapes[ s ];

        if ( maxIndicesCount - materialIds.size() * 3 < shape.mesh.num_face_vertices.size() * 3 )
        {
            // Too many indices
            return false;
        }

        uint32_t index_offset = 0;
        materialIds.reserve( materialIds.size() + shape.mesh.num_face_vertices.size() );
        for ( size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f )
        {
            uint32_t fv = shape.mesh.num_face_vertices[ f ];
            assert( fv == 3 ); // Hitting this means triangulation failed

            for ( uint32_t v = 0; v < 3; ++v ) 
            {
                tinyobj::index_t idx = shape.mesh.indices[ index_offset + v ];
                uniqueIndexSet.insert( idx );

                if ( uniqueIndexSet.size() > maxVerticesCount )
                {
                    // Too many unique vertices
                    return false;
                }
            }
            index_offset += 3;

            materialIds.push_back( shape.mesh.material_ids[ f ] );
        }
    }

    const bool hasNormal = !attrib.normals.empty() && ( allowedVertexAttributes & CMesh::EVertexFormat::eNormal ) != 0;
    const bool hasTexcoord = !attrib.texcoords.empty() && ( allowedVertexAttributes & CMesh::EVertexFormat::eTexcoord ) != 0;

    uint32_t vertexFormat = CMesh::EVertexFormat::ePosition;
    vertexFormat |= hasNormal ? CMesh::EVertexFormat::eNormal : 0;
    vertexFormat |= hasTexcoord ? CMesh::EVertexFormat::eTexcoord : 0;

    uint32_t verticesCount = (uint32_t)uniqueIndexSet.size();
    if ( !mesh->AllocateVertices( vertexFormat, verticesCount ) )
    {
        return false;
    }

    // Assemble final vertices from the unique index set
    std::unordered_map<tinyobj::index_t, uint32_t> tinyObjIndexToMeshIndex; // This is used to remap the tinyobj index to the final vertices later
    float* position = mesh->GetPosition( 0 );
    float* normal = mesh->GetNormal( 0 );
    float* texcoords = mesh->GetTexcoord( 0 );
    for ( auto iter : uniqueIndexSet )
    {
        const tinyobj::index_t tinyObjIndex = iter;
        uint32_t meshIndex = (uint32_t)tinyObjIndexToMeshIndex.size();

        tinyObjIndexToMeshIndex.insert( std::make_pair( tinyObjIndex, meshIndex ) );

        // Read positions
        {
            tinyobj::real_t vx = 0, vy = 0, vz = 0;
            if ( tinyObjIndex.vertex_index >= 0 )
            { 
                vx = attrib.vertices[ 3 * tinyObjIndex.vertex_index + 0 ];
                vy = attrib.vertices[ 3 * tinyObjIndex.vertex_index + 1 ];
                vz = attrib.vertices[ 3 * tinyObjIndex.vertex_index + 2 ];
            }
            position[ 0 ] = (float)vx;
            position[ 1 ] = (float)vy;
            position[ 2 ] = (float)vz;
            position = (float*)( (uint8_t*)position + mesh->GetVertexSize() );
        }

        // Read normals
        if ( hasNormal )
        {
            tinyobj::real_t nx = 0, ny = 0, nz = 0;
            if ( tinyObjIndex.normal_index >= 0 )
            { 
                nx = attrib.normals[ 3 * tinyObjIndex.normal_index + 0 ];
                ny = attrib.normals[ 3 * tinyObjIndex.normal_index + 1 ];
                nz = attrib.normals[ 3 * tinyObjIndex.normal_index + 2 ];
            }
            normal[ 0 ] = (float)nx;
            normal[ 1 ] = (float)ny;
            normal[ 2 ] = (float)nz;
            normal = (float*)( (uint8_t*)normal + mesh->GetVertexSize() );
        }

        // Read texcoords
        if ( hasTexcoord )
        {
            tinyobj::real_t tx = 0, ty = 0;
            if ( tinyObjIndex.texcoord_index >= 0 )
            {
                tx = attrib.texcoords[ 2 * tinyObjIndex.texcoord_index + 0 ];
                ty = attrib.texcoords[ 2 * tinyObjIndex.texcoord_index + 1 ];
            }
            texcoords[ 0 ] = (float)tx;
            texcoords[ 1 ] = (float)ty;
            texcoords = (float*)( (uint8_t*)texcoords + mesh->GetVertexSize() );
        }
    }

    const uint32_t indicesCount = (uint32_t)( materialIds.size() * 3 );
    mesh->AllocateIndices( indicesCount );

    // Generate indices
    {
        uint32_t* indices = mesh->GetIndices();
        for ( size_t s = 0; s < shapes.size(); ++s )
        {
            const tinyobj::shape_t& shape = shapes[ s ];
            uint32_t index_offset = 0;
            for ( size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f )
            {
                for ( uint32_t v = 0; v < 3; ++v ) 
                {
                    tinyobj::index_t idx = shape.mesh.indices[ index_offset + v ];
                    auto meshIndexIter = tinyObjIndexToMeshIndex.find( idx );
                    assert( meshIndexIter != tinyObjIndexToMeshIndex.end() );
                    *indices = meshIndexIter->second;
                    ++indices;
                }
                index_offset += 3;
            }
        }
    }

    return true;
}