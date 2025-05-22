# Minimum required CMake version
cmake_minimum_required(VERSION 3.14)

# Project name and language
project(ModuleTest LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Collect all source files recursively in src/ and include/
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/VisionRT/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*/*/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules/*/*.cpp"
)

# Collect all header files (optional, for clarity)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules/*/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules/*/*.hpp"
)

# Define the executable
add_executable(ModuleTest ${SOURCES})

# Include the header directories
target_include_directories(ModuleTest PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/modules"
)

# Link against any necessary libraries (adjust as needed)
# target_link_libraries(ModuleTest PRIVATE SomeLibrary)

# Additional flags (optional)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")  # /W4 for maximum warnings, /WX to treat warnings as errors

# Enable folder structure in IDEs like Visual Studio
# source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SOURCES})
# source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/include" PREFIX "Header Files" FILES ${HEADERS})

# Display collected files (for debugging)
message(STATUS "Collected source files: ${SOURCES}")
message(STATUS "Collected header files: ${HEADERS}")