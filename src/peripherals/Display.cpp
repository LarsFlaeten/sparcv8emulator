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
            // One-time diagnostic: print first non-zero pixel offset and byte values
            static bool fb_diag_done = false;
            if (!fb_diag_done) {
                fb_diag_done = true;
                const uint8_t* fb = static_cast<const uint8_t*>(framebuffer);
                size_t nonzero_off = SIZE_MAX;
                for (size_t i = 0; i < (size_t)height * width * (bpp/8) && i < 4096; i++) {
                    if (fb[i]) { nonzero_off = i; break; }
                }
                if (nonzero_off == SIZE_MAX)
                    printf("[Display] framebuffer ptr=%p first 4096 bytes are all zero\n", framebuffer);
                else
                    printf("[Display] framebuffer ptr=%p first non-zero byte at offset %zu = 0x%02x; bytes[0..3]=%02x %02x %02x %02x\n",
                           framebuffer, nonzero_off, fb[nonzero_off],
                           fb[nonzero_off], fb[nonzero_off+1], fb[nonzero_off+2], fb[nonzero_off+3]);
            }
            void* pixels;
            int pitch;
            SDL_LockTexture(texture, nullptr, &pixels, &pitch);
            std::memcpy(pixels, framebuffer, (size_t)height * pitch);
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