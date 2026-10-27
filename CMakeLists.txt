cmake_minimum_required(VERSION 3.20)
if(APPLE)
  # Must be set before project(): plugin loads on macOS 13+, universal (Apple Silicon + Intel).
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
  option(FALSECOLOR_UNIVERSAL "Build arm64 + x86_64" OFF)
  if(FALSECOLOR_UNIVERSAL)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  endif()
endif()
project(FalseColor VERSION 1.0.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Never use fast-math: band comparisons must stay exact.
if(NOT MSVC)
  add_compile_options(-fno-fast-math)
endif()
# Windows: static C++ runtime, so the plugin loads in any OFX host without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(FALSECOLOR_GPU_METAL  "Metal backend (macOS)" ${APPLE})
option(FALSECOLOR_GPU_OPENCL "OpenCL backend (prepared, disabled)" OFF)
option(FALSECOLOR_GPU_CUDA   "CUDA backend (prepared, disabled)" OFF)
set(OFX_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openfx/include" CACHE PATH
    "Folder containing ofxCore.h, ofxImageEffect.h, ofxGPURender.h (from the OpenFX repo)")

if(FALSECOLOR_GPU_OPENCL OR FALSECOLOR_GPU_CUDA)
  message(WARNING "OpenCL/CUDA are prepared but NOT implemented in the build on this phase.")
endif()

# --- core (no external dependencies) ---------------------------------------------------------
add_library(fc_core STATIC falsecolor_core.cpp cpu_render.cpp overlay.cpp)
target_include_directories(fc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(fc_core PUBLIC Threads::Threads)

# --- Metal ------------------------------------------------------------------------------------
set(FC_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
if(APPLE AND FALSECOLOR_GPU_METAL)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  file(MAKE_DIRECTORY "${FC_GEN_DIR}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/kernels/falsecolor.metal" "${CMAKE_CURRENT_SOURCE_DIR}/fc_shared.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/palette_generated.h")
  execute_process(COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_metal.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/kernels/falsecolor.metal" "${CMAKE_CURRENT_SOURCE_DIR}"
    "${FC_GEN_DIR}/falsecolor_metal_source.h" RESULT_VARIABLE _emb)
  if(NOT _emb EQUAL 0)
    message(FATAL_ERROR "embed_metal.py failed")
  endif()
  add_library(fc_metal STATIC metal_backend.mm)
  target_include_directories(fc_metal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${FC_GEN_DIR})
  target_compile_options(fc_metal PRIVATE -fobjc-arc)
  target_link_libraries(fc_metal PUBLIC "-framework Metal" "-framework Foundation")
endif()

# --- tests that need nothing but the core ---------------------------------------------------
enable_testing()
foreach(t ramp edges float sources overlay formats render_path)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE fc_core)
  add_test(NAME test_${t} COMMAND test_${t})
endforeach()
if(TARGET fc_metal)
  add_executable(test_gpu_parity tests/test_gpu_parity.cpp)
  target_link_libraries(test_gpu_parity PRIVATE fc_core fc_metal)
  add_test(NAME test_gpu_parity COMMAND test_gpu_parity)
  set_tests_properties(test_gpu_parity PROPERTIES SKIP_RETURN_CODE 77)
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_palette COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_palette.py)
  set_tests_properties(test_palette PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Regenerate palette_generated.h from falsecolor_core.cpp:  cmake --build build --target regen_palette
add_executable(gen_palette tools/gen_palette.cpp falsecolor_core.cpp)
target_compile_definitions(gen_palette PRIVATE FC_GENERATOR)
target_include_directories(gen_palette PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_custom_target(regen_palette COMMAND gen_palette > "${CMAKE_CURRENT_SOURCE_DIR}/palette_generated.h" DEPENDS gen_palette)

# Preview renderer (CPU pipeline + scale -> PPM), for checking the look without Resolve.
add_executable(fc_preview tools/preview.cpp)
target_link_libraries(fc_preview PRIVATE fc_core)

# --- plugin -----------------------------------------------------------------------------------
if(NOT EXISTS "${OFX_INCLUDE_DIR}/ofxCore.h")
  message(WARNING "OpenFX headers not found in ${OFX_INCLUDE_DIR}. Run:\n"
    "  git clone https://github.com/AcademySoftwareFoundation/openfx third_party/openfx\n"
    "or pass -DOFX_INCLUDE_DIR=<path>/include. The plugin target is NOT built.")
else()
    set(PLUGIN_SOURCES FalseColorPlugin.cpp)
    add_library(FalseColor MODULE ${PLUGIN_SOURCES})
    target_include_directories(FalseColor PRIVATE ${OFX_INCLUDE_DIR})
    target_link_libraries(FalseColor PRIVATE fc_core)
    if(UNIX AND NOT APPLE)
      target_link_libraries(FalseColor PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    set_target_properties(FalseColor PROPERTIES PREFIX "" SUFFIX ".ofx"
      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(APPLE)
      if(TARGET fc_metal)
        target_link_libraries(FalseColor PRIVATE fc_metal)
      endif()
      set(BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/FalseColor.ofx.bundle")
      add_custom_command(TARGET FalseColor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist" "${BUNDLE_DIR}/Contents/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:FalseColor>" "${BUNDLE_DIR}/Contents/MacOS/FalseColor.ofx"
        COMMENT "Assembling FalseColor.ofx.bundle")
    endif()
    if(UNIX AND NOT APPLE)
      # NOT TESTED locally (built by CI). Bundle: FalseColor.ofx.bundle/Contents/Linux-x86-64/FalseColor.ofx
      set(BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/FalseColor.ofx.bundle")
      add_custom_command(TARGET FalseColor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}/Contents/Linux-x86-64"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:FalseColor>" "${BUNDLE_DIR}/Contents/Linux-x86-64/FalseColor.ofx")
    endif()
    if(WIN32)
      # NOT TESTED. Bundle layout: FalseColor.ofx.bundle/Contents/Win64/FalseColor.ofx
      set(BUNDLE_DIR "${CMAKE_CURRENT_BINARY_DIR}/FalseColor.ofx.bundle")
      add_custom_command(TARGET FalseColor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BUNDLE_DIR}/Contents/Win64"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:FalseColor>" "${BUNDLE_DIR}/Contents/Win64/FalseColor.ofx")
    endif()
endif()
