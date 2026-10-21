cmake_minimum_required(VERSION 3.16)
project(MCT VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MCT_BUILD_TESTS "Build the mct selftest/probe binaries" ON)
option(MCT_ENABLE_LTO "Enable link time optimization in release builds" ON)

# 单文件发布：MSVC 静态运行库
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(WIN32)
  enable_language(RC)
endif()

file(GLOB_RECURSE MCT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM MCT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(mct_core STATIC ${MCT_SOURCES})
target_include_directories(mct_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(mct_core PUBLIC MCT_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(mct_core PUBLIC /utf-8 /W3 /EHsc)
  target_compile_definitions(mct_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
else()
  target_compile_options(mct_core PUBLIC -Wall -Wextra -Wno-unused-parameter -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(MINGW)
  target_compile_options(mct_core PUBLIC -ffunction-sections -fdata-sections)
endif()

if(WIN32)
  target_compile_definitions(mct_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_link_libraries(mct_core PUBLIC winhttp shell32)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(mct_core PUBLIC Threads::Threads)
endif()

set(MCT_MAIN_SOURCES src/main.cpp)
if(WIN32)
  list(APPEND MCT_MAIN_SOURCES src/app.rc)
endif()
add_executable(mct ${MCT_MAIN_SOURCES})
target_link_libraries(mct PRIVATE mct_core)

if(WIN32 AND MINGW)
  # 静态链接 libstdc++/libgcc/winpthread，发布时只需要一个 exe
  target_link_options(mct PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--gc-sections
                      $<$<CONFIG:Release>:-s>)
endif()

if(WIN32 AND MSVC)
  target_link_options(mct PRIVATE $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

if(MCT_BUILD_TESTS)
  add_executable(mct_selftest tests/selftest.cpp)
  target_link_libraries(mct_selftest PRIVATE mct_core)
  add_executable(mct_probe tests/probe.cpp)
  target_link_libraries(mct_probe PRIVATE mct_core)

  if(WIN32 AND MINGW)
    target_link_options(mct_selftest PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--gc-sections)
    target_link_options(mct_probe PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--gc-sections)
  endif()
endif()

if(MCT_ENABLE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT MCT_IPO_OK OUTPUT MCT_IPO_MESSAGE LANGUAGES CXX)
  if(MCT_IPO_OK)
    set_property(TARGET mct_core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    set_property(TARGET mct PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    if(TARGET mct_selftest)
      set_property(TARGET mct_selftest PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
      set_property(TARGET mct_probe PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
    message(STATUS "MCT: LTO 已启用")
  else()
    message(STATUS "MCT: 当前工具链不支持 LTO（${MCT_IPO_MESSAGE}）")
  endif()
endif()

install(TARGETS mct RUNTIME DESTINATION bin)
