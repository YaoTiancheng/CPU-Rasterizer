#pragma once

#include <vector>
#include <DirectXMath.h>
#include "Rasterizer.h"

class CScene;

struct SMeshDrawCommand
{
    DirectX::XMFLOAT4X3 m_WorldMatrix;
    Rasterizer::SStream m_PositionStream;
    Rasterizer::SStream m_NormalStream;
    Rasterizer::SStream m_TexcoordsStream;
    Rasterizer::SStream m_ColorStream;
    Rasterizer::SStream m_IndexStream;
    Rasterizer::EIndexType m_IndexType;
    uint32_t m_PrimitiveCount;
    Rasterizer::SMaterial m_Material;
    Rasterizer::SImage m_DiffuseTexture;
    uint8_t m_AlphaRef;
    bool m_AlphaTest;
    bool m_AlphaBlend;
    bool m_TwoSided;
};

void GenerateMeshDrawCommands( const CScene& scene, std::vector<SMeshDrawCommand>* commands );