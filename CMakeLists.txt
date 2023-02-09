# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.
cmake_minimum_required(VERSION 3.14)
project(d3d12test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    DirectX-Headers
    GIT_REPOSITORY https://github.com/Microsoft/DirectX-Headers.git
    GIT_TAG main
)
FetchContent_MakeAvailable(DirectX-Headers)

if (NOT WIN32)
set (dxc_url https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2212/linux_dxc_2022_12_16.tar.gz)
else()
set (dxc_url https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.7.2212/dxc_2022_12_16.zip)
endif()


FetchContent_Declare(
  dxc
  URL      ${dxc_url}
)

FetchContent_MakeAvailable(dxc)

add_custom_target(shaders)

set(HLSL_SHADER_FILES shader.hlsl)

set_source_files_properties(shader.hlsl PROPERTIES ShaderType "cs")
set_source_files_properties(${HLSL_SHADER_FILES} PROPERTIES ShaderModel "6_0")
set (dxc_bin_dir ${dxc_SOURCE_DIR}/bin/x64)

foreach(FILE ${HLSL_SHADER_FILES})
  get_filename_component(FILE_WE ${FILE} NAME_WE)
  get_source_file_property(shadertype ${FILE} ShaderType)
  get_source_file_property(shadermodel ${FILE} ShaderModel)
    if (NOT WIN32)
        file(COPY ${dxc_bin_dir}/dxc DESTINATION ${dxc_SOURCE_DIR}/lib/x64 FILE_PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ)
        file(CREATE_LINK ${dxc_SOURCE_DIR}/lib/x64/libdxcompiler.so ${dxc_SOURCE_DIR}/lib/x64/libdxcompiler.so.3.7)
        set (dxc_bin_dir ${dxc_SOURCE_DIR}/lib/x64)
  endif()
  add_custom_command(TARGET shaders
                    COMMAND ${dxc_bin_dir}/dxc /Emain /T${shadertype}_${shadermodel} $<IF:$<CONFIG:DEBUG>,/Od,/O3> /Fo ${CMAKE_BINARY_DIR}/${FILE_WE}.cso  ${CMAKE_CURRENT_SOURCE_DIR}/${FILE}
                    MAIN_DEPENDENCY ${FILE}
                    COMMENT "HLSL ${FILE}"
                    WORKING_DIRECTORY ${dxc_bin_dir}
                     VERBATIM)
endforeach(FILE)

add_executable(d3d12test test.cpp)

add_dependencies(d3d12test shaders)

target_link_libraries(d3d12test Microsoft::DirectX-Headers Microsoft::DirectX-Guids)

if (NOT WIN32)
    target_link_libraries(d3d12test dl)
endif()
