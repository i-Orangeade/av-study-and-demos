cmake_minimum_required(VERSION 3.10)
project(av_easy_player)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 开启所有警告
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# Ubuntu/Linux 下通过 pkg-config 查找 FFmpeg 和 SDL2 开发库。
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# 编译可执行文件
add_executable(av_easy_player main.cpp)

# 链接库
target_link_libraries(av_easy_player PRIVATE
    PkgConfig::FFMPEG
    PkgConfig::SDL2
    Threads::Threads
)