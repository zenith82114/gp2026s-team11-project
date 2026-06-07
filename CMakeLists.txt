cmake_minimum_required(VERSION 3.10)
cmake_policy(VERSION 3.10)
project("Final Project" C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
set(PROJECT_ORIGIN ".")

file(GLOB SOURCE CONFIGURE_DEPENDS
    "src/*.h"
    "src/*.cpp"
)

set(SOURCE ${SOURCE} glad.c)
set(IMGUI_DIR ${PROJECT_ORIGIN}/third_party/imgui)
list(APPEND SOURCE
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
set(NAME "main")

link_directories(${PROJECT_ORIGIN}/lib)

if(WIN32)
  set(LIBS glfw3 opengl32 freetype assimp FreeImage)
  # FreeImage is always linked on Windows (vendored lib/dll), so enable the
  # saveImage() path. Without this define, src/main.cpp compiles the no-op stub
  # and the V-key / validation screenshots are silently skipped.
  add_definitions(-DHAS_FREEIMAGE)
  file(COPY ${PROJECT_ORIGIN}/dlls/assimp-vc140-mt.dll
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  file(COPY ${PROJECT_ORIGIN}/dlls/FreeImage.dll
     DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif(WIN32)

if(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_search_module(GLFW REQUIRED glfw3)
  pkg_search_module(FREETYPE freetype2)
  pkg_search_module(ASSIMP_PKG assimp)
  find_package(glm REQUIRED)
  find_package(assimp REQUIRED)
  set(LIBS ${GLFW_LDFLAGS})
  if(TARGET assimp::assimp)
    list(APPEND LIBS assimp::assimp)
  elseif(ASSIMP_PKG_FOUND)
    list(APPEND LIBS ${ASSIMP_PKG_LDFLAGS})
    include_directories(${ASSIMP_PKG_INCLUDE_DIRS})
  else()
    list(APPEND LIBS assimp)
  endif()
  if(FREETYPE_FOUND)
    list(APPEND LIBS ${FREETYPE_LDFLAGS})
    include_directories(${FREETYPE_INCLUDE_DIRS})
  endif()
  find_path(FREEIMAGE_INCLUDE_DIR FreeImage.h)
  find_library(FREEIMAGE_LIBRARY NAMES FreeImage freeimage)
  if(FREEIMAGE_INCLUDE_DIR AND FREEIMAGE_LIBRARY)
    list(APPEND LIBS ${FREEIMAGE_LIBRARY})
    add_definitions(-DHAS_FREEIMAGE)
    include_directories(${FREEIMAGE_INCLUDE_DIR})
  endif()
  include_directories(${GLFW_INCLUDE_DIRS})
  include_directories(${GLM_INCLUDE_DIRS})
  include_directories(${ASSIMP_INCLUDE_DIRS})
endif(UNIX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(${NAME} ${SOURCE})
# The vendored GLFW/glfw3native.h requires BOTH a window and a context API macro.
# imgui's GLFW backend only defines GLFW_EXPOSE_NATIVE_WIN32, so add the WGL macro
# here to avoid the "No context API specified" #error.
if(WIN32)
  target_compile_definitions(${NAME} PRIVATE GLFW_EXPOSE_NATIVE_WGL)
endif(WIN32)
target_link_libraries(${NAME} ${LIBS})
include_directories(${PROJECT_ORIGIN}/includes)
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)
