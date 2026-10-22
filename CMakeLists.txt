cmake_minimum_required(VERSION 3.25)
project(todo_api LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(httplib CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 CONFIG REQUIRED)

# Cada modulo = pasta com dono. Glob evita editar este arquivo a cada novo .cpp.
file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS
  src/db/*.cpp src/auth/*.cpp src/mail/*.cpp src/tasks/*.cpp src/http/*.cpp)

add_library(todo_core STATIC ${LIB_SOURCES})
target_include_directories(todo_core PUBLIC src)
target_link_libraries(todo_core PUBLIC
  httplib::httplib
  unofficial::sqlite3::sqlite3
  jwt-cpp::jwt-cpp
  unofficial-sodium::sodium
  nlohmann_json::nlohmann_json)

add_executable(todo_api src/main.cpp)
target_link_libraries(todo_api PRIVATE todo_core)

enable_testing()
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
if(TEST_SOURCES)
  add_executable(todo_tests ${TEST_SOURCES})
  target_link_libraries(todo_tests PRIVATE todo_core Catch2::Catch2WithMain)
  include(CTest)
  include(Catch)
  catch_discover_tests(todo_tests)
endif()
