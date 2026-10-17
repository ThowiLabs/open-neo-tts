cmake_minimum_required(VERSION 3.18)
if(NOT DEFINED OPEN_NEO_VERSION)
  set(OPEN_NEO_VERSION 0.1.0)
endif()
project(open_neo_tts VERSION ${OPEN_NEO_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Open edition: ONEO compiler and inference CLI only.
option(NEO_LITE_ONLY "Build the open inference distribution" ON)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_path(NEO_ORT_INCLUDE onnxruntime_cxx_api.h
  HINTS "${NEO_ORT_ROOT}/include" "${NEO_ORT_ROOT}/include/onnxruntime" REQUIRED)
find_library(NEO_ORT_LIBRARY NAMES onnxruntime
  HINTS "${NEO_ORT_ROOT}/lib" REQUIRED)
find_path(NEO_ESPEAK_INCLUDE espeak-ng/speak_lib.h REQUIRED)
find_library(NEO_ESPEAK_LIBRARY NAMES espeak-ng REQUIRED)
find_path(NEO_JSONC_INCLUDE json-c/json.h REQUIRED)
find_library(NEO_JSONC_LIBRARY NAMES json-c json-c_static REQUIRED)

add_library(oneo-lib STATIC src/open_format/oneo.cpp)
target_include_directories(oneo-lib PUBLIC src)
target_link_libraries(oneo-lib PUBLIC OpenSSL::Crypto "${NEO_JSONC_LIBRARY}")

add_executable(oneo-compiler src/open_format/compiler_cli.cpp)
target_link_libraries(oneo-compiler PRIVATE oneo-lib)
add_executable(open-neo-lite
  src/open_format/lite_cli.cpp
  src/web_infer.cpp
  src/phonemes.cpp
  src/vits_text_chunker.cpp)
target_include_directories(open-neo-lite PRIVATE src
  "${NEO_ORT_INCLUDE}" "${NEO_ESPEAK_INCLUDE}")
target_link_libraries(open-neo-lite PRIVATE oneo-lib
  "${NEO_ORT_LIBRARY}" "${NEO_ESPEAK_LIBRARY}" "${NEO_JSONC_LIBRARY}")
if(WIN32)
  target_link_libraries(open-neo-lite PRIVATE ws2_32)
endif()
if(MINGW)
  target_link_options(open-neo-lite PRIVATE -municode)
endif()
enable_testing()
add_test(NAME oneo_compiler_help COMMAND oneo-compiler --help)
add_test(NAME open_neo_lite_help COMMAND open-neo-lite --help)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME oneo_interoperability
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/oneo_interop_test.py"
      "$<TARGET_FILE:oneo-compiler>" "$<TARGET_FILE:open-neo-lite>"
      "${CMAKE_SOURCE_DIR}/tests/fixtures/tiny-voice.onnx"
      "${CMAKE_SOURCE_DIR}/tests/fixtures/tiny-voice.onnx.json")
endif()
