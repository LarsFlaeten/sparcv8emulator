cmake_minimum_required(VERSION 3.6)
message(STATUS " ***** TEST ***** ")
# We need thread support
find_package(Threads REQUIRED)

# Locate GTest
#find_package(PkgConfig)
#pkg_check_modules(GTEST REQUIRED gtest>=1.7.0)
#pkg_check_modules(GMOCK REQUIRED gmock>=1.7.0)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include_directories(
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/libraries
)
set (GTEST_LIBRARIES gtest_main)
#link_directories(${PROJECT_SOURCE_DIR}/libraries ${PROJECT_SOURCE_DIR}/libraries/cspice)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++23")
message(STATUS "Using CXX flags: " ${CMAKE_CXX_FLAGS})

# compile the asm tests:
add_custom_target(run_setup_tool
    COMMAND ${PROJECT_SOURCE_DIR}/test/asm/runtests nc
    COMMENT "Running ASM tests (pre-built binaries, no recompile)"
)


# Link runTests with what we want to test and the GTest and pthread library
file(GLOB SOURCE_FILES *.cpp)
message(STATUS "Compiling testfiles: " ${SOURCE_FILES})
message(STATUS "With test libs: " ${GTEST_LIBRARIES})
add_executable(sparcv8_tests ${SOURCE_FILES})
target_link_libraries(sparcv8_tests ${GTEST_LIBRARIES} sparcv8 peripherals gdbstub pthread SDL2)
# Must see the same struct layout as the libraries it links against
if(PERF_STATS)
    target_compile_definitions(sparcv8_tests PRIVATE PERF_STATS)
endif()
#add_test(NAME sparcv8_tests COMMAND sparcv8_tests)
include(GoogleTest)
gtest_discover_tests(sparcv8_tests)

# Ensure the setup tool runs before tests using dependencies
add_dependencies(sparcv8_tests run_setup_tool)

