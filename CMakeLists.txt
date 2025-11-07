cmake_minimum_required(VERSION 3.16)
project(v2v_map LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ✅ Ajoute ces lignes :
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(v2v_map
  src/main.cpp
  src/MapView.cpp
  src/MapView.h
  src/TileManager.cpp
  src/TileManager.h
  src/RoadGraph.cpp
  src/RoadGraph.h
  src/RoadGraphLoader.cpp
  src/RoadGraphLoader.h
  src/OSMDownloader.cpp
  src/OSMDownloader.h
  src/Vehicle.h
)

target_link_libraries(v2v_map PRIVATE Qt6::Widgets Qt6::Network)

find_path(LIBOSMIUM_INCLUDE_DIR osmium/io/any_input.hpp)
if (LIBOSMIUM_INCLUDE_DIR)
  target_compile_definitions(v2v_map PRIVATE HAVE_LIBOSMIUM)
  target_include_directories(v2v_map PRIVATE ${LIBOSMIUM_INCLUDE_DIR})
endif()

find_path(PROTOZERO_INCLUDE_DIR protozero/pbf_message.hpp)
if (PROTOZERO_INCLUDE_DIR)
  target_include_directories(v2v_map PRIVATE ${PROTOZERO_INCLUDE_DIR})
endif()
