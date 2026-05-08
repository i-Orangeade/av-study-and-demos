#include <SDL2/SDL.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string path;
    int width = 0;
    int height = 0;
    int fps = 25;
};

void printUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " <input.yuv> <width> <height> [fps]\n\n"
        << "Example:\n"
        << "  " << program << " test_640x360.yuv 640 360 25\n";
}

bool parsePositiveInt(const char* text, int& value) {
    char* end = nullptr;
    long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 100000) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool parseOptions(int argc, char* argv[], Options& options) {
    if (argc < 4 || argc > 5) {
        return false;
    }

    options.path = argv[1];
    if (!parsePositiveInt(argv[2], options.width) ||
        !parsePositiveInt(argv[3], options.height)) {
        return false;
    }

    if (argc == 5 && !parsePositiveInt(argv[4], options.fps)) {
        return false;
    }

    if (options.width % 2 != 0 || options.height % 2 != 0) {
        std::cerr << "YUV420P requires even width and height." << std::endl;
        return false;
    }

    return true;
}

void destroySdl(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture) {
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

int runPlayer(const Options& options) {
    std::ifstream input(options.path, std::ios::binary);
    if (!input) {
        std::cerr << "Failed to open input file: " << options.path << std::endl;
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("AV YUV420P Player",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          options.width,
                                          options.height,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
        destroySdl(window, renderer, texture);
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl;
        destroySdl(window, renderer, texture);
        return 1;
    }

    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_IYUV,
                                SDL_TEXTUREACCESS_STREAMING,
                                options.width,
                                options.height);
    if (!texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
        destroySdl(window, renderer, texture);
        return 1;
    }

    const size_t ySize = static_cast<size_t>(options.width) * options.height;
    const size_t uvSize = ySize / 4;
    const size_t frameSize = ySize + uvSize * 2;
    std::vector<unsigned char> frame(frameSize);

    const auto frameDuration = std::chrono::milliseconds(1000 / options.fps);
    bool running = true;
    long frameIndex = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT ||
                (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
                running = false;
            }
        }

        if (!input.read(reinterpret_cast<char*>(frame.data()), static_cast<std::streamsize>(frame.size()))) {
            std::cout << "End of file, played " << frameIndex << " frames." << std::endl;
            break;
        }

        const unsigned char* yPlane = frame.data();
        const unsigned char* uPlane = yPlane + ySize;
        const unsigned char* vPlane = uPlane + uvSize;

        if (SDL_UpdateYUVTexture(texture,
                                 nullptr,
                                 yPlane,
                                 options.width,
                                 uPlane,
                                 options.width / 2,
                                 vPlane,
                                 options.width / 2) != 0) {
            std::cerr << "SDL_UpdateYUVTexture failed: " << SDL_GetError() << std::endl;
            destroySdl(window, renderer, texture);
            return 1;
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        ++frameIndex;
        std::this_thread::sleep_for(frameDuration);
    }

    destroySdl(window, renderer, texture);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options options;
    if (!parseOptions(argc, argv, options)) {
        printUsage(argv[0]);
        return 1;
    }

    return runPlayer(options);
}
