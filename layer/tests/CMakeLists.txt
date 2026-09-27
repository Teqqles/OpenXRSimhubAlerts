add_executable(layer_tests test_layout.cpp test_shm_reader.cpp test_overlay.cpp test_frame_pacer.cpp test_quad_fit.cpp test_atlas.cpp
  ${CMAKE_SOURCE_DIR}/src/shm_reader.cpp
  ${CMAKE_SOURCE_DIR}/src/overlay.cpp
  ${CMAKE_SOURCE_DIR}/src/frame_pacer.cpp
  ${CMAKE_SOURCE_DIR}/src/quad_fit.cpp
  ${CMAKE_SOURCE_DIR}/src/atlas.cpp
  ${CMAKE_SOURCE_DIR}/src/embedded_assets.cpp
  ${CMAKE_BINARY_DIR}/generated/assets.rc)
target_include_directories(layer_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/../shared ${CMAKE_SOURCE_DIR}/src ${stb_SOURCE_DIR})
target_compile_definitions(layer_tests PRIVATE
  SHARED_ASSETS_DIR="${CMAKE_SOURCE_DIR}/../shared/assets")
target_link_libraries(layer_tests PRIVATE Catch2::Catch2WithMain)
add_test(NAME layer_tests COMMAND layer_tests)
