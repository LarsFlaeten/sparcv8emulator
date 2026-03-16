// SPDX-License-Identifier: MIT
#pragma once
#include <SDL2/SDL.h>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>





class Display {
public:
    Display(int width, int height, int bpp, int refreshRateHz, const void* framebuffer = nullptr);
    ~Display();

    void start();       // Start render thread
    void enable();      // Show window and start/resume rendering
    void disable(bool hideWindow);    // Pause rendering: either hide or blank
    void stop();        // Stop thread and destroy window

    void set_framebuffer(const void* fb) {framebuffer = fb;}
    void set_resolution(int w, int h) {
        std::lock_guard<std::mutex> lock(mtx);
        width = w;
        height = h;
        resolution_changed = true;
    }

    bool isRunning() const;
    bool isEnabled() const;

private:
    void renderLoop();

    int width, height, bpp, refreshRateHz;
    std::atomic<bool> resolution_changed{false};
    const void* framebuffer;

    std::thread renderThread;
    std::atomic<bool> running;
    std::atomic<bool> enabled;
    std::atomic<bool> hideWindowWhenDisabled;
    std::mutex mtx;
    std::condition_variable cv;
};