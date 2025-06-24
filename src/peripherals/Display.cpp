#include "Display.h"
#include <chrono>
#include <cstring>

Display::Display(int width, int height, int bpp, int refreshRateHz, const void* framebuffer)
    : width(width), height(height), bpp(bpp), refreshRateHz(refreshRateHz),
      framebuffer(framebuffer), running(false), enabled(false) {}

Display::~Display() {
    stop();
}

void Display::start() {
    running = true;
    renderThread = std::thread(&Display::renderLoop, this);
}

void Display::enable() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        enabled = true;
    }
    cv.notify_one();
}

void Display::disable(bool hideWindow) {
    {
        std::lock_guard<std::mutex> lock(mtx);
        enabled = false;
        hideWindowWhenDisabled = hideWindow;
    }
}


void Display::stop() {
    if (running) {
        running = false;
        enable(); // In case it's waiting
        if (renderThread.joinable())
            renderThread.join();
    }
}

bool Display::isRunning() const {
    return running.load();
}

bool Display::isEnabled() const {
    return enabled.load();
}

void Display::renderLoop() {
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return enabled.load(); });
    }

    window = SDL_CreateWindow("Screen Buffer",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);

    const int frameDelayMs = 1000 / refreshRateHz;

    while (running) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return !running || enabled.load(); });
        if (!running) break;
        lock.unlock();

        SDL_ShowWindow(window);

        while (running && enabled) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                // You can optionally handle SDL_QUIT here
            }
            void* pixels;
            int pitch;
            SDL_LockTexture(texture, nullptr, &pixels, &pitch);
            std::memcpy(pixels, framebuffer, height * pitch);
            SDL_UnlockTexture(texture);

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);

            std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
        }

        if (!running) break;

        if (hideWindowWhenDisabled) {
            SDL_HideWindow(window);
        } else {
            // Blank screen (fill black)
            SDL_RenderClear(renderer);
            SDL_RenderPresent(renderer);
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}