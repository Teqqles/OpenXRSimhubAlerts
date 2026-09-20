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

add_library(OpenXRSimHubAlerts SHARED
  src/layer.cpp src/log.cpp src/session.cpp src/endframe.cpp
  src/shm_reader.cpp src/overlay.cpp
  src/render/d3d11_backend.cpp src/render/d3d12_backend.cpp src/render/vulkan_backend.cpp)
target_include_directories(OpenXRSimHubAlerts PRIVATE ${CMAKE_SOURCE_DIR}/../shared src)
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
