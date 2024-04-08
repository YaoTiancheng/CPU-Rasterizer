#pragma once

#include <filesystem>

class CMesh;

bool LoadMeshFromObjFile( const std::filesystem::path& filename, CMesh* mesh, uint32_t allowedVertexAttributes = 0xFFFFFFFF );
