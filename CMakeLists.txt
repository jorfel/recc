cmake_minimum_required(VERSION 3.16)
project(recc VERSION 1.0.0 LANGUAGES CXX)

add_subdirectory(extlib/cxxopts)
add_subdirectory(extlib/asmjit)


file(GLOB_RECURSE RECC_CLIENT_SRC_FILES CONFIGURE_DEPENDS src/client/*.cpp src/client/*.hpp)
add_executable(recc ${RECC_CLIENT_SRC_FILES})
set_property(TARGET recc PROPERTY CXX_STANDARD 20)
target_link_libraries(recc PRIVATE cxxopts::cxxopts asmjit::asmjit)


file(GLOB_RECURSE RECC_DLL_SRC_FILES CONFIGURE_DEPENDS src/dll/*.cpp src/dll/*.hpp)
add_library(recc_dll SHARED ${RECC_DLL_SRC_FILES})
set_property(TARGET recc_dll PROPERTY CXX_STANDARD 20)


install(TARGETS recc recc_dll CONFIGURATIONS Release)
