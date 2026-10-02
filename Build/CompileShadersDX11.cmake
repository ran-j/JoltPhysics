# DX11 shader compilation and packaging are owned by the Jolt backend.
add_executable(JoltShaderCompilerDX11 "${PHYSICS_REPO_ROOT}/Build/CompileShadersDX11.cpp")
target_compile_features(JoltShaderCompilerDX11 PRIVATE cxx_std_17)
# The build-time filesystem tool uses exceptions, independently of the runtime library's exception policy.
if(MSVC)
    target_compile_options(JoltShaderCompilerDX11 PRIVATE /EHsc)
endif()
target_link_libraries(JoltShaderCompilerDX11 PRIVATE d3dcompiler)
file(GLOB JOLT_DX11_SHADER_INPUTS CONFIGURE_DEPENDS "${JOLT_PHYSICS_ROOT}/Shaders/*.h" "${JOLT_PHYSICS_ROOT}/Shaders/Hair*.hlsl")
set(JOLT_DX11_BYTECODE "${CMAKE_CURRENT_BINARY_DIR}/generated/ComputeShadersDX11.generated.h")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${JOLT_DX11_BYTECODE}"
    COMMAND JoltShaderCompilerDX11 "${JOLT_PHYSICS_ROOT}/Shaders" "${JOLT_DX11_BYTECODE}"
    DEPENDS JoltShaderCompilerDX11 ${JOLT_DX11_SHADER_INPUTS} "${JOLT_PHYSICS_ROOT}/Shaders/CopyFloat3ToTexture.hlsl" VERBATIM)
target_sources(Jolt PRIVATE "${JOLT_DX11_BYTECODE}")
target_include_directories(Jolt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
