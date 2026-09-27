cmake_minimum_required(VERSION 3.21)
project(OpenXRSimHubAlertsLayer CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.2)
FetchContent_MakeAvailable(Catch2)

FetchContent_Declare(OpenXR
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git GIT_TAG release-1.1.43)
FetchContent_MakeAvailable(OpenXR)

# stb_truetype and stb_image (single header, public domain / MIT). No CMake
# project upstream, so only populate the sources.
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

# Font and icons compiled into the DLL as RCDATA, from the one shared copy.
set(SHARED_ASSETS ${CMAKE_SOURCE_DIR}/../shared/assets)
configure_file(src/assets.rc.in ${CMAKE_BINARY_DIR}/generated/assets.rc @ONLY)

add_library(OpenXRSimHubAlerts SHARED
  src/layer.cpp src/log.cpp src/session.cpp src/endframe.cpp
  src/shm_reader.cpp src/overlay.cpp src/frame_pacer.cpp src/quad_fit.cpp
  src/render/xr_swapchain.cpp
  src/render/d3d11_backend.cpp src/render/d3d12_backend.cpp src/render/vulkan_backend.cpp
  src/atlas.cpp src/embedded_assets.cpp ${CMAKE_BINARY_DIR}/generated/assets.rc)
target_include_directories(OpenXRSimHubAlerts PRIVATE ${CMAKE_SOURCE_DIR}/../shared src ${stb_SOURCE_DIR})
target_link_libraries(OpenXRSimHubAlerts PRIVATE OpenXR::headers d3d11 d3d12 dxgi d3dcompiler)
find_package(Vulkan)
if(Vulkan_FOUND)
  target_include_directories(OpenXRSimHubAlerts PRIVATE ${Vulkan_INCLUDE_DIRS})
  # The Vulkan backend calls vk* entry points directly, so link the loader lib
  # (headers alone leave those symbols unresolved).
  target_link_libraries(OpenXRSimHubAlerts PRIVATE Vulkan::Vulkan)
endif()

# Copy the API layer manifest next to the built DLL so its relative
# library_path (".\\OpenXRSimHubAlerts.dll") resolves at load time.
add_custom_command(TARGET OpenXRSimHubAlerts POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/src/manifest/OpenXRSimHubAlerts.json
    $<TARGET_FILE_DIR:OpenXRSimHubAlerts>/OpenXRSimHubAlerts.json)

enable_testing()
add_subdirectory(tests)
