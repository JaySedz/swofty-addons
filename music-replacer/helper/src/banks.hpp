// The game's Wwise banks (bank version 150): metadata.bnk holds the music's structure, music.bnk its media.
//
// A menu song is a leaf of a music switch: the switch value music_soundtrack_NN or music_theme_NN names a
// segment, the segment holds one music track, the track plays one source (a media id). A soundtrack song
// streams from streams/<id>.wem and keeps its opening (the prefetch) in music.bnk; a theme piece lives whole
// in music.bnk. Replacing one, with an Opus .wem of our own:
//   metadata.bnk   the track: the in-memory size, the length (and no end trim); the segment: its
//                  length fields, the preview marker at the same share of the song, the exit marker (where
//                  the next song takes over) at the same distance from the end
//   music.bnk      the media entry: the new song's opening (a streamed song) or the whole song (a theme piece)
//   streams        the whole song as streams/<id>.wem (a streamed song)
// Objects are found by scanning for known ids and names rather than parsing every v150 layout; only fields
// of fixed size are rewritten, so metadata.bnk keeps its size and layout.
#pragma once

#include "audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace banks {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

template <class T> T get(const Bytes &b, size_t o)
{
    T v;
    std::memcpy(&v, b.data() + o, sizeof v);
    return v;
}
template <class T> void put(Bytes &b, size_t o, T v) { std::memcpy(b.data() + o, &v, sizeof v); }

inline Bytes read_file(const fs::path &p, size_t limit = SIZE_MAX)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot read " + p.filename().string());
    f.seekg(0, std::ios::end);
    size_t n = std::min<size_t>(static_cast<size_t>(f.tellg()), limit);
    f.seekg(0);
    Bytes b(n);
    f.read(reinterpret_cast<char *>(b.data()), n);
    if (!f)
        throw std::runtime_error("cannot read " + p.filename().string());
    return b;
}

struct Chunk {
    size_t at = 0, size = 0;  // the payload
};
inline std::map<std::string, Chunk> chunks(const Bytes &b)
{
    std::map<std::string, Chunk> out;
    for (size_t p = 0; p + 8 <= b.size();) {
        uint32_t n = get<uint32_t>(b, p + 4);
        if (p + 8 + n > b.size())
            break;
        out[std::string(reinterpret_cast<const char *>(&b[p]), 4)] = {p + 8, n};
        p += 8 + n;
    }
    return out;
}

struct Obj {
    uint8_t type;
    uint32_t id;
    size_t at, size;  // at = the object's id; size counts from there
};
inline std::vector<Obj> hirc(const Bytes &b)
{
    auto c = chunks(b);
    if (!c.count("HIRC"))
        throw std::runtime_error("metadata.bnk has no HIRC");
    size_t q = c["HIRC"].at, end = q + c["HIRC"].size;
    uint32_t n = get<uint32_t>(b, q);
    q += 4;
    std::vector<Obj> out;
    for (uint32_t i = 0; i < n && q + 9 <= end; ++i) {
        Obj o{b[q], get<uint32_t>(b, q + 5), q + 5, get<uint32_t>(b, q + 1)};
        out.push_back(o);
        q += 5 + o.size;
    }
    return out;
}

// Wwise's name hash: 32-bit FNV-1 of the lower-cased name
inline uint32_t fnv(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h *= 16777619u;
        h ^= static_cast<unsigned char>(std::tolower(c));
    }
    return h;
}

enum Kind { soundtrack, theme };
struct Group {
    const char *value, *exit;
};
inline constexpr Group kGroups[] = {
    {"music_soundtrack_%02d", "soundtrack_exit"},
    {"music_theme_%02d", "theme_exit"},
};

inline constexpr uint32_t kOpus = 0x00140001;  // the tracks' codec: Wwise Opus, as ours are

struct Slot {
    int number = 0;
    uint32_t segment = 0, track = 0, source = 0;
    size_t source_at = 0;   // where the track holds the source id
    bool embedded = false;  // the whole song is in music.bnk (else it streams, its opening there)
};

inline std::vector<Slot> find_slots(const Bytes &m, const std::vector<Obj> &objs, Kind kind)
{
    std::map<uint32_t, const Obj *> by;
    for (auto &o : objs)
        by[o.id] = &o;
    std::map<uint32_t, int> value;
    for (int i = 1; i <= 60; ++i) {
        char s[48];
        std::snprintf(s, sizeof s, kGroups[kind].value, i);
        value[fnv(s)] = i;
    }
    std::vector<Slot> out;
    for (auto &sw : objs) {
        if (sw.type != 12)  // music switch
            continue;
        for (size_t i = sw.at + 4; i + 8 <= sw.at + sw.size; ++i) {
            auto v = value.find(get<uint32_t>(m, i));
            if (v == value.end())
                continue;
            auto seg = by.find(get<uint32_t>(m, i + 4));
            if (seg == by.end() || seg->second->type != 10)  // music segment
                continue;
            Slot s;
            s.number = v->second;
            s.segment = seg->first;
            const Obj &so = *seg->second;
            for (size_t j = so.at + 4; j + 4 <= so.at + so.size && !s.track; ++j) {
                auto t = by.find(get<uint32_t>(m, j));
                if (t != by.end() && t->second->type == 11)  // music track
                    s.track = t->first;
            }
            if (!s.track)
                continue;
            const Obj &to = *by[s.track];
            if (get<uint32_t>(m, to.at + 5) != 1)
                continue;  // one source only
            s.source_at = to.at + 14;
            s.source = get<uint32_t>(m, s.source_at);
            s.embedded = m[s.source_at - 1] == 0;
            out.push_back(s);
        }
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.number < b.number; });
    return out;
}

// music.bnk: an index (DIDX: id, offset, size) and the data (DATA), each entry 16-byte aligned
struct MusicBank {
    const Bytes &bytes;
    Chunk didx, data;
    struct Entry {
        uint32_t id, off, size;
    };
    std::vector<Entry> entries;

    explicit MusicBank(const Bytes &b) : bytes(b)
    {
        auto c = chunks(bytes);
        if (!c.count("DIDX") || !c.count("DATA"))
            throw std::runtime_error("music.bnk has no media index");
        didx = c["DIDX"];
        data = c["DATA"];
        for (size_t i = 0; i + 12 <= didx.size; i += 12)
            entries.push_back({get<uint32_t>(bytes, didx.at + i), get<uint32_t>(bytes, didx.at + i + 4),
                               get<uint32_t>(bytes, didx.at + i + 8)});
    }
    const uint8_t *media(uint32_t id, uint32_t *size = nullptr) const
    {
        for (auto &e : entries)
            if (e.id == id) {
                if (size)
                    *size = e.size;
                return bytes.data() + data.at + e.off;
            }
        return nullptr;
    }
    Bytes rebuilt(const std::map<uint32_t, Bytes> &swap) const
    {
        size_t total = didx.at + didx.size + 8;
        for (auto &e : entries) {
            auto s = swap.find(e.id);
            total += (s != swap.end() ? s->second.size() : e.size) + 16;
        }
        Bytes nm;
        nm.reserve(total);
        nm.insert(nm.end(), bytes.begin(), bytes.begin() + didx.at);  // BKHD and the DIDX tag/size
        size_t index = nm.size();
        nm.resize(nm.size() + entries.size() * 12);
        nm.insert(nm.end(), {'D', 'A', 'T', 'A', 0, 0, 0, 0});
        size_t base = nm.size();
        for (size_t k = 0; k < entries.size(); ++k) {
            const Entry &e = entries[k];
            while ((nm.size() - base) % 16)
                nm.push_back(0);
            uint32_t off = static_cast<uint32_t>(nm.size() - base), size;
            auto s = swap.find(e.id);
            if (s != swap.end()) {
                size = static_cast<uint32_t>(s->second.size());
                nm.insert(nm.end(), s->second.begin(), s->second.end());
            } else {
                size = e.size;
                nm.insert(nm.end(), bytes.begin() + data.at + e.off, bytes.begin() + data.at + e.off + e.size);
            }
            put<uint32_t>(nm, index + 12 * k, e.id);
            put<uint32_t>(nm, index + 12 * k + 4, off);
            put<uint32_t>(nm, index + 12 * k + 8, size);
        }
        put<uint32_t>(nm, base - 4, static_cast<uint32_t>(nm.size() - base));
        nm.insert(nm.end(), bytes.begin() + data.at + data.size, bytes.end());  // anything after DATA
        return nm;
    }
};

// Rewrites the doubles in [at, at+size) within 2 ms of `from` as `to`; returns where.
inline std::vector<size_t> retime(Bytes &m, size_t at, size_t size, double from, double to)
{
    std::vector<size_t> where;
    for (size_t i = at; i + 8 <= at + size; ++i) {
        double d = get<double>(m, i);
        if (std::isfinite(d) && std::fabs(d - from) < 2.0) {
            put<double>(m, i, to);
            where.push_back(i);
            i += 7;
        }
    }
    return where;
}

struct Replacement {
    Kind kind;
    int slot;
    fs::path wem;  // one of our .wem files
};

struct Result {
    Bytes meta, music;
    std::vector<std::pair<uint32_t, fs::path>> streams;  // streams/<id>.wem <- the song file
    std::vector<std::string> warnings;
};

// `original_stream(id)` gives the clean streams/<id>.wem, for a song whose header is not in music.bnk.
inline Result patch(const Bytes &meta_in, const Bytes &music_in, const std::vector<Replacement> &repl,
                    const std::function<fs::path(uint32_t)> &original_stream)
{
    Result r;
    r.meta = meta_in;
    Bytes &meta = r.meta;
    MusicBank music(music_in);
    auto objs = hirc(meta);
    std::map<uint32_t, const Obj *> by;
    for (auto &o : objs)
        by[o.id] = &o;
    std::vector<Slot> slots[2] = {find_slots(meta, objs, soundtrack), find_slots(meta, objs, theme)};
    std::map<uint32_t, Bytes> swap;

    for (auto &rp : repl) {
        const char *kname = rp.kind == soundtrack ? "soundtrack" : "theme";
        auto it = std::find_if(slots[rp.kind].begin(), slots[rp.kind].end(), [&](auto &s) { return s.number == rp.slot; });
        if (it == slots[rp.kind].end()) {
            r.warnings.push_back(std::string(kname) + " " + std::to_string(rp.slot) + ": not in the game's banks; left out");
            continue;
        }
        const Slot &s = *it;
        size_t a = s.source_at;
        if (get<uint32_t>(meta, a - 5) != kOpus) {
            r.warnings.push_back(std::string(kname) + " " + std::to_string(rp.slot) +
                                 ": the game's track is not the expected format; left out");
            continue;
        }
        // the song's current length, from its media header
        Bytes h(64);
        uint32_t have = 0;
        if (const uint8_t *p = music.media(s.source, &have); p && have >= 64)
            std::memcpy(h.data(), p, 64);
        else
            h = read_file(original_stream(s.source), 64);
        if (h.size() < 64 || std::memcmp(h.data(), "RIFF", 4))
            throw std::runtime_error(std::string(kname) + " " + std::to_string(rp.slot) + ": cannot read the game's song");
        double old_ms = 1000.0 * get<uint32_t>(h, 0x2c) / get<uint32_t>(h, 0x18);

        // the new song, read whole (a few MB)
        Bytes head = read_file(rp.wem);
        audio::WemInfo info = audio::wem_info(head.data(), head.size());
        if (!info.codec || info.ms <= 0)
            throw std::runtime_error(std::string(kname) + " " + std::to_string(rp.slot) + ": its converted song is damaged; replace it again");
        double ms = info.ms;
        if (!s.embedded)
            head.resize(std::min(head.size(), info.prefetch));  // the opening the game keeps in memory
        uint32_t media = s.source;
        swap[media] = std::move(head);
        if (!s.embedded)
            r.streams.push_back({media, rp.wem});

        // the track: its codec stays Opus
        put<uint32_t>(meta, a + 4, static_cast<uint32_t>(swap[media].size()));  // in memory
        const Obj &t = *by[s.track];
        auto len = retime(meta, t.at, t.size, old_ms, ms);
        if (len.size() != 1)
            throw std::runtime_error(std::string(kname) + " " + std::to_string(rp.slot) + ": the track's length was not found");
        if (get<double>(meta, len[0] - 8) < 0)  // the end trim (up to 0.2 s off a theme piece): none
            put<double>(meta, len[0] - 8, 0.0);

        // the segment. Markers are: id, position (double), name with a terminating zero.
        const Obj &g = *by[s.segment];
        auto marker = [&](const char *name) -> size_t {
            size_t n = std::strlen(name) + 1;
            for (size_t i = g.at + 12; i + n <= g.at + g.size; ++i)
                if (!std::memcmp(&meta[i], name, n))
                    return i - 8;
            return 0;
        };
        size_t pe = marker("music_entry"), pp = marker("preview_entry"), px = marker(kGroups[rp.kind].exit);
        if (!px)
            throw std::runtime_error(std::string(kname) + " " + std::to_string(rp.slot) + ": the segment has no exit marker");
        double vp = pp ? get<double>(meta, pp) : 0, vx = get<double>(meta, px);
        // its length fields: the doubles after the exit marker and up to the song's end (the duration, cut to
        // the beat 0.1-1.2 s short of the file, and an end point just before it). The longest becomes the new
        // song's length; the others keep their distance from it.
        std::vector<std::pair<size_t, double>> lens;
        double seg_len = 0;
        for (size_t i = g.at + 4; i + 8 <= g.at + g.size; ++i) {
            double d = get<double>(meta, i);
            if (i == pe || i == pp || i == px || !std::isfinite(d) || d <= vx || d > old_ms + 2.0)
                continue;
            lens.push_back({i, d});
            seg_len = std::max(seg_len, d);
            i += 7;
        }
        if (lens.empty())
            throw std::runtime_error(std::string(kname) + " " + std::to_string(rp.slot) + ": the segment's length was not found");
        for (auto &[i, d] : lens)
            put<double>(meta, i, std::max(0.0, ms - (seg_len - d)));
        if (pp)
            put<double>(meta, pp, std::floor(vp / old_ms * ms));
        put<double>(meta, px, std::max(0.0, ms - std::clamp(old_ms - vx, 0.0, 5000.0)));
    }
    r.music = swap.empty() ? Bytes() : music.rebuilt(swap);
    return r;
}

}  // namespace banks
