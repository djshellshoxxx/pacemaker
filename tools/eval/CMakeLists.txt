add_executable(pacemaker_eval main.cpp)
target_link_libraries(pacemaker_eval PRIVATE pacemaker_engine)
# `gen` reuses the synthetic drum machine from the tests.
target_include_directories(pacemaker_eval PRIVATE ${PROJECT_SOURCE_DIR}/Tests)
if(NOT MSVC)
  target_compile_options(pacemaker_eval PRIVATE -Wall -Wextra)
endif()
