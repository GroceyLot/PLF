cmake_minimum_required(VERSION 3.10)

# Set the project name
project(PLF)

# Set the root directories for SDL2 and LuaJIT
set(SDL2_DIR "PATH/TO/SDL")
set(LUAJIT_DIR "PATH/TO/LuaJIT")

# Add the executable
add_executable(plf main.c)

# Include directories
target_include_directories(plf PRIVATE
    "${SDL2_DIR}/include"
    "${LUAJIT_DIR}/include"
)

# Library directories
target_link_directories(plf PRIVATE
    "${SDL2_DIR}/lib/x64"
    "${LUAJIT_DIR}/lib"
)

# Link libraries
target_link_libraries(plf PRIVATE
    SDL2
    SDL2main
    lua51  # Replace with 'luajit' if that's the correct library name
)
