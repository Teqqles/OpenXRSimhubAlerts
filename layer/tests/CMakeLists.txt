add_executable(layer_tests test_layout.cpp test_shm_reader.cpp
  ${CMAKE_SOURCE_DIR}/src/shm_reader.cpp)
target_include_directories(layer_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/../shared ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(layer_tests PRIVATE Catch2::Catch2WithMain)
add_test(NAME layer_tests COMMAND layer_tests)
