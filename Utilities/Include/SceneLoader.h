#pragma once

#include <filesystem>

class CScene;

bool LoadSceneFronGLTFFile( const std::filesystem::path& filename, CScene* scene );