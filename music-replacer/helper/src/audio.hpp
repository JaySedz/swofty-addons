// Song conversion: any file Windows' Media Foundation decodes (mp3, m4a/aac, wav, wma, flac) to 48 kHz
// 16-bit stereo, then to a Wwise Opus .wem, the format the game's own songs use.
#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace audio {

using Bytes = std::vector<uint8_t>;

inline constexpr uint32_t kRate = 48000, kChannels = 2, kBlock = 4;

template <class T> void put(Bytes &b, size_t o, T v) { std::memcpy(b.data() + o, &v, sizeof v); }
template <class T> void append(Bytes &b, T v)
{
    auto p = reinterpret_cast<const uint8_t *>(&v);
    b.insert(b.end(), p, p + sizeof v);
}

// Media Foundation, started once per process
inline void start()
{
    static bool started = [] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(MFStartup(MF_VERSION));
    }();
    if (!started)
        throw std::runtime_error("Windows' Media Foundation did not start");
}

template <class T> struct Com {
    T *p = nullptr;
    ~Com()
    {
        if (p)
            p->Release();
    }
    T **operator&() { return &p; }
    T *operator->() { return p; }
};

// Decodes `song` to 48 kHz 16-bit stereo PCM. A song that would clip is scaled down to fit; nothing else
// about its loudness changes. `progress` gets 0..1 while decoding (by position).
template <class Progress> Bytes decode(const std::filesystem::path &song, Progress &&progress)
{
    start();
    Com<IMFSourceReader> reader;
    if (FAILED(MFCreateSourceReaderFromURL(song.wstring().c_str(), nullptr, &reader)))
        throw std::runtime_error("Windows cannot open it as audio");
    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    // float PCM at the song's own rate and channel count; stereo and 48 kHz are made below
    {
        Com<IMFMediaType> want;
        MFCreateMediaType(&want);
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, want.p)))
            throw std::runtime_error("Windows cannot decode it");
    }
    UINT32 rate = 0, chans = 0;
    {
        Com<IMFMediaType> got;
        if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &got)))
            throw std::runtime_error("Windows cannot decode it");
        got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &chans);
    }
    if (!rate || !chans)
        throw std::runtime_error("its audio format is unreadable");
    double total = 0;  // 100 ns units
    {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &v)))
            total = static_cast<double>(v.uhVal.QuadPart);
        PropVariantClear(&v);
    }

    std::vector<float> in;
    for (;;) {
        DWORD flags = 0;
        LONGLONG at = 0;
        Com<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, &at, &sample)))
            throw std::runtime_error("decoding failed part way through");
        if (sample.p) {
            Com<IMFMediaBuffer> buf;
            sample->ConvertToContiguousBuffer(&buf);
            BYTE *p = nullptr;
            DWORD n = 0;
            buf->Lock(&p, nullptr, &n);
            auto f = reinterpret_cast<const float *>(p);
            in.insert(in.end(), f, f + n / sizeof(float));
            buf->Unlock();
            if (total > 0)
                progress(std::min(1.0, at / total));
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
            break;
    }

    // to stereo: mono is doubled; more than two channels keep the front pair
    size_t frames = in.size() / chans;
    if (frames < rate)
        throw std::runtime_error("it is under a second long");
    std::vector<float> st(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        st[2 * i] = in[i * chans];
        st[2 * i + 1] = in[i * chans + (chans > 1 ? 1 : 0)];
    }
    in.clear();
    in.shrink_to_fit();

    // to 48 kHz: a windowed sinc, 16 taps a side
    size_t out_frames = static_cast<size_t>(static_cast<double>(frames) * kRate / rate);
    std::vector<float> res(out_frames * 2);
    if (rate == kRate) {
        res = std::move(st);
        res.resize(out_frames * 2);
    } else {
        const double step = static_cast<double>(rate) / kRate, cutoff = std::min(1.0, 1.0 / step);
        const int taps = 16;
        const double pi = 3.14159265358979323846;
        for (size_t o = 0; o < out_frames; ++o) {
            double pos = o * step;
            long base = static_cast<long>(pos);
            double acc[2] = {0, 0}, wsum = 0;
            for (int k = -taps + 1; k <= taps; ++k) {
                long idx = base + k;
                if (idx < 0 || idx >= static_cast<long>(frames))
                    continue;
                double d = idx - pos, x = d * cutoff;
                double sinc = std::fabs(x) < 1e-9 ? 1.0 : std::sin(pi * x) / (pi * x);
                double w = sinc * (0.5 + 0.5 * std::cos(pi * d / taps));
                acc[0] += st[idx * 2] * w;
                acc[1] += st[idx * 2 + 1] * w;
                wsum += w;
            }
            res[o * 2] = static_cast<float>(wsum ? acc[0] / wsum : 0);
            res[o * 2 + 1] = static_cast<float>(wsum ? acc[1] / wsum : 0);
        }
    }
    float peak = 0;
    for (float v : res)
        peak = std::max(peak, std::fabs(v));
    float gain = peak > 1.0f ? 1.0f / peak : 1.0f;
    Bytes pcm(res.size() * 2);
    for (size_t i = 0; i < res.size(); ++i) {
        long v = std::lround(res[i] * gain * 32767.0f);
        put<int16_t>(pcm, i * 2, static_cast<int16_t>(std::clamp(v, -32768L, 32767L)));
    }
    return pcm;
}

// ---- Opus ---------------------------------------------------------------------------------------------
//
// The game's songs are Wwise Opus (codec 0x3041), a RIFF with
//   fmt   36 bytes: codec, channels, 48000 Hz, bytes per second, 0, 0, cbSize 18, 960 samples a frame, the
//         channel layout (0x3102: stereo), sample count, packet count, pre-skip, version 1, mapping family 0
//   hash  16 bytes: an MD5 of the packets (the game doesn't check it)
//   seek  each packet's size as a u16
//   data  the raw Opus packets, back to back
// opusenc (opus-tools, bundled) encodes the PCM to Ogg Opus at 20 ms frames; its packets are repacked so.

inline constexpr uint16_t kCodecOpus = 0x3041;
inline constexpr int kPrefetchPackets = 11;  // the game keeps its songs' header and first 11 packets in memory

// a tiny MD5 (RFC 1321), for the hash chunk
inline void md5(const uint8_t *msg, size_t len, uint8_t out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af,
        0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
        0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97,
        0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t total = ((len + 8) / 64 + 1) * 64;
    auto block = [&](const uint8_t *p) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = p[i * 4] | p[i * 4 + 1] << 8 | p[i * 4 + 2] << 16 | static_cast<uint32_t>(p[i * 4 + 3]) << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) f = (b & c) | (~b & d), g = i;
            else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
            else f = c ^ (b | ~d), g = (7 * i) % 16;
            uint32_t t = d;
            d = c;
            c = b;
            uint32_t x = a + f + K[i] + w[g];
            b += (x << R[i]) | (x >> (32 - R[i]));
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    };
    size_t full = len / 64 * 64;
    for (size_t i = 0; i < full; i += 64)
        block(msg + i);
    uint8_t tail[128] = {};
    size_t rest = len - full;
    std::memcpy(tail, msg + full, rest);
    tail[rest] = 0x80;
    size_t tl = total - full;
    uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; ++i)
        tail[tl - 8 + i] = static_cast<uint8_t>(bits >> (8 * i));
    block(tail);
    if (tl == 128)
        block(tail + 64);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i * 4 + j] = static_cast<uint8_t>(h[i] >> (8 * j));
}

// Runs `exe args`, writing `in` to its stdin and returning its stdout. `fed` gets the share of `in` written
// so far (0..1), from the writing thread.
inline Bytes run_piped(const std::filesystem::path &exe, const std::wstring &args, const Bytes &in,
                       std::function<void(double)> fed = {})
{
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE in_r, in_w, out_r, out_w;
    if (!CreatePipe(&in_r, &in_w, &sa, 1 << 20) || !CreatePipe(&out_r, &out_w, &sa, 1 << 20))
        throw std::runtime_error("cannot start the Opus encoder");
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = nul;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        throw std::runtime_error("cannot start the Opus encoder (bin/opus/opusenc.exe)");
    }
    struct Feed {
        HANDLE h;
        const Bytes *b;
        std::function<void(double)> *fed;
    } feed{in_w, &in, &fed};
    HANDLE writer = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        auto *f = static_cast<Feed *>(p);
        size_t at = 0;
        while (at < f->b->size()) {
            DWORD n = 0, want = static_cast<DWORD>(std::min<size_t>(f->b->size() - at, 1 << 20));
            if (!WriteFile(f->h, f->b->data() + at, want, &n, nullptr))
                break;
            at += n;
            if (*f->fed)
                (*f->fed)(static_cast<double>(at) / f->b->size());
        }
        CloseHandle(f->h);
        return 0;
    }, &feed, 0, nullptr);
    Bytes out;
    uint8_t buf[1 << 16];
    for (DWORD n; ReadFile(out_r, buf, sizeof buf, &n, nullptr) && n;)
        out.insert(out.end(), buf, buf + n);
    CloseHandle(out_r);
    WaitForSingleObject(writer, INFINITE);
    CloseHandle(writer);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0)
        throw std::runtime_error("the Opus encoder failed");
    return out;
}

// Encodes 48 kHz 16-bit stereo PCM to a Wwise Opus .wem at `kbps`. `progress` gets 0..1 as it goes.
inline Bytes make_opus_wem(const Bytes &pcm, int kbps, const std::filesystem::path &opusenc,
                           std::function<void(double)> progress = {})
{
    Bytes ogg = run_piped(opusenc,
                          L"--quiet --raw --raw-rate 48000 --raw-chan 2 --raw-bits 16 --raw-endianness 0 --bitrate " +
                              std::to_wstring(kbps) + L" --framesize 20 - -",
                          pcm, std::move(progress));
    // the Ogg stream's packets: OpusHead, OpusTags, then audio
    std::vector<std::pair<size_t, size_t>> packets;  // offset into `joined`, size
    Bytes joined;
    size_t cur = joined.size();
    for (size_t p = 0; p + 27 <= ogg.size();) {
        if (std::memcmp(&ogg[p], "OggS", 4))
            throw std::runtime_error("the Opus encoder's output is damaged");
        int nseg = ogg[p + 26];
        size_t q = p + 27 + nseg;
        for (int s = 0; s < nseg; ++s) {
            int len = ogg[p + 27 + s];
            joined.insert(joined.end(), ogg.begin() + q, ogg.begin() + q + len);
            q += len;
            if (len < 255) {
                packets.push_back({cur, joined.size() - cur});
                cur = joined.size();
            }
        }
        p = q;
    }
    if (packets.size() < 3 || std::memcmp(&joined[packets[0].first], "OpusHead", 8))
        throw std::runtime_error("the Opus encoder's output is not Opus");
    uint16_t preskip;
    std::memcpy(&preskip, &joined[packets[0].first + 10], 2);
    Bytes data, seek;
    uint32_t count = 0;
    for (size_t i = 2; i < packets.size(); ++i) {
        auto [off, len] = packets[i];
        if (!len)
            continue;
        if (len > 0xFFFF)
            throw std::runtime_error("an Opus packet is too large");
        data.insert(data.end(), joined.begin() + off, joined.begin() + off + len);
        append<uint16_t>(seek, static_cast<uint16_t>(len));
        ++count;
    }
    uint32_t samples = static_cast<uint32_t>(pcm.size() / kBlock);
    Bytes w;
    auto tag = [&](const char *t, uint32_t n) {
        w.insert(w.end(), t, t + 4);
        append<uint32_t>(w, n);
    };
    w.insert(w.end(), {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'});
    tag("fmt ", 36);
    append<uint16_t>(w, kCodecOpus);
    append<uint16_t>(w, kChannels);
    append<uint32_t>(w, kRate);
    append<uint32_t>(w, static_cast<uint32_t>(data.size() * static_cast<double>(kRate) / samples));
    append<uint16_t>(w, 0);
    append<uint16_t>(w, 0);
    append<uint16_t>(w, 18);
    append<uint16_t>(w, 960);
    append<uint32_t>(w, 0x3102);
    append<uint32_t>(w, samples);
    append<uint32_t>(w, count);
    append<uint16_t>(w, preskip);
    w.push_back(1);
    w.push_back(0);
    tag("hash", 16);
    uint8_t digest[16];
    md5(data.data(), data.size(), digest);
    w.insert(w.end(), digest, digest + 16);
    tag("seek", static_cast<uint32_t>(seek.size()));
    w.insert(w.end(), seek.begin(), seek.end());
    tag("data", static_cast<uint32_t>(data.size()));
    w.insert(w.end(), data.begin(), data.end());
    put<uint32_t>(w, 4, static_cast<uint32_t>(w.size() - 8));
    return w;
}

// ---- reading our .wem files -------------------------------------------------------------------------------

struct WemInfo {
    uint16_t codec = 0;
    double ms = 0;        // its length
    size_t prefetch = 0;  // what the game keeps in memory when it streams: the header and 11 packets
};

// What one of our Opus .wem files holds (pass the whole file, or at least its header and first 11 packets).
// codec 0: not one.
inline WemInfo wem_info(const uint8_t *h, size_t n)
{
    WemInfo w;
    // RIFF, size, WAVE, "fmt ", 36, then the format: codec at 20, the sample count at 44
    if (n < 56 || std::memcmp(h, "RIFF", 4) || std::memcmp(h + 8, "WAVEfmt ", 8))
        return w;
    uint16_t codec;
    uint32_t a, b;
    std::memcpy(&codec, h + 20, 2);
    if (codec != kCodecOpus)
        return w;
    std::memcpy(&a, h + 44, 4);  // the sample count
    w.ms = 1000.0 * a / kRate;
    // the chunks after fmt: hash, seek, data
    size_t p = 12;
    const uint8_t *seek = nullptr;
    uint32_t seek_n = 0;
    while (p + 8 <= n) {
        std::memcpy(&b, h + p + 4, 4);
        if (!std::memcmp(h + p, "seek", 4)) {
            seek = h + p + 8;
            seek_n = b / 2;
        } else if (!std::memcmp(h + p, "data", 4)) {
            if (!seek)
                return w;
            size_t pf = p + 8;
            for (uint32_t i = 0; i < seek_n && i < static_cast<uint32_t>(kPrefetchPackets); ++i) {
                uint16_t s;
                std::memcpy(&s, seek + 2 * i, 2);
                pf += s;
            }
            w.prefetch = pf;
            w.codec = codec;
            return w;
        }
        p += 8 + b + (b & 1);
    }
    return w;
}

inline double wem_ms(const uint8_t *h, size_t n) { return wem_info(h, n).ms; }

}  // namespace audio
