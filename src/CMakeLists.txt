add_subdirectory(sparcv8)
add_subdirectory(gdb)
add_subdirectory(peripherals)


set(EXENAME sparcv8_runelf)
set(EXENAME2 sparcv8_leon)
set(EXENAME3 console)


# Sources
include_directories(/usr/local/include "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/libraries" "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")

link_directories(${PROJECT_SOURCE_DIR}/libraries ${PROJECT_SOURCE_DIR}/lib)

add_executable(${EXENAME} sparcv8_runelf.cpp readelf.cpp debug.cpp dis.cpp)
target_compile_options(${EXENAME} PUBLIC -std=c++20 -Wall)
target_link_libraries(${EXENAME} sparcv8)

add_executable(${EXENAME2} sparcv8_leon.cpp readelf.cpp debug.cpp dis.cpp)
target_compile_options(${EXENAME2} PUBLIC -std=c++20 -Wall)
target_link_libraries(${EXENAME2} sparcv8 peripherals gdbstub)

add_executable(${EXENAME3} console_test.cpp)
target_compile_options(${EXENAME2} PUBLIC -std=c++20 -Wall)
target_link_libraries(${EXENAME2} sparcv8 peripherals)


