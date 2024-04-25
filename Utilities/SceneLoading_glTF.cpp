#include "UtilitiesPCH.h"
#include "SceneLoader.h"
#include "Scene.h"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma warning( disable : 4018 )
#pragma warning( disable : 4267 )
#include "tinygltf/tiny_gltf.h"
#pragma warning( default : 4018 )
#pragma warning( default : 4267 )

using namespace DirectX;
using namespace Microsoft::WRL;

static ComPtr<IWICImagingFactory> s_WICFactory;

bool DecodeImageFromMemory( const uint8_t* data, uint32_t byteSize, SSceneImage* image )
{
    if ( !s_WICFactory )
    {
        if ( FAILED( CoCreateInstance( CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (LPVOID*)s_WICFactory.GetAddressOf() ) ) )
        {
            return false;
        }
    }

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> convertedFrame;

    if ( FAILED( s_WICFactory->CreateStream( stream.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( stream->InitializeFromMemory( (WICInProcPointer)data, byteSize ) ) )
    {
        return false;
    }

    if ( FAILED( s_WICFactory->CreateDecoderFromStream( stream.Get(), NULL, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( decoder->GetFrame( 0, frame.GetAddressOf() ) ) )
    {
        return false;
    }

    uint32_t width = 0, height = 0;
    if ( FAILED( frame->GetSize( &width, &height ) ) )
    {
        return false;
    }
    
    if ( FAILED( s_WICFactory->CreateFormatConverter( convertedFrame.GetAddressOf() ) ) )
    {
        return false;
    }

    if ( FAILED( convertedFrame->Initialize( frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom ) ) )
    {
        return false;
    }

    image->m_Width = width;
    image->m_Height = height;
    image->m_Data = (uint8_t*)malloc( width * height * 4 );
    return SUCCEEDED( convertedFrame->CopyPixels( nullptr, width * 4, width * height * 4, (BYTE*)image->m_Data ) );
}

static void GetStream( const tinygltf::Model& model, const tinygltf::Accessor& accessor, SSceneStream* stream, uint32_t defaultStride )
{
    const tinygltf::BufferView& bufferView = model.bufferViews[ accessor.bufferView ];
    stream->m_Buffer = bufferView.buffer;
    stream->m_ByteOffset = (uint32_t)bufferView.byteOffset;
    stream->m_ByteSize = (uint32_t)bufferView.byteLength;
    stream->m_ByteStride = bufferView.byteStride != 0 ? (uint32_t)bufferView.byteStride : defaultStride;
}

template <typename T>
inline T TranslateValue( const tinygltf::Value& value )
{
    return T();
}

template <>
inline XMFLOAT4 TranslateValue<XMFLOAT4>( const tinygltf::Value& number4 )
{
    XMFLOAT4 float4;
    float4.x = (float)number4.Get( 0 ).Get<double>();
    float4.y = (float)number4.Get( 1 ).Get<double>();
    float4.z = (float)number4.Get( 2 ).Get<double>();
    float4.w = (float)number4.Get( 3 ).Get<double>();
    return float4;
}

template <>
inline XMFLOAT3 TranslateValue<XMFLOAT3>( const tinygltf::Value& number3 )
{
    XMFLOAT3 float3;
    float3.x = (float)number3.Get( 0 ).Get<double>();
    float3.y = (float)number3.Get( 1 ).Get<double>();
    float3.z = (float)number3.Get( 2 ).Get<double>();
    return float3;
}

template <>
inline float TranslateValue<float>( const tinygltf::Value& number )
{
    return (float)number.Get<double>();
}

template <>
inline int TranslateValue<int>( const tinygltf::Value& number )
{
    return number.Get<int>();
}

template <typename T>
T TranslateObjectProperty( const tinygltf::Value::Object&, const char*, T );

template <>
inline tinygltf::TextureInfo TranslateValue<tinygltf::TextureInfo>( const tinygltf::Value& value )
{
    tinygltf::TextureInfo textureInfo;
    const tinygltf::Value::Object& object = value.Get<tinygltf::Value::Object>();
    textureInfo.index = TranslateObjectProperty<int>( object, "index", -1 );
    return textureInfo;
}

template <typename T>
T TranslateObjectProperty( const tinygltf::Value::Object& object, const char* name, T defaultValue )
{
    T result = defaultValue;
    auto it = object.find( name ) ;
    if ( it != object.end() )
    {
        const tinygltf::Value& value = it->second;
        result = TranslateValue<T>( value );
    }
    return result;
}

bool LoadSceneFronGLTFFile( const std::filesystem::path& filename, CScene* scene )
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    if ( !loader.LoadBinaryFromFile( &model, &err, &warn, filename.u8string() ) )
    {
        OutputDebugStringA( err.c_str() );
        OutputDebugStringA( "\n" );
        OutputDebugStringA( warn.c_str() );
        return false;
    }

    // Load buffers
    assert( scene->m_Buffers.empty() );
    scene->m_Buffers.reserve( model.buffers.size() );
    for ( size_t bufferIndex = 0; bufferIndex < model.buffers.size(); ++bufferIndex )
    {
        scene->m_Buffers.emplace_back();
        SSceneBuffer& newBuffer = scene->m_Buffers.back();
        const tinygltf::Buffer& srcBuffer = model.buffers[ bufferIndex ];

        newBuffer.m_Data = (uint8_t*)malloc( srcBuffer.data.size() );
        memcpy( newBuffer.m_Data, srcBuffer.data.data(), srcBuffer.data.size() );
    }

    // Load meshes
    assert( scene->m_Meshes.empty() );
    scene->m_Meshes.reserve( model.meshes.size() );
    for ( size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex )
    {
        scene->m_Meshes.emplace_back();
        SSceneMesh& newMesh = scene->m_Meshes.back();
        const tinygltf::Mesh& srcMesh = model.meshes[ meshIndex ];
        
        newMesh.m_Sections.reserve( srcMesh.primitives.size() );
        for ( size_t sectionIndex = 0; sectionIndex < srcMesh.primitives.size(); ++sectionIndex )
        {
            const tinygltf::Primitive& primitive = srcMesh.primitives[ sectionIndex ];

            if ( primitive.mode != TINYGLTF_MODE_TRIANGLES )
            {
                continue;
            }

            newMesh.m_Sections.emplace_back();
            SSceneMeshSection& newSection = newMesh.m_Sections.back();

            const std::map<std::string, int>& attributes = primitive.attributes;

            auto iter = attributes.find( "POSITION" );
            if ( iter != attributes.end() )
            {
                const int32_t accessorIndex = iter->second;
                const tinygltf::Accessor& accessor = model.accessors[ accessorIndex ];
                if ( accessor.type == TINYGLTF_TYPE_VEC3 && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT )
                { 
                    GetStream( model, accessor, &newSection.m_PositionStream, 12 );
                }
            }

            iter = attributes.find( "NORMAL" );
            if ( iter != attributes.end() )
            {
                const int32_t accessorIndex = iter->second;
                const tinygltf::Accessor& accessor = model.accessors[ accessorIndex ];
                if ( accessor.type == TINYGLTF_TYPE_VEC3 && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT )
                { 
                    GetStream( model, accessor, &newSection.m_NormalStream, 12 );
                }
            }

            iter = attributes.find( "TEXCOORD_0" );
            if ( iter != attributes.end() )
            {
                const int32_t accessorIndex = iter->second;
                const tinygltf::Accessor& accessor = model.accessors[ accessorIndex ];
                if ( accessor.type == TINYGLTF_TYPE_VEC2 && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT )
                {
                    GetStream( model, accessor, &newSection.m_TexcoordsTream, 8 );
                }
            }

            iter = attributes.find( "COLOR_0" );
            if ( iter != attributes.end() )
            {
                const int32_t accessorIndex = iter->second;
                const tinygltf::Accessor& accessor = model.accessors[ accessorIndex ];
                const bool isVec3 = accessor.type == TINYGLTF_TYPE_VEC3;
                const bool isVec4 = accessor.type == TINYGLTF_TYPE_VEC4;
                if ( ( isVec3 || isVec4 ) && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT )
                {
                    GetStream( model, accessor, &newSection.m_ColorStream, isVec3 ? 12 : 16 );
                }
            }

            {
                const int32_t accessorIndex = primitive.indices;
                if ( accessorIndex != -1 )
                { 
                    const tinygltf::Accessor& accessor = model.accessors[ accessorIndex ];
                    const bool is32BitIndex = accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_INT;
                    const bool is16BitIndex = accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT || accessor.componentType == TINYGLTF_COMPONENT_TYPE_SHORT;
                    if ( accessor.type == TINYGLTF_TYPE_SCALAR && ( is32BitIndex || is16BitIndex ) )
                    {
                        const uint32_t defaultByteStride = is32BitIndex ? 4 : 2;
                        GetStream( model, accessor, &newSection.m_IndexStream, defaultByteStride );
                        newSection.m_Is32bitIndex = is32BitIndex;
                        newSection.m_PrimitivesCount = ( newSection.m_IndexStream.m_ByteSize / newSection.m_IndexStream.m_ByteStride ) / 3;
                    }
                }
                else
                {
                    newSection.m_PrimitivesCount = newSection.m_PositionStream.m_ByteSize ? 
                        ( newSection.m_PositionStream.m_ByteSize / newSection.m_PositionStream.m_ByteStride ) / 3 : 0;
                }
            }

            newSection.m_Material = primitive.material;
        }
    }

    // Load materials
    assert( scene->m_Materials.empty() );
    scene->m_Materials.reserve( model.materials.size() );
    for ( size_t materialIndex = 0; materialIndex < model.materials.size(); ++materialIndex )
    {
        scene->m_Materials.emplace_back();
        SSceneMaterial& newMaterial = scene->m_Materials.back();
        const tinygltf::Material& srcMaterial = model.materials[ materialIndex ];

        // "If the specular-glossiness extension is included in an asset, then any client implementation that supports the extension
        // should always render the asset using the specular-glossiness material properties." 
        // -- quote from https://kcoley.github.io/glTF/extensions/2.0/Khronos/KHR_materials_pbrSpecularGlossiness/
        auto iter = srcMaterial.extensions.find( "KHR_materials_pbrSpecularGlossiness" );
        if ( iter != srcMaterial.extensions.end() )
        {
            const tinygltf::Value::Object& specularGlossy = iter->second.Get<tinygltf::Value::Object>();
            newMaterial.m_Diffuse = TranslateObjectProperty<XMFLOAT4>( specularGlossy, "diffuseFactor", XMFLOAT4( 1.f, 1.f, 1.f, 1.f ) );
            newMaterial.m_Specular = TranslateObjectProperty<XMFLOAT3>( specularGlossy, "specularFactor", XMFLOAT3( 1.f, 1.f, 1.f ) );
            const tinygltf::TextureInfo diffuseTextureInfo = TranslateObjectProperty<tinygltf::TextureInfo>( specularGlossy, "diffuseTexture", tinygltf::TextureInfo() );
            newMaterial.m_DiffuseTexture = diffuseTextureInfo.index;
        }
        else
        { 
            const std::vector<double>& baseColorFactor = srcMaterial.pbrMetallicRoughness.baseColorFactor;
            newMaterial.m_Diffuse = XMFLOAT4( (float)baseColorFactor[ 3 ], (float)baseColorFactor[ 2 ], (float)baseColorFactor[ 1 ], (float)baseColorFactor[ 0 ] );
            newMaterial.m_Specular = XMFLOAT3( 0.f, 0.f, 0.f );
            newMaterial.m_Power = 1.f;
            if ( srcMaterial.pbrMetallicRoughness.baseColorTexture.index != -1 )
            { 
                const tinygltf::Texture& texture = model.textures[ srcMaterial.pbrMetallicRoughness.baseColorTexture.index ];
                newMaterial.m_DiffuseTexture = texture.source;
            }
        }

        newMaterial.m_AlphaTest = strcmp( srcMaterial.alphaMode.c_str(), "MASK" ) == 0;
        newMaterial.m_AlphaThreshold = (float)srcMaterial.alphaCutoff;
        newMaterial.m_TwoSided = srcMaterial.doubleSided;
    }

    // Load images
    assert( scene->m_Images.empty() );
    scene->m_Images.reserve( model.images.size() );
    for ( size_t imageIndex = 0; imageIndex < model.images.size(); ++imageIndex )
    {
        scene->m_Images.emplace_back();
        SSceneImage& newImage = scene->m_Images.back();
        const tinygltf::Image& srcImage = model.images[ imageIndex ];

        if ( srcImage.bufferView != -1 )
        { 
            const tinygltf::BufferView& bufferView = model.bufferViews[ srcImage.bufferView ];
            const tinygltf::Buffer& buffer = model.buffers[ bufferView.buffer ];
            DecodeImageFromMemory( buffer.data.data() + bufferView.byteOffset, (uint32_t)bufferView.byteLength, &newImage );
        }
    }

    // Load all nodes
    assert( scene->m_Nodes.empty() );
    scene->m_Nodes.reserve( model.nodes.size() );
    for ( size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex )
    {
        scene->m_Nodes.emplace_back();
        SSceneNode& newNode = scene->m_Nodes.back();
        const tinygltf::Node& srcNode = model.nodes[ nodeIndex ];

        if ( !srcNode.matrix.empty() )
        {
        }
        else
        {
            XMVECTOR translation = !srcNode.translation.empty() ?
                XMVectorSet( (float)srcNode.translation[ 0 ], (float)srcNode.translation[ 1 ], (float)srcNode.translation[ 2 ], 1.f ) 
                : g_XMZero;
            XMVECTOR rotation = !srcNode.rotation.empty() ?
                XMVectorSet( (float)srcNode.rotation[ 0 ], (float)srcNode.rotation[ 1 ], (float)srcNode.rotation[ 2 ], (float)srcNode.rotation[ 3 ] )
                : XMQuaternionIdentity();
            XMVECTOR scale = !srcNode.scale.empty() ?
                XMVectorSet( (float)srcNode.scale[ 0 ], (float)srcNode.scale[ 1 ], (float)srcNode.scale[ 2 ], 0.f )
                : g_XMOne;
            XMMATRIX matrix = XMMatrixAffineTransformation( scale, g_XMZero, rotation, translation );
            XMStoreFloat4x3( &newNode.m_LocalTransform, matrix );
        }

        if ( srcNode.mesh != -1 )
        {
            newNode.m_Mesh = srcNode.mesh;
            scene->m_MeshNodes.emplace_back( (int32_t)nodeIndex );
        }
    }

    // Assign parent index to the nodes
    for ( int32_t nodeIndex = 0; nodeIndex < (int32_t)scene->m_Nodes.size(); ++nodeIndex )
    {
        const tinygltf::Node& parentNode = model.nodes[ nodeIndex ];
        for ( int32_t child : parentNode.children )
        {
            scene->m_Nodes[ child ].m_Parent = nodeIndex;
        }
    }

    return true;
}