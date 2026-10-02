// Jolt Physics Library (https://github.com/jrouwe/JoltPhysics)
// SPDX-License-Identifier: MIT

#define NOMINMAX
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        return 1;
    }
    const std::filesystem::path source = argv[1];
    std::vector<std::filesystem::path> shaders;
    for (const auto &entry : std::filesystem::directory_iterator(source))
    {
        if (entry.path().extension() == ".hlsl" && entry.path().stem().string().compare(0, 4, "Hair") == 0)
        {
            shaders.push_back(entry.path());
        }
    }
    shaders.emplace_back(source / "CopyFloat3ToTexture.hlsl");
    std::sort(shaders.begin(), shaders.end());
    std::ofstream output(argv[2], std::ios::binary);
    output << "#pragma once\n#include <cstddef>\nnamespace ComputeShadersDX11 {\n";
    for (const auto &shader : shaders)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> code, error;
        const HRESULT result = D3DCompileFromFile(shader.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &error);
        if (FAILED(result))
        {
            std::cerr << shader << ": " << (error ? static_cast<const char *>(error->GetBufferPointer()) : "compilation failed") << '\n';
            return 1;
        }
        output << "inline constexpr unsigned char " << shader.stem().string() << "[] = {";
        const auto *bytes = static_cast<const unsigned char *>(code->GetBufferPointer());
        for (size_t i = 0; i < code->GetBufferSize(); ++i)
        {
            if (i % 24 == 0)
            {
                output << '\n';
            }
            output << unsigned(bytes[i]) << ',';
        }
        output << "\n};\n";
    }
    output << "struct Shader { const char *name; const unsigned char *data; size_t size; };\ninline constexpr Shader registry[] = {\n";
    for (const auto &shader : shaders)
    {
        const std::string name = shader.stem().string();
        output << "{\"" << name << "\", " << name << ", sizeof(" << name << ")},\n";
    }
    output << "};\n}\n";
    return output ? 0 : 1;
}
