// SPDX-License-Identifier: MIT
#include "Display.h"
#include <array>
#include <chrono>
#include <cstring>
#include <signal.h>

#include <pthread.h>

// PS/2 Set 2 scan codes indexed by SDL_Scancode.
// 0x0000 = unmapped, 0x00XX = normal (single byte XX),
// 0xE0XX = extended (0xE0 prefix then XX).
// Built once via a lambda — avoids C++ designated-array-initializer issues.
static const std::array<uint16_t, SDL_NUM_SCANCODES>& ps2_set2_table() {
    static const auto tbl = []() {
        std::array<uint16_t, SDL_NUM_SCANCODES> t{};
        t[SDL_SCANCODE_A]            = 0x001C;
        t[SDL_SCANCODE_B]            = 0x0032;
        t[SDL_SCANCODE_C]            = 0x0021;
        t[SDL_SCANCODE_D]            = 0x0023;
        t[SDL_SCANCODE_E]            = 0x0024;
        t[SDL_SCANCODE_F]            = 0x002B;
        t[SDL_SCANCODE_G]            = 0x0034;
        t[SDL_SCANCODE_H]            = 0x0033;
        t[SDL_SCANCODE_I]            = 0x0043;
        t[SDL_SCANCODE_J]            = 0x003B;
        t[SDL_SCANCODE_K]            = 0x0042;
        t[SDL_SCANCODE_L]            = 0x004B;
        t[SDL_SCANCODE_M]            = 0x003A;
        t[SDL_SCANCODE_N]            = 0x0031;
        t[SDL_SCANCODE_O]            = 0x0044;
        t[SDL_SCANCODE_P]            = 0x004D;
        t[SDL_SCANCODE_Q]            = 0x0015;
        t[SDL_SCANCODE_R]            = 0x002D;
        t[SDL_SCANCODE_S]            = 0x001B;
        t[SDL_SCANCODE_T]            = 0x002C;
        t[SDL_SCANCODE_U]            = 0x003C;
        t[SDL_SCANCODE_V]            = 0x002A;
        t[SDL_SCANCODE_W]            = 0x001D;
        t[SDL_SCANCODE_X]            = 0x0022;
        t[SDL_SCANCODE_Y]            = 0x0035;
        t[SDL_SCANCODE_Z]            = 0x001A;
        t[SDL_SCANCODE_1]            = 0x0016;
        t[SDL_SCANCODE_2]            = 0x001E;
        t[SDL_SCANCODE_3]            = 0x0026;
        t[SDL_SCANCODE_4]            = 0x0025;
        t[SDL_SCANCODE_5]            = 0x002E;
        t[SDL_SCANCODE_6]            = 0x0036;
        t[SDL_SCANCODE_7]            = 0x003D;
        t[SDL_SCANCODE_8]            = 0x003E;
        t[SDL_SCANCODE_9]            = 0x0046;
        t[SDL_SCANCODE_0]            = 0x0045;
        t[SDL_SCANCODE_RETURN]       = 0x005A;
        t[SDL_SCANCODE_ESCAPE]       = 0x0076;
        t[SDL_SCANCODE_BACKSPACE]    = 0x0066;
        t[SDL_SCANCODE_TAB]          = 0x000D;
        t[SDL_SCANCODE_SPACE]        = 0x0029;
        t[SDL_SCANCODE_MINUS]        = 0x004E;
        t[SDL_SCANCODE_EQUALS]       = 0x0055;
        t[SDL_SCANCODE_LEFTBRACKET]  = 0x0054;
        t[SDL_SCANCODE_RIGHTBRACKET] = 0x005B;
        t[SDL_SCANCODE_BACKSLASH]    = 0x005D;
        t[SDL_SCANCODE_SEMICOLON]    = 0x004C;
        t[SDL_SCANCODE_APOSTROPHE]   = 0x0052;
        t[SDL_SCANCODE_GRAVE]        = 0x000E;
        t[SDL_SCANCODE_COMMA]        = 0x0041;
        t[SDL_SCANCODE_PERIOD]       = 0x0049;
        t[SDL_SCANCODE_SLASH]        = 0x004A;
        t[SDL_SCANCODE_NONUSBACKSLASH] = 0x0061; // ISO key between LShift and Z (<> on Norwegian)
        t[SDL_SCANCODE_CAPSLOCK]     = 0x0058;
        t[SDL_SCANCODE_F1]           = 0x0005;
        t[SDL_SCANCODE_F2]           = 0x0006;
        t[SDL_SCANCODE_F3]           = 0x0004;
        t[SDL_SCANCODE_F4]           = 0x000C;
        t[SDL_SCANCODE_F5]           = 0x0003;
        t[SDL_SCANCODE_F6]           = 0x000B;
        t[SDL_SCANCODE_F7]           = 0x0083;
        t[SDL_SCANCODE_F8]           = 0x000A;
        t[SDL_SCANCODE_F9]           = 0x0001;
        t[SDL_SCANCODE_F10]          = 0x0009;
        t[SDL_SCANCODE_F11]          = 0x0078;
        t[SDL_SCANCODE_F12]          = 0x0007;
        t[SDL_SCANCODE_SCROLLLOCK]   = 0x007E;
        t[SDL_SCANCODE_INSERT]       = 0xE070;
        t[SDL_SCANCODE_HOME]         = 0xE06C;
        t[SDL_SCANCODE_PAGEUP]       = 0xE07D;
        t[SDL_SCANCODE_DELETE]       = 0xE071;
        t[SDL_SCANCODE_END]          = 0xE069;
        t[SDL_SCANCODE_PAGEDOWN]     = 0xE07A;
        t[SDL_SCANCODE_RIGHT]        = 0xE074;
        t[SDL_SCANCODE_LEFT]         = 0xE06B;
        t[SDL_SCANCODE_DOWN]         = 0xE072;
        t[SDL_SCANCODE_UP]           = 0xE075;
        t[SDL_SCANCODE_LCTRL]        = 0x0014;
        t[SDL_SCANCODE_LSHIFT]       = 0x0012;
        t[SDL_SCANCODE_LALT]         = 0x0011;
        t[SDL_SCANCODE_LGUI]         = 0xE01F;
        t[SDL_SCANCODE_RCTRL]        = 0xE014;
        t[SDL_SCANCODE_RSHIFT]       = 0x0059;
        t[SDL_SCANCODE_RALT]         = 0xE011;
        t[SDL_SCANCODE_RGUI]         = 0xE027;
        return t;
    }();
    return tbl;
}

void Display::handle_key_event(const SDL_KeyboardEvent& e,
                                const std::function<void(uint8_t)>& cb) {
    if (!cb) return;
    SDL_Scancode sc = e.keysym.scancode;
    if (sc >= SDL_NUM_SCANCODES) return;
    uint16_t code = ps2_set2_table()[sc];
    if (code == 0) return;

    bool extended = (code >> 8) == 0xE0;
    uint8_t byte  = code & 0xFF;

    if (e.type == SDL_KEYDOWN) {
        if (extended) cb(0xE0);
        cb(byte);
    } else { // SDL_KEYUP
        if (extended) cb(0xE0);
        cb(0xF0);
        cb(byte);
    }
}

void set_thread_name(const char* name) {
    pthread_setname_np(pthread_self(), name);
}

Display::Display(int width, int height, int bpp, int refreshRateHz, const void* framebuffer, bool fullscreen)
    : width(width), height(height), bpp(bpp), refreshRateHz(refreshRateHz),
      framebuffer(framebuffer), running(false), enabled(false), fullscreen_(fullscreen) {}

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
    if (fullscreen_)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                width, height);
    // Clear texture to black so uninitialized pixels don't show white
    { void* px; int pt; SDL_LockTexture(texture, nullptr, &px, &pt); memset(px, 0, pt * height); SDL_UnlockTexture(texture); }
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

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
            { void* px; int pt; SDL_LockTexture(texture, nullptr, &px, &pt); memset(px, 0, pt * height); SDL_UnlockTexture(texture); }
        }
        lock.unlock();

        SDL_ShowWindow(window);

        while (running && enabled) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
                    continue;
                auto& ke = e.key;
                auto sc  = ke.keysym.scancode;
                bool ctrl = (ke.keysym.mod & KMOD_CTRL) != 0;

                if (escape_pending_) {
                    if (e.type == SDL_KEYUP && sc == SDL_SCANCODE_A && a_keydown_suppressed_) {
                        // Suppress the A keyup that matched the suppressed keydown
                        a_keydown_suppressed_ = false;
                        continue;
                    }
                    if (e.type == SDL_KEYDOWN) {
                        escape_pending_    = false;
                        a_keydown_suppressed_ = false;
                        if (sc == SDL_SCANCODE_X) {
                            raise(SIGTERM);
                            continue;
                        }
                        // Ctrl+A Ctrl+A → forward A normally; Ctrl is already held in guest
                        // Any other key → forward normally
                    }
                    handle_key_event(ke, key_callback_);
                    continue;
                }

                // Ctrl+A → start escape sequence, suppress A keydown
                if (e.type == SDL_KEYDOWN && ctrl && sc == SDL_SCANCODE_A) {
                    escape_pending_       = true;
                    a_keydown_suppressed_ = true;
                    continue;
                }

                handle_key_event(ke, key_callback_);
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