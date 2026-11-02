// OpenAL (Infinity Blade 1): the 25 al / alc functions its UE3 audio device (UALAudioDevice) uses.
// IB2 and IB3 play through the 3D Mixer audio unit (mixer.cpp); IB1 is older and plays through OpenAL,
// which the port did not have: UALAudioDevice::Init waited for a device forever (black screen).
// Sources play buffers (one, or a queue) and are mixed into the same output as everything else
// (mixer.cpp calls openal_mix), with gain, pitch, looping, and a simple 3D pan and distance fade.
#include "audio/mixer.h"
#include "hle.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace audio {

void ensure_output();  // mixer.cpp

namespace {

enum : u32 {
    AL_SOURCE_RELATIVE = 0x202, AL_PITCH = 0x1003, AL_POSITION = 0x1004, AL_DIRECTION = 0x1005,
    AL_VELOCITY = 0x1006, AL_LOOPING = 0x1007, AL_BUFFER = 0x1009, AL_GAIN = 0x100A, AL_MIN_GAIN = 0x100D,
    AL_MAX_GAIN = 0x100E, AL_ORIENTATION = 0x100F, AL_SOURCE_STATE = 0x1010, AL_INITIAL = 0x1011,
    AL_PLAYING = 0x1012, AL_PAUSED = 0x1013, AL_STOPPED = 0x1014, AL_BUFFERS_QUEUED = 0x1015,
    AL_BUFFERS_PROCESSED = 0x1016, AL_REFERENCE_DISTANCE = 0x1020, AL_ROLLOFF_FACTOR = 0x1021,
    AL_MAX_DISTANCE = 0x1023, AL_SOURCE_TYPE = 0x1027, AL_STATIC = 0x1028, AL_STREAMING = 0x1029,
    AL_UNDETERMINED = 0x1030,
    AL_FORMAT_MONO8 = 0x1100, AL_FORMAT_MONO16 = 0x1101, AL_FORMAT_STEREO8 = 0x1102, AL_FORMAT_STEREO16 = 0x1103,
    AL_INVALID_NAME = 0xA001, AL_INVALID_ENUM = 0xA002, AL_INVALID_VALUE = 0xA003, AL_INVALID_OPERATION = 0xA004,
    AL_INVERSE_DISTANCE = 0xD001, AL_INVERSE_DISTANCE_CLAMPED = 0xD002, AL_LINEAR_DISTANCE = 0xD003,
    AL_LINEAR_DISTANCE_CLAMPED = 0xD004, AL_EXPONENT_DISTANCE = 0xD005, AL_EXPONENT_DISTANCE_CLAMPED = 0xD006,
};

struct Buffer {
    std::vector<s16> pcm;  // interleaved
    int channels = 1, rate = 44100;
    u64 frames = 0;
};
struct Source {
    std::deque<u32> queue;  // buffer names (a static source: one)
    size_t current = 0;     // index in queue of the buffer playing
    double pos = 0;         // in its frames
    u32 state = AL_INITIAL;
    bool looping = false, relative = false, is_static = false;
    float gain = 1, pitch = 1, min_gain = 0, max_gain = 1, ref_dist = 1, rolloff = 1, max_dist = 3.4e38f;
    float position[3] = {0, 0, 0};
};

std::mutex g_m;  // everything below: the game's threads and the audio thread
std::map<u32, std::shared_ptr<Buffer>> g_buffers;
std::map<u32, Source> g_sources;
u32 g_next_name = 1;
u32 g_error = 0;
u32 g_model = AL_INVERSE_DISTANCE_CLAMPED;
float g_listener[3] = {0, 0, 0}, g_at[3] = {0, 0, -1}, g_up[3] = {0, 1, 0};
u64 g_device_token = 0x4F70656E414C4445ull, g_context_token = 0x4F70656E414C4358ull;  // (never dereferenced)
std::atomic<int> g_playing_logged{0};

void set_error(u32 e) {
    if (!g_error) g_error = e;
}
std::shared_ptr<Buffer> buffer_of(u32 name) {
    auto it = g_buffers.find(name);
    return it == g_buffers.end() ? nullptr : it->second;
}

float distance_gain(const Source& s, float d) {
    float ref = std::max(s.ref_dist, 1e-4f), mx = s.max_dist, r = s.rolloff;
    switch (g_model) {
    case 0: return 1;  // AL_NONE
    case AL_INVERSE_DISTANCE_CLAMPED: d = std::clamp(d, ref, std::max(ref, mx)); [[fallthrough]];
    case AL_INVERSE_DISTANCE: return ref / (ref + r * (d - ref));
    case AL_LINEAR_DISTANCE_CLAMPED: d = std::clamp(d, ref, std::max(ref, mx)); [[fallthrough]];
    case AL_LINEAR_DISTANCE: return mx > ref ? std::clamp(1 - r * (d - ref) / (mx - ref), 0.0f, 1.0f) : 1.0f;
    case AL_EXPONENT_DISTANCE_CLAMPED: d = std::clamp(d, ref, std::max(ref, mx)); [[fallthrough]];
    case AL_EXPONENT_DISTANCE: return std::pow(std::max(d, 1e-4f) / ref, -r);
    }
    return 1;
}

// left / right gains of a mono source: distance fade, and a pan by where it is around the listener
void spatial(const Source& s, float& gl, float& gr) {
    float v[3];
    for (int i = 0; i < 3; i++) v[i] = s.relative ? s.position[i] : s.position[i] - g_listener[i];
    const float d = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    const float g = distance_gain(s, d);
    // the listener's right: at x up
    const float right[3] = {g_at[1] * g_up[2] - g_at[2] * g_up[1], g_at[2] * g_up[0] - g_at[0] * g_up[2],
                            g_at[0] * g_up[1] - g_at[1] * g_up[0]};
    const float rl = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    float pan = 0;
    if (d > 1e-4f && rl > 1e-6f)
        pan = std::clamp((v[0] * right[0] + v[1] * right[1] + v[2] * right[2]) / (d * rl), -1.0f, 1.0f);
    gl = g * std::cos((pan + 1) * 0.785398f) * 1.41421f;
    gr = g * std::sin((pan + 1) * 0.785398f) * 1.41421f;
}

// ---- alc ----------------------------------------------------------------------------------------
u64 alcOpenDevice(const char*) {
    LOG_INFO("openal: device opened (mixed into the port's output)");
    ensure_output();
    return gaddr(&g_device_token);
}
bool alcCloseDevice(u64) { return true; }
u64 alcCreateContext(u64, const s32*) { return gaddr(&g_context_token); }
bool alcMakeContextCurrent(u64) { return true; }
void alcDestroyContext(u64) {}
void alcProcessContext(u64) {}
void alcSuspendContext(u64) {}

// ---- al -----------------------------------------------------------------------------------------
u32 alGetError() {
    std::lock_guard lock(g_m);
    u32 e = g_error;
    g_error = 0;
    return e;
}
s32 alGetEnumValue(const char* name) {
    static const std::map<std::string, s32> known = {
        {"AL_FORMAT_MONO8", AL_FORMAT_MONO8}, {"AL_FORMAT_MONO16", AL_FORMAT_MONO16},
        {"AL_FORMAT_STEREO8", AL_FORMAT_STEREO8}, {"AL_FORMAT_STEREO16", AL_FORMAT_STEREO16},
    };
    auto it = name ? known.find(name) : known.end();
    return it == known.end() ? 0 : it->second;  // (multichannel formats: none, as on iOS)
}
void alDistanceModel(u32 model) {
    std::lock_guard lock(g_m);
    g_model = model;
}
void alGenBuffers(s32 n, u32* out) {
    std::lock_guard lock(g_m);
    for (s32 i = 0; i < n && out; i++) {
        out[i] = g_next_name++;
        g_buffers[out[i]] = std::make_shared<Buffer>();
    }
}
void alDeleteBuffers(s32 n, const u32* names) {
    std::lock_guard lock(g_m);
    for (s32 i = 0; i < n && names; i++) g_buffers.erase(names[i]);
}
void alBufferData(u32 name, u32 format, const void* data, s32 size, s32 freq) {
    if (format < AL_FORMAT_MONO8 || format > AL_FORMAT_STEREO16 || size < 0 || freq <= 0) {
        std::lock_guard lock(g_m);
        set_error(AL_INVALID_ENUM);
        return;
    }
    auto b = std::make_shared<Buffer>();
    const bool stereo = format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16;
    const bool eight = format == AL_FORMAT_MONO8 || format == AL_FORMAT_STEREO8;
    b->channels = stereo ? 2 : 1;
    b->rate = freq;
    if (eight) {
        b->pcm.resize((size_t)size);
        const u8* p = static_cast<const u8*>(data);
        for (s32 i = 0; i < size && p; i++) b->pcm[i] = (s16)((p[i] - 128) << 8);
    } else {
        b->pcm.resize((size_t)size / 2);
        if (data && size) std::memcpy(b->pcm.data(), data, b->pcm.size() * 2);
    }
    b->frames = b->pcm.size() / b->channels;
    std::lock_guard lock(g_m);
    if (!g_buffers.count(name)) {
        set_error(AL_INVALID_NAME);
        return;
    }
    g_buffers[name] = b;
}
void alGenSources(s32 n, u32* out) {
    std::lock_guard lock(g_m);
    for (s32 i = 0; i < n && out; i++) {
        out[i] = g_next_name++;
        g_sources[out[i]] = Source{};
    }
}
void alDeleteSources(s32 n, const u32* names) {
    std::lock_guard lock(g_m);
    for (s32 i = 0; i < n && names; i++) g_sources.erase(names[i]);
}
Source* source(u32 name) {
    auto it = g_sources.find(name);
    if (it == g_sources.end()) {
        set_error(AL_INVALID_NAME);
        return nullptr;
    }
    return &it->second;
}
void alSourcei(u32 name, u32 param, s32 v) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s) return;
    switch (param) {
    case AL_BUFFER:
        s->queue.clear();
        if (v) s->queue.push_back((u32)v);
        s->current = 0, s->pos = 0, s->is_static = v != 0;
        break;
    case AL_LOOPING: s->looping = v != 0; break;
    case AL_SOURCE_RELATIVE: s->relative = v != 0; break;
    default: break;  // (cones, offsets: not used)
    }
}
void alSourcef(u32 name, u32 param, float v) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s) return;
    switch (param) {
    case AL_GAIN: s->gain = std::max(v, 0.0f); break;
    case AL_PITCH: s->pitch = std::clamp(v, 0.05f, 8.0f); break;
    case AL_MIN_GAIN: s->min_gain = v; break;
    case AL_MAX_GAIN: s->max_gain = v; break;
    case AL_REFERENCE_DISTANCE: s->ref_dist = v; break;
    case AL_ROLLOFF_FACTOR: s->rolloff = v; break;
    case AL_MAX_DISTANCE: s->max_dist = v; break;
    default: break;
    }
}
void alSourcefv(u32 name, u32 param, const float* v) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s || !v) return;
    if (param == AL_POSITION) std::memcpy(s->position, v, sizeof(s->position));
}
void alListenerfv(u32 param, const float* v) {
    if (!v) return;
    std::lock_guard lock(g_m);
    if (param == AL_POSITION) std::memcpy(g_listener, v, sizeof(g_listener));
    else if (param == AL_ORIENTATION) std::memcpy(g_at, v, sizeof(g_at)), std::memcpy(g_up, v + 3, sizeof(g_up));
}
void alGetSourcei(u32 name, u32 param, s32* out) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s || !out) return;
    switch (param) {
    case AL_SOURCE_STATE: *out = (s32)s->state; break;
    case AL_BUFFERS_QUEUED: *out = (s32)s->queue.size(); break;
    case AL_BUFFERS_PROCESSED:  // (a stopped source: all of them)
        *out = s->state == AL_STOPPED ? (s32)s->queue.size() : (s->is_static ? 0 : (s32)s->current);
        break;
    case AL_LOOPING: *out = s->looping; break;
    case AL_BUFFER: *out = s->queue.empty() ? 0 : (s32)s->queue.front(); break;
    case AL_SOURCE_RELATIVE: *out = s->relative; break;
    case AL_SOURCE_TYPE: *out = s->queue.empty() ? AL_UNDETERMINED : s->is_static ? AL_STATIC : AL_STREAMING; break;
    default: *out = 0; break;
    }
}
void alSourcePlay(u32 name) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s) return;
    if (s->state != AL_PAUSED) s->current = 0, s->pos = 0;  // (playing again: from the start)
    s->state = s->queue.empty() ? AL_STOPPED : AL_PLAYING;
    if (g_playing_logged.fetch_add(1) < 3) LOG_INFO("openal: a source plays (%zu buffer(s))", s->queue.size());
}
void alSourcePause(u32 name) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (s && s->state == AL_PLAYING) s->state = AL_PAUSED;
}
void alSourceStop(u32 name) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (s && s->state != AL_INITIAL) s->state = AL_STOPPED, s->pos = 0;
}
void alSourceQueueBuffers(u32 name, s32 n, const u32* names) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s || !names) return;
    if (s->is_static) s->queue.clear(), s->current = 0, s->is_static = false;
    for (s32 i = 0; i < n; i++) s->queue.push_back(names[i]);
}
void alSourceUnqueueBuffers(u32 name, s32 n, u32* names) {
    std::lock_guard lock(g_m);
    Source* s = source(name);
    if (!s || !names) return;
    const size_t processed = s->state == AL_STOPPED ? s->queue.size() : s->current;
    if (n < 0 || (size_t)n > processed) {
        set_error(AL_INVALID_VALUE);
        return;
    }
    for (s32 i = 0; i < n; i++) {
        names[i] = s->queue.front();
        s->queue.pop_front();
    }
    s->current = s->state == AL_STOPPED ? 0 : s->current - (size_t)n;
}

}  // namespace

// The audio thread: every playing source added to mix (stereo, out_rate)
void openal_mix(float* mix, u32 n, int out_rate, float effects_gain) {
    std::lock_guard lock(g_m);
    for (auto& [name, s] : g_sources) {
        if (s.state != AL_PLAYING) continue;
        const float base = std::clamp(s.gain, s.min_gain, s.max_gain) * effects_gain;
        for (u32 i = 0; i < n;) {
            if (s.current >= s.queue.size()) {
                s.state = AL_STOPPED;
                break;
            }
            auto b = buffer_of(s.queue[s.current]);
            if (!b || !b->frames) {  // (empty: the next one)
                s.current++, s.pos = 0;
                continue;
            }
            float gl = 1, gr = 1;
            if (b->channels == 1) spatial(s, gl, gr);
            const double step = (double)b->rate * s.pitch / out_rate;
            for (; i < n && s.pos < b->frames; i++) {
                const u64 f0 = (u64)s.pos, f1 = std::min(f0 + 1, b->frames - 1);
                const float t = (float)(s.pos - f0);
                float l, r;
                if (b->channels == 2) {
                    l = (b->pcm[f0 * 2] * (1 - t) + b->pcm[f1 * 2] * t) / 32768.0f;
                    r = (b->pcm[f0 * 2 + 1] * (1 - t) + b->pcm[f1 * 2 + 1] * t) / 32768.0f;
                } else {
                    l = r = (b->pcm[f0] * (1 - t) + b->pcm[f1] * t) / 32768.0f;
                }
                mix[2 * i] += l * gl * base;
                mix[2 * i + 1] += r * gr * base;
                s.pos += step;
            }
            if (s.pos >= b->frames) {  // this buffer done: loop, the next one, or stop
                s.pos -= b->frames;
                if (s.looping && s.is_static) continue;
                if (s.looping && s.current + 1 >= s.queue.size()) s.current = 0;
                else s.current++;
            }
        }
    }
}

void install_openal() {
    using hle::fn;
    fn("_alcOpenDevice", alcOpenDevice);
    fn("_alcCloseDevice", alcCloseDevice);
    fn("_alcCreateContext", alcCreateContext);
    fn("_alcMakeContextCurrent", alcMakeContextCurrent);
    fn("_alcDestroyContext", alcDestroyContext);
    fn("_alcProcessContext", alcProcessContext);
    fn("_alcSuspendContext", alcSuspendContext);
    fn("_alGetError", alGetError);
    fn("_alGetEnumValue", alGetEnumValue);
    fn("_alDistanceModel", alDistanceModel);
    fn("_alGenBuffers", alGenBuffers);
    fn("_alDeleteBuffers", alDeleteBuffers);
    fn("_alBufferData", alBufferData);
    fn("_alGenSources", alGenSources);
    fn("_alDeleteSources", alDeleteSources);
    fn("_alSourcei", alSourcei);
    fn("_alSourcef", alSourcef);
    fn("_alSourcefv", alSourcefv);
    fn("_alListenerfv", alListenerfv);
    fn("_alGetSourcei", alGetSourcei);
    fn("_alSourcePlay", alSourcePlay);
    fn("_alSourcePause", alSourcePause);
    fn("_alSourceStop", alSourceStop);
    fn("_alSourceQueueBuffers", alSourceQueueBuffers);
    fn("_alSourceUnqueueBuffers", alSourceUnqueueBuffers);
}

}  // namespace audio
