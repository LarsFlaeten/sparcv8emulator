// SPDX-License-Identifier: MIT
#include "Display.h"
#include <chrono>
#include <cstring>

#include <pthread.h>

void set_thread_name(const char* name) {
    pthread_setname_np(pthread_self(), name);
}

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
    set_thread_name("vga_display");

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this]() { return enabled.load(); });
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        resolution_changed = false;
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

        // Apply resolution change if requested
        if (resolution_changed.load()) {
            resolution_changed = false;
            SDL_DestroyTexture(texture);
            SDL_SetWindowSize(window, width, height);
            texture = SDL_CreateTexture(renderer,
                                        SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING,
                                        width, height);
        }
        lock.unlock();

        SDL_ShowWindow(window);

        while (running && enabled) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                // You can optionally handle SDL_QUIT here
            }
            if (!framebuffer) {
                std::this_thread::sleep_for(std::chrono::milliseconds(frameDelayMs));
                continue;
            }
            void* pixels;
            int pitch;
            SDL_LockTexture(texture, nullptr, &pixels, &pitch);
            // SPARC stores pixels big-endian [A,R,G,B] in memory; SDL ARGB8888 LE
            // reads byte[0] as B and byte[3] as A, so we must bswap each pixel.
            // Also force A=0xFF since fbcon sets transp=0 (alpha=0) for all colors.
            {
                const uint32_t* src = static_cast<const uint32_t*>(framebuffer);
                uint32_t* dst = static_cast<uint32_t*>(pixels);
                const int stride = pitch / 4;
                for (int y = 0; y < height; y++)
                    for (int x = 0; x < width; x++)
                        dst[y * stride + x] = __builtin_bswap32(src[y * width + x]) | 0xFF000000;
            }
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