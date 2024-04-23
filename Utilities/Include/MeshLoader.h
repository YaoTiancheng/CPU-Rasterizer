#pragma once

#include <vector>
#include <filesystem>

class CMesh;
class CImage;

bool LoadMeshFromObjFile( const std::filesystem::path& filename, CMesh* mesh, std::vector<CImage>* images, uint32_t allowedVertexAttributes = 0xFFFFFFFF );