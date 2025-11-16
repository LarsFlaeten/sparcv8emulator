set(LIBNAME "peripherals")
message(STATUS " ***** LEON PERIPHERALS ***** ")

include(CheckIncludeFileCXX)
check_include_file_cxx("format" CXX_FORMAT_SUPPORT)
if (NOT CXX_FORMAT_SUPPORT)
        find_package(fmt REQUIRED)
endif()


# Sources
include_directories("${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/libraries" )
file(GLOB SOURCE_FILES *.cpp */*.cpp)

# Libraries
#if(UNIX)
#	set(LIBS ${LIBS} rt)
#endif(UNIX)

message(STATUS "Using libs:  " ${LIBS})
message(STATUS "Include dirs:" ${PROJECT_SOURCE_DIR} ", " ${PROJECT_SOURCE_DIR}/libraries)

set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -std=c++20")
message(STATUS "Using CXX flags: " ${CMAKE_CXX_FLAGS})
#link_directories(${PROJECT_SOURCE_DIR}/libraries ${PROJECT_SOURCE_DIR}/libraries/cspice)

# Static or shared?
set(LIBTYPE SHARED)


# Create a library
add_library(${LIBNAME} ${LIBTYPE} ${SOURCE_FILES})
if (NOT CXX_FORMAT_SUPPORT)
    target_link_libraries(${LIBNAME} fmt::fmt)
endif()

target_link_libraries(${LIBNAME} portaudio)

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# Install headers
FILE(GLOB include-files *.h)
INSTALL(FILES ${include-files} DESTINATION include/${LIBNAME})

# Install library
INSTALL(TARGETS ${LIBNAME} LIBRARY DESTINATION ${LIB_INSTALL_DIR} ARCHIVE DESTINATION ${LIB_INSTALL_DIR})

