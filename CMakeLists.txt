cmake_minimum_required(VERSION 3.10)

# Set the project name
project(PLF)

# Add the executable
add_executable(plf WIN32 main.c)

# Add include directories for Lua
target_include_directories(plf PRIVATE ${CMAKE_SOURCE_DIR}/lua/include)

# Link against the Windows API and Lua library
target_link_libraries(plf PRIVATE WinHttp user32 gdi32 ${CMAKE_SOURCE_DIR}/lua/lua54.lib)
