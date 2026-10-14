# Embedded web UI (RS-03 section 2): static library, plain C++17, no JUCE. cpp-httplib is fetched by tag; CI and
# offline machines can point FETCHCONTENT_SOURCE_DIR_HTTPLIB at a local checkout instead.
if(TARGET pacemaker_web)
  return()
endif()

include(FetchContent)
set(PACEMAKER_HTTPLIB_TAG "v0.29.0" CACHE STRING "cpp-httplib git tag")
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG ${PACEMAKER_HTTPLIB_TAG}
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR pacemaker_use_header_only)     # header-only: skip httplib's own CMakeLists
FetchContent_MakeAvailable(httplib)

# ---- compile web/assets/* into the binary (no runtime files)
set(PM_WEB_ASSETS
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/index.html
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/metronome.html
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/app.js
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/app.css
  ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.svg)
set(_body "#include \"Assets.h\"\nnamespace pacemaker { namespace webassets {\n")
set(_table "")
foreach(f ${PM_WEB_ASSETS})
  get_filename_component(n ${f} NAME)
  string(MAKE_C_IDENTIFIER ${n} id)
  file(READ ${f} hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
  string(APPEND _body "static const unsigned char d_${id}[] = {${arr}0};\n")
  string(APPEND _table "  { \"${n}\", d_${id}, sizeof(d_${id}) - 1 },\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${f})
endforeach()
string(APPEND _body "const Asset kAssets[] = {\n${_table}  { nullptr, nullptr, 0 }\n};\n} }\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/Assets.cpp.tmp "${_body}")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/Assets.cpp.tmp ${CMAKE_CURRENT_BINARY_DIR}/Assets.cpp COPYONLY)

add_library(pacemaker_web STATIC
  src/WebUi.cpp
  src/Assets.h
  ${CMAKE_CURRENT_BINARY_DIR}/Assets.cpp)
target_include_directories(pacemaker_web
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${httplib_SOURCE_DIR})
# Tests use httplib::Client against the server; expose the header to them.
target_include_directories(pacemaker_web PUBLIC $<BUILD_INTERFACE:${httplib_SOURCE_DIR}>)
target_link_libraries(pacemaker_web PUBLIC pacemaker_services)
target_compile_features(pacemaker_web PUBLIC cxx_std_17)
if(MSVC)
  target_compile_options(pacemaker_web PRIVATE /W4)
else()
  target_compile_options(pacemaker_web PRIVATE -Wall -Wextra)
endif()
