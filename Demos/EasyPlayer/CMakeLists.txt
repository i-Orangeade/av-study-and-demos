cmake_minimum_required(VERSION 3.10)
project(av_player)

# C++11 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启所有警告
if(MINGW)
    add_compile_options(-Wall -Wextra)
endif()

# 1. 手动指定 FFmpeg 路径
set(FFMPEG_ROOT "D:/FFmpeg/ffmpeg-7.1")
set(FFMPEG_INCLUDE_DIR "${FFMPEG_ROOT}/include")
set(FFMPEG_LIB_DIR "${FFMPEG_ROOT}/lib")
set(FFMPEG_BIN_DIR "${FFMPEG_ROOT}/bin")

# 查找 SDL2 库
find_package(SDL2 REQUIRED)

# 编译可执行文件
add_executable(av_player main.cpp)

# 头文件路径
target_include_directories(av_player PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

# 库目录
target_link_directories(av_player PRIVATE
    ${FFMPEG_LIB_DIR}
)

# 链接库
target_link_libraries(av_player
    avformat
    avcodec
    avutil
    swscale
    swresample
    ${SDL2_LIBRARIES}
)

# -------------------------- 运行时 DLL 部署（Windows） --------------------------
# 解决运行时找不到 FFmpeg DLL（avcodec-61.dll 等）的问题。
if(WIN32)
    set(FFMPEG_RUNTIME_DLLS
        "${FFMPEG_BIN_DIR}/avcodec-61.dll"
        "${FFMPEG_BIN_DIR}/avformat-61.dll"
        "${FFMPEG_BIN_DIR}/avutil-59.dll"
        "${FFMPEG_BIN_DIR}/swresample-5.dll"
        "${FFMPEG_BIN_DIR}/swscale-8.dll"
    )

    foreach(dll IN LISTS FFMPEG_RUNTIME_DLLS)
        add_custom_command(TARGET av_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${dll}"
                    "$<TARGET_FILE_DIR:av_player>"
            VERBATIM
        )
    endforeach()
endif()