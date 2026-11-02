// Infinity Blade 1: the iOS pieces only IB1 uses (IB2 and IB3 never call them), on the port's own parts:
//   MPMoviePlayerController   IB1's movies (IB2/IB3 use AVPlayer, av.cpp): played by video.cpp
//   UIAccelerometer            asked for at start (tilt): a shared object that sends nothing
//   CGAffineTransformTranslate the movie view's transform
//   glGetBooleanv              GL_SHADER_COMPILER (asked once, when the viewport is made)
// The IB1 files on top of the IB2/3 port (openal.cpp, this file) are Hydroid's (2026-11-02).
#include "audio/video.h"
#include "foundation/foundation.h"
#include "game/game.h"
#include "hle.h"
#include <filesystem>
#include <miniz/miniz.h>
#include <vector>
#include "game/pad.h"
#include <thread>
#include <chrono>
#include "libc/vfs.h"
#include "objc/internal.h"
#include <atomic>
#include <fstream>
#include <iterator>
#include <string>

namespace game {

using objc::Class;
using objc::id;
using objc::SEL;

bool is_ib1() {
    static const bool ib1 = [] {
        std::ifstream f(vfs::host_bundle() + "/Info.plist", std::ios::binary);
        std::string plist((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return plist.find("com.chairentertainment.birthright") != std::string::npos;
    }();
    return ib1;
}

namespace {

extern std::atomic<bool> g_press_when_shown;

struct MovieData : objc::HostData {
    id url = 0, view = 0;
    std::shared_ptr<video::Movie> movie;
    bool repeat = false;
    // MPMoviePlaybackState: 0 stopped (also when it ended), 1 playing, 2 paused. IB1 watches it on a timer
    // (LogoMovieTimer): an always-"playing" answer kept it on its logo screen after the movie had ended.
    std::atomic<int> state{0};
    ~MovieData() override { video::stop(movie); }
};

void post_finished(id player) {
    if (auto* d = objc::get<MovieData>(player)) d->state = 0;
    g_press_when_shown = true;  // (the title screen's Start: pressed once it is there)
    LOG_INFO("ib1: movie finished: MPMoviePlayerPlaybackDidFinishNotification");
    objc::retain(player);
    ns::post_to_main([player] {
        id center = objc::send(objc::class_named("NSNotificationCenter"), "defaultCenter");
        objc::send(center, "postNotificationName:object:", {ns::str("MPMoviePlayerPlaybackStateDidChangeNotification"), player});
        objc::send(center, "postNotificationName:object:", {ns::str("MPMoviePlayerPlaybackDidFinishNotification"), player});
        objc::release(player);
    });
}


// The title screen's Start (a UIImageView the port doesn't draw, pressed by a touch the port doesn't send):
// shown over the movie (ib1_draw_start), pressed with the pad's A / cross or Start / options, and pressed
// by itself when the logo movie ends (no waiting on a screen with nothing on it).
std::atomic<bool> g_start_shown{false};
std::atomic<bool> g_press_when_shown{false};

bool start_on_screen() {  // (main thread)
    id app = objc::send(objc::class_named("UIApplication"), "sharedApplication");
    id delegate = app ? objc::send(app, "delegate") : 0;
    return delegate && objc::responds_to(delegate, objc::sel("StartButton")) && objc::send(delegate, "StartButton");
}
void press_start(const char* why) {  // (main thread)
    if (!start_on_screen()) return;
    LOG_INFO("ib1: Start pressed (%s)", why);
    g_start_shown = false, g_press_when_shown = false;
    id delegate = objc::send(objc::send(objc::class_named("UIApplication"), "sharedApplication"), "delegate");
    objc::send(delegate, "OnStartButtonPressed");
}
void start_button_watch() {
    static std::atomic<bool> running{false};
    if (running.exchange(true)) return;
    std::thread([] {
        pad::State was;
        pad::read(was);
        for (int tick = 0;; tick++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (tick % 5 == 0)  // is it there (main thread's views)
                ns::post_to_main([] {
                    const bool on = start_on_screen();
                    if (on && !g_start_shown.exchange(true)) LOG_INFO("ib1: the title screen's Start is shown");
                    if (!on) g_start_shown = false;
                    if (on && g_press_when_shown) press_start("after the logo movie");
                });
            pad::State p;
            pad::read(p);
            const u16 pressed = p.buttons & ~was.buttons & (pad::kA | pad::kStart);
            was = p;
            if (pressed && g_start_shown) ns::post_to_main([] { press_start("pad"); });
        }
    }).detach();
}

// Start.png (512 x 256, 8-bit RGBA, from the game's bundle): decoded once
struct Picture { std::vector<u8> rgba; int w = 0, h = 0; };
const Picture& start_picture() {
    static Picture pic = [] {
        Picture p;
        std::ifstream f(vfs::host_bundle() + "/Start.png", std::ios::binary);
        std::vector<u8> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() < 33 || std::memcmp(d.data() + 12, "IHDR", 4)) return p;
        auto be = [&](size_t o) { return (u32)d[o] << 24 | (u32)d[o + 1] << 16 | (u32)d[o + 2] << 8 | d[o + 3]; };
        const int w = (int)be(16), h = (int)be(20);
        if (d[24] != 8 || d[25] != 6 || d[28] != 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096) return p;  // RGBA8 only
        std::vector<u8> z;
        for (size_t o = 8; o + 12 <= d.size();) {
            const u32 n = be(o);
            if (!std::memcmp(d.data() + o + 4, "IDAT", 4)) z.insert(z.end(), d.begin() + o + 8, d.begin() + o + 8 + n);
            o += 12 + n;
        }
        const size_t stride = (size_t)w * 4;
        std::vector<u8> raw((stride + 1) * h);
        mz_ulong out = (mz_ulong)raw.size();
        if (mz_uncompress(raw.data(), &out, z.data(), (mz_ulong)z.size()) != MZ_OK || out != raw.size()) return p;
        p.rgba.resize(stride * h);
        for (int y = 0; y < h; y++) {  // the PNG row filters
            const u8 type = raw[y * (stride + 1)];
            const u8* in = &raw[y * (stride + 1) + 1];
            u8* row = &p.rgba[y * stride];
            const u8* up = y ? &p.rgba[(y - 1) * stride] : nullptr;
            for (size_t i = 0; i < stride; i++) {
                const int a = i >= 4 ? row[i - 4] : 0, b = up ? up[i] : 0, c = (up && i >= 4) ? up[i - 4] : 0;
                int v = in[i];
                if (type == 1) v += a;
                else if (type == 2) v += b;
                else if (type == 3) v += (a + b) / 2;
                else if (type == 4) {
                    const int pp = a + b - c, pa = std::abs(pp - a), pb = std::abs(pp - b), pc = std::abs(pp - c);
                    v += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                }
                row[i] = (u8)v;
            }
        }
        p.w = w, p.h = h;
        LOG_INFO("ib1: Start.png %dx%d", w, h);
        return p;
    }();
    return pic;
}

struct AffineTransform { double a, b, c, d, tx, ty; };

}  // namespace

bool ib1_start_shown() { return is_ib1() && g_start_shown.load(); }
const u8* ib1_start_rgba(int& w, int& h) {
    const Picture& p = start_picture();
    if (p.rgba.empty()) return nullptr;
    w = p.w, h = p.h;
    return p.rgba.data();
}

// Start.png on the movie frame: its middle, in the lower part, a third of the frame wide, alpha-blended
bool ib1_draw_start(u8* rgba, int w, int h) {
    const Picture& p = start_picture();
    if (p.rgba.empty() || w <= 0 || h <= 0) return false;
    const int dw = w * 2 / 5, dh = dw * p.h / p.w, x0 = (w - dw) / 2, y0 = h - dh - h / 12;
    for (int y = 0; y < dh; y++) {
        if (y0 + y < 0 || y0 + y >= h) continue;
        const u8* src_row = &p.rgba[(size_t)(y * p.h / dh) * p.w * 4];
        u8* dst = &rgba[((size_t)(y0 + y) * w + x0) * 4];
        const float fy = std::min((float)(p.h - 1), (y + 0.5f) * p.h / dh - 0.5f);
        const int ya = std::max(0, (int)fy), yb = std::min(ya + 1, p.h - 1);
        const float ty = std::max(0.0f, fy - ya);
        (void)src_row;
        for (int x = 0; x < dw; x++, dst += 4) {
            const float fx = std::min((float)(p.w - 1), (x + 0.5f) * p.w / dw - 0.5f);
            const int xa = std::max(0, (int)fx), xb = std::min(xa + 1, p.w - 1);
            const float tx = std::max(0.0f, fx - xa);
            float px[4];
            for (int c = 0; c < 4; c++) {
                auto at = [&](int xx, int yy) { return (float)p.rgba[((size_t)yy * p.w + xx) * 4 + c]; };
                px[c] = (at(xa, ya) * (1 - tx) + at(xb, ya) * tx) * (1 - ty) + (at(xa, yb) * (1 - tx) + at(xb, yb) * tx) * ty;
            }
            const float a = px[3] / 255.0f;
            for (int c = 0; c < 3; c++) dst[c] = (u8)(px[c] * a + dst[c] * (1 - a) + 0.5f);
        }
    }
    return true;
}

// IB1's picture settings: its config (CookedIPhone/Coalesced_<language>.bin) served with
//   - every TEXTUREGROUP at LODBias=0: the iPhone 6 Plus profile it uses keeps weapons at 1/8 and
//     vehicles at 1/4 of their size (every Apple profile does); the engine clamps to what is cooked
//   - MaxAnisotropy 8 instead of 2 (floors and walls at an angle)
// Same-length edits only (the file's strings are length-prefixed): the layout stays as the game reads it.
void ib1_picture_settings() {
    namespace fs = std::filesystem;
    const std::string src_dir = vfs::host_bundle() + "/CookedIPhone";
    const std::string out_dir = vfs::host_home() + "/Library/Caches/port/coalesced";
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    auto u16 = [](const std::string& a) {
        std::string o;
        for (char c : a) o += c, o += (char)0;
        return o;
    };
    int files = 0, lods = 0, anis = 0;
    for (const auto& e : fs::directory_iterator(src_dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.rfind("Coalesced_", 0) != 0 || name.size() < 5 || name.substr(name.size() - 4) != ".bin") continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        for (char b : std::string("1234")) {  // LODBias=N -> LODBias=0
            const std::string from = u16(std::string("LODBias=") + b), to = u16("LODBias=0");
            for (size_t at = d.find(from); at != std::string::npos; at = d.find(from, at + 1)) d.replace(at, from.size(), to), lods++;
        }
        const std::string key = u16("MaxAnisotropy") + std::string(2, '\0');  // key, its null, then the value: len -2, digit, null
        for (size_t at = d.find(key); at != std::string::npos; at = d.find(key, at + 1)) {
            const size_t v = at + key.size();
            if (v + 8 <= d.size() && (u8)d[v] == 0xfe && (u8)d[v + 1] == 0xff && d[v + 4] >= '1' && d[v + 4] <= '7' && d[v + 5] == 0)
                d[v + 4] = '8', anis++;
        }
        const std::string out = out_dir + "/" + name;
        std::ofstream(out, std::ios::binary) << d;
        vfs::override_bundle_file("CookedIPhone/" + name, out);
        files++;
    }
    LOG_INFO("ib1: picture settings: %d config file(s), %d texture groups at full size, %d anisotropy x8", files, lods, anis);
}

void install_ib1_apis() {
    using objc::class_method;
    using objc::method;

    // ---- MPMoviePlayerController ----
    Class MP = objc::host_class("MPMoviePlayerController");
    method(MP, "initWithContentURL:", [](id self, SEL, id url) -> id {
        auto& d = objc::ensure<MovieData>(self);
        d.url = objc::retain(url);
        std::string path = ns::utf8(objc::send(url, "path"));
        d.movie = video::open(vfs::to_host(path.c_str()));
        LOG_INFO("ib1: movie %s%s", path.c_str(), d.movie ? "" : " (cannot be played: reported as finished)");
        return self;
    });
    method(MP, "play", [](id self, SEL) {
        auto& d = objc::ensure<MovieData>(self);
        if (!d.movie) {
            post_finished(self);
            return;
        }
        auto movie = d.movie;
        video::set_on_finished(movie, [self] {
            auto* md = objc::get<MovieData>(self);
            if (md && md->repeat && md->movie) {
                video::restart(md->movie);
                return;
            }
            post_finished(self);
        });
        objc::ensure<MovieData>(self).state = 1;
        LOG_INFO("ib1: movie play");
        video::play(movie);
        start_button_watch();
    });
    method(MP, "pause", [](id self, SEL) {
        auto& d = objc::ensure<MovieData>(self);
        video::pause(d.movie);
        d.state = 2;
        LOG_INFO("ib1: movie pause");
    });
    method(MP, "stop", [](id self, SEL) {
        auto& d = objc::ensure<MovieData>(self);
        if (d.movie) video::set_on_finished(d.movie, nullptr), video::stop(d.movie);
        d.state = 0;
        LOG_INFO("ib1: movie stop");
    });
    method(MP, "view", [](id self, SEL) -> id {
        auto& d = objc::ensure<MovieData>(self);
        if (!d.view) d.view = objc::send(objc::alloc(objc::class_named("UIView")), "init");
        return d.view;
    });
    method(MP, "setRepeatMode:", [](id self, SEL, s64 mode) { objc::ensure<MovieData>(self).repeat = mode != 0; });
    method(MP, "currentPlaybackTime", [](id self, SEL) -> double { return video::time(objc::ensure<MovieData>(self).movie); });
    method(MP, "duration", [](id self, SEL) -> double { return video::duration(objc::ensure<MovieData>(self).movie); });
    method(MP, "playbackState", [](id self, SEL) -> s64 {
        static int told;
        const int st = objc::ensure<MovieData>(self).state.load();
        if (told++ < 5) LOG_INFO("ib1: movie playbackState -> %d", st);
        return st;
    });
    method(MP, "loadState", [](id, SEL) -> s64 { return 3; });      // playable | playthrough OK
    for (const char* sel : {"prepareToPlay"}) method(MP, sel, [](id, SEL) {});
    method(MP, "setControlStyle:", [](id, SEL, s64) {});
    method(MP, "setScalingMode:", [](id, SEL, s64) {});
    method(MP, "setMovieControlMode:", [](id, SEL, s64) {});
    method(MP, "setFullscreen:", [](id, SEL, bool) {});
    method(MP, "setFullscreen:animated:", [](id, SEL, bool, bool) {});
    method(MP, "setShouldAutoplay:", [](id, SEL, bool) {});
    method(MP, "setUseApplicationAudioSession:", [](id, SEL, bool) {});
    method(MP, "setBackgroundColor:", [](id, SEL, id) {});
    method(MP, "setInitialPlaybackTime:", [](id, SEL, double) {});

    // ---- UIAccelerometer: there, quiet ----
    Class ACC = objc::host_class("UIAccelerometer");
    class_method(ACC, "sharedAccelerometer", [](Class c, SEL) -> id {
        static id shared = 0;
        if (!shared) shared = objc::send(objc::alloc(c), "init");
        return shared;
    });
    method(ACC, "setUpdateInterval:", [](id, SEL, double) {});
    method(ACC, "setDelegate:", [](id, SEL, id) {});
    method(ACC, "delegate", [](id, SEL) -> id { return 0; });

    // ---- CGAffineTransformTranslate(t, tx, ty): t by reference (48 bytes), the result to x8 ----
    hle::raw("_CGAffineTransformTranslate", [](cpu::Thread& t) {
        AffineTransform m = *gptr<AffineTransform>(t.x(0));
        const double tx = t.d(0), ty = t.d(1);
        m.tx += m.a * tx + m.c * ty;
        m.ty += m.b * tx + m.d * ty;
        *gptr<AffineTransform>(t.x(8)) = m;
    });

    // ---- glGetBooleanv: GL_SHADER_COMPILER yes, the rest false ----
    hle::fn("_glGetBooleanv", [](u32 pname, u8* out) {
        if (out) *out = pname == 0x8DFA ? 1 : 0;
    });
}

}  // namespace game
