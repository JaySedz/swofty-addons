// Music Replacer launcher plugin: the "Music Replacer" page in Swofty Launcher's sidebar. A Soundtrack tab lists the
// game's 30 soundtrack songs and a Theme tab its 3 theme pieces; each can be replaced with a song of your own
// (mp3, m4a, wav, wma, flac), converted when you pick it. At build the plugin rebuilds the game's music banks
// from the clean copies the launcher keeps, with the replaced songs in, and hands them to the game folder.
//
// Speaks the launcher plugin protocol (newline-delimited JSON on stdin/stdout, launcher API 1). Keeps
// config.json and the converted songs (songs/*.wem) in the addon's data_dir, and the rebuilt banks in its
// cache_dir.

#include <swofty/json.hpp>

#include "audio.hpp"
#include "banks.hpp"
#include "tracks.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <io.h>

namespace fs = std::filesystem;
namespace J = swofty::json;

namespace {

constexpr const char *kSoundbanks = "audio/2025/soundbanks/";
constexpr const char *kKinds[] = {"soundtrack", "theme"};

// ---------------------------------------------------------------------------------------------------------
// I/O

void send(const J::Value &msg)
{
    std::string line = J::dump(msg);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

bool read_line(std::string &line)
{
    if (!std::getline(std::cin, line))
        return false;
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

int64_t g_next_call = 1000;

// A host call: the launcher answers before anything else.
J::Value call(const char *method, J::Value params = nullptr)
{
    J::Value msg = J::Value::object();
    msg["id"] = g_next_call++;
    msg["method"] = method;
    msg["params"] = std::move(params);
    send(msg);
    std::string line;
    if (!read_line(line))
        std::exit(0);
    J::Value reply = J::parse(line);
    return reply.contains("error") ? J::Value() : reply["result"];
}


fs::path from_utf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string to_utf8(const fs::path &p)
{
    auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string read_text(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// writes through a temporary file, so a failed write leaves the old file whole
bool write_file(const fs::path &p, const void *data, size_t n)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
        if (!f)
            return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}
bool write_file(const fs::path &p, const std::string &s) { return write_file(p, s.data(), s.size()); }

std::string clock(double seconds)
{
    int s = static_cast<int>(seconds + 0.5);
    char b[16];
    std::snprintf(b, sizeof b, "%d:%02d", s / 60, s % 60);
    return b;
}

std::string megabytes(double bytes)
{
    char b[32];
    std::snprintf(b, sizeof b, bytes < 10 * 1048576.0 ? "%.1f MB" : "%.0f MB", bytes / 1048576.0);
    return b;
}

// opusenc (opus-tools), shipped beside this program as bin/opus/opusenc.exe
fs::path opusenc()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return fs::path(self).parent_path() / "opus" / "opusenc.exe";
}

// every song is saved as 160 kbps Opus (about 1.2 MB a minute), the game's own format
constexpr int kKbps = 160;


// ---------------------------------------------------------------------------------------------------------
// State: config.json in data_dir.
//   { "songs": { "soundtrack/03": { "file": "soundtrack_03.wem", "name": "Party Hard", "from": "Party Hard.mp3",
//                                   "seconds": 184.9, "bytes": 35507564 } } }

struct State {
    fs::path data_dir, cache_dir;
    J::Value config = J::Value::object();

    fs::path config_path() const { return data_dir / "config.json"; }
    fs::path songs_dir() const { return data_dir / "songs"; }

    void load()
    {
        J::Error err;
        J::Value v = J::parse(read_text(config_path()), &err);
        config = err.ok() && v.is_object() ? v : J::Value::object();
        if (!config["songs"].is_object())
            config["songs"] = J::Value::object();
    }
    bool save() { return write_file(config_path(), J::dump(config, 2)); }
    J::Value &songs() { return config["songs"]; }
} g;

std::string key(int kind, int slot)
{
    char b[32];
    std::snprintf(b, sizeof b, "%s/%02d", kKinds[kind], slot);
    return b;
}

const Track *find_track(int kind, int slot)
{
    if (kind == 0) {
        for (auto &t : kSoundtrack)
            if (t.slot == slot)
                return &t;
    } else {
        for (auto &t : kTheme)
            if (t.slot == slot)
                return &t;
    }
    return nullptr;
}

// "soundtrack/03" -> (0, 3); false when it names no song
bool parse_key(const std::string &k, int &kind, int &slot)
{
    size_t s = k.find('/');
    if (s == std::string::npos)
        return false;
    std::string kn = k.substr(0, s);
    kind = kn == "soundtrack" ? 0 : kn == "theme" ? 1 : -1;
    slot = std::atoi(k.c_str() + s + 1);
    return kind >= 0 && find_track(kind, slot);
}

// ---------------------------------------------------------------------------------------------------------
// Replacing a song

// A picked song is converted on a worker thread, one at a time, so the page can show how far it has got:
// the event returns at once, the row's button reads "Converting… 40%", and the page (ticked about once a
// second) redraws as the job moves on. config.json is written under g_mx, by the worker and the main thread.

struct Job {
    std::string key;  // "soundtrack/03"
    fs::path src;
    std::string stage = "Waiting";  // Waiting, Converting, Compressing, Saving
    double done = 0;                // within the stage, 0..1
};

std::mutex g_mx;                   // guards config.json, g_jobs and g_errors
std::condition_variable g_idle;    // signalled when the queue empties
std::deque<Job> g_jobs;            // the front one is running
std::vector<std::string> g_errors; // failed conversions, shown once on the page
bool g_worker = false;

void set_stage(const std::string &stage, double done)
{
    std::lock_guard<std::mutex> lock(g_mx);
    g_jobs.front().stage = stage;
    g_jobs.front().done = done;
}

void work()
{
    for (;;) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(g_mx);
            if (g_jobs.empty()) {
                g_worker = false;
                g_idle.notify_all();
                return;
            }
            job = g_jobs.front();
        }
        int kind, slot;
        parse_key(job.key, kind, slot);
        std::string name = to_utf8(job.src.stem()), shown = to_utf8(job.src.filename());
        std::string error;
        try {
            set_stage("Converting", 0);
            audio::Bytes pcm = audio::decode(job.src, [&](double d) { set_stage("Converting", d); });
            set_stage("Compressing", 0);
            audio::Bytes wem = audio::make_opus_wem(pcm, kKbps, opusenc(), [&](double d) { set_stage("Compressing", d); });
            pcm.clear();
            set_stage("Saving", 0);
            char file[32];
            std::snprintf(file, sizeof file, "%s_%02d.wem", kKinds[kind], slot);
            if (!write_file(g.songs_dir() / file, wem.data(), wem.size()))
                throw std::runtime_error("cannot save the converted song in the addon's data folder");
            double seconds = audio::wem_ms(wem.data(), wem.size()) / 1000.0;
            std::lock_guard<std::mutex> lock(g_mx);
            State st;
            st.data_dir = g.data_dir;
            st.load();
            st.songs()[job.key] = J::Value::object({{"file", std::string(file)},
                                                    {"name", name},
                                                    {"from", shown},
                                                    {"seconds", seconds},
                                                    {"bytes", static_cast<int64_t>(wem.size())},
                                                    {"kbps", kKbps}});
            st.save();
        } catch (const std::exception &e) {
            error = shown + ": " + e.what();
        }
        std::lock_guard<std::mutex> lock(g_mx);
        if (!error.empty())
            g_errors.push_back(error);
        g_jobs.pop_front();
    }
}

J::Value replace(const std::string &k, const J::Value &paths)
{
    int kind, slot;
    if (!parse_key(k, kind, slot))
        return J::Value("none");
    const J::Array *list = paths.if_array();
    if (!list || list->empty() || !(*list)[0].is_string())
        return J::Value("none");
    std::lock_guard<std::mutex> lock(g_mx);
    for (auto &j : g_jobs)
        if (j.key == k)
            return J::Value("none");  // already on its way
    g_jobs.push_back({k, from_utf8((*list)[0].as_string())});
    if (!g_worker) {
        g_worker = true;
        std::thread(work).detach();
    }
    return J::Value("reload");
}

// The job for `k`, if it is queued or running: its button text ("Converting… 40%").
std::string job_label(const std::string &k)
{
    std::lock_guard<std::mutex> lock(g_mx);
    for (auto &j : g_jobs)
        if (j.key == k)
            return j.stage == "Waiting" ? std::string("Waiting…")
                                        : j.stage + "… " + std::to_string(static_cast<int>(j.done * 100)) + "%";
    return {};
}

// What the page shows of the jobs, to tell a tick when to redraw.
std::string jobs_signature()
{
    std::lock_guard<std::mutex> lock(g_mx);
    std::string s = std::to_string(g_errors.size());
    for (auto &j : g_jobs)
        s += "|" + j.key + j.stage + std::to_string(static_cast<int>(j.done * 20));  // every 5%
    return s;
}

void wait_for_jobs()
{
    std::unique_lock<std::mutex> lock(g_mx);
    g_idle.wait(lock, [] { return !g_worker; });
}

J::Value restore(const std::string &k)
{
    int kind, slot;
    if (!parse_key(k, kind, slot) || !job_label(k).empty())
        return J::Value("none");
    std::lock_guard<std::mutex> lock(g_mx);
    g.load();
    J::Value &songs = g.songs();
    if (const J::Value *s = songs.find(k)) {
        std::error_code ec;
        fs::remove(g.songs_dir() / from_utf8((*s)["file"].as_string()), ec);
        songs.if_object()->erase(k);
        g.save();
    }
    return J::Value::object({{"notice", std::string(find_track(kind, slot)->title) +
                                            " is back. The game's own song plays from the next time you press Play."}});
}

// ---------------------------------------------------------------------------------------------------------
// The page

std::string tab = "soundtrack";

J::Value text_block(const std::string &text, const std::string &tone = "neutral")
{
    return J::Value::object({{"text", J::Value::object({{"text", text}, {"tone", tone}})}});
}

J::Value replace_button(const std::string &k, const std::string &label)
{
    return J::Value::object(
        {{"id", "replace:" + k},
         {"label", label},
         {"icon", "upload"},
         {"style", "primary"},
         {"kind", J::Value::object({{"pick", J::Value::object({{"title", "Choose a song"},
                                                               {"extensions", J::Value::array({"mp3", "m4a", "aac", "wav", "wma", "flac"})},
                                                               {"folders", false},
                                                               {"multiple", false}})}})}});
}

template <class List> J::Value song_table(int kind, const List &tracks)
{
    J::Value &songs = g.songs();
    J::Value rows = J::Value::array();
    for (const Track &t : tracks) {
        std::string k = key(kind, t.slot);
        const J::Value *s = songs.find(k);
        std::string original = std::string(t.title) + " — " + t.artist + " (" + clock(t.seconds) + ")";
        J::Value detail = J::Value::array();
        J::Value facts = J::Value::array({J::Value::object({{"label", "Game's song"}, {"value", original}})});
        J::Value buttons = J::Value::array();
        std::string playing, busy = job_label(k);
        if (!busy.empty()) {
            // a plain button while it converts: a click does nothing
            buttons.push_back(J::Value::object({{"id", "busy:" + k}, {"label", busy}, {"icon", "upload"}, {"style", "primary"}}));
            if (s)
                facts.push_back(J::Value::object({{"label", "Replaced with"}, {"value", (*s)["name"]}}));
        } else if (s) {
            playing = std::string((*s)["name"].as_string()) + " (" + clock((*s)["seconds"].as_double()) + ")";
            facts.push_back(J::Value::object({{"label", "Replaced with"}, {"value", playing}}));
            facts.push_back(J::Value::object({{"label", "From"}, {"value", (*s)["from"]}}));
            facts.push_back(J::Value::object({{"label", "Size"}, {"value", megabytes((*s)["bytes"].as_double())}}));
            buttons.push_back(replace_button(k, "Replace with another…"));
            buttons.push_back(J::Value::object({{"id", "restore:" + k}, {"label", "Use the game's song"}, {"icon", "eraser"}}));
        } else {
            buttons.push_back(replace_button(k, "Replace…"));
        }
        detail.push_back(J::Value::object({{"facts", std::move(facts)}}));
        detail.push_back(J::Value::object({{"buttons", std::move(buttons)}}));
        J::Value now = !busy.empty() ? J::Value::object({{"text", busy}, {"tone", "warn"}})
                       : s           ? J::Value::object({{"text", playing}, {"tone", "accent"}})
                                     : J::Value::object({{"text", "Game's song"}, {"tone", "muted"}});
        rows.push_back(J::Value::object({{"id", k},
                                         {"title", t.title},
                                         {"subtitle", t.artist},
                                         {"cells", J::Value::array({J::Value::object({{"text", std::to_string(t.slot)}}),
                                                                    J::Value::object({{"text", t.title}}),
                                                                    J::Value::object({{"text", t.artist}, {"tone", "muted"}}),
                                                                    J::Value::object({{"text", clock(t.seconds)}, {"tone", "muted"}}),
                                                                    std::move(now)})},
                                         {"detail", std::move(detail)}}));
    }
    return J::Value::object(
        {{"table", J::Value::object({{"id", kKinds[kind]},
                                     {"columns", J::Value::array({
                                                     J::Value::object({{"key", "n"}, {"title", "#"}, {"sort", true}}),
                                                     J::Value::object({{"key", "song"}, {"title", "Song"}, {"sort", true}}),
                                                     J::Value::object({{"key", "artist"}, {"title", kind ? "Composer" : "Artist"}, {"sort", true}}),
                                                     J::Value::object({{"key", "length"}, {"title", "Length"}}),
                                                     J::Value::object({{"key", "now"}, {"title", "Plays"}, {"sort", true}}),
                                                 })},
                                     {"rows", std::move(rows)},
                                     {"search", J::Value::object({{"placeholder", "Search songs"}})}})}});
}

size_t count(int kind)
{
    size_t n = 0;
    if (auto *o = g.songs().if_object())
        for (auto &[k, v] : *o)
            n += k.rfind(kKinds[kind], 0) == 0;
    return n;
}

std::string g_shown;  // jobs_signature() when the page was last drawn

J::Value view()
{
    g_shown = jobs_signature();
    std::vector<std::string> errors;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        errors.swap(g_errors);  // shown once
        g.load();
    }
    size_t ns = count(0), nt = count(1);
    double bytes = 0;
    if (auto *o = g.songs().if_object())
        for (auto &[k, v] : *o)
            bytes += v["bytes"].as_double();

    J::Value blocks = J::Value::array();
    blocks.push_back(J::Value::object(
        {{"tabs", J::Value::object({{"id", "page"},
                                    {"items", J::Value::array({
                                                  J::Value::object({{"id", "soundtrack"}, {"label", "Soundtrack"}, {"icon", "play"},
                                                                    {"badge", std::to_string(ns)}}),
                                                  J::Value::object({{"id", "theme"}, {"label", "Theme"}, {"icon", "layers"},
                                                                    {"badge", std::to_string(nt)}}),
                                              })},
                                    {"active", tab}})}}));

    if (tab == "theme") {
        blocks.push_back(text_block("The themes that play when set to Theme in the audio settings. Good for if you don't have 30 songs to replace.",
                                    "muted"));
        blocks.push_back(song_table(1, kTheme));
    } else {
        blocks.push_back(text_block("The licensed soundtrack. Plays in the menus, practice sessions and time trial when music type is set to Soundtrack in "
                                    "the audio settings.",
                                    "muted"));
        blocks.push_back(song_table(0, kSoundtrack));
    }

    J::Value notices = J::Value::array();
    for (auto &e : errors)
        notices.push_back(J::Value::object({{"text", "Could not convert " + e}, {"tone", "bad"}}));
    if (call("game_status")["running"].as_bool(false))
        notices.push_back(J::Value::object({{"text", "The game is running: changes play from the next time you press Play."},
                                            {"tone", "muted"}}));
    std::string sub = ns + nt ? std::to_string(ns + nt) + " of 33 songs replaced, " + megabytes(bytes) + " of audio"
                              : "The game's own music";
    return J::Value::object({{"subtitle", sub},
                             {"notices", std::move(notices)},
                             {"body", J::Value::object({{"blocks", std::move(blocks)}})},
                             {"footer", "Music Replacer. Click a song to replace it."},
                             // a tick about once a second while the page is open: it redraws as a conversion moves on
                             {"tick", true}});
}

J::Value event(const J::Value &ev)
{
    if (auto *t = ev.find("tab")) {
        std::string next((*t)["tab"].as_string());
        if (next == "soundtrack" || next == "theme")
            tab = next;
        return J::Value("reload");
    }
    if (auto *p = ev.find("picked")) {
        std::string id((*p)["id"].as_string());
        if (id.rfind("replace:", 0) == 0)
            return replace(id.substr(8), (*p)["paths"]);
        return J::Value("none");
    }
    if (auto *c = ev.find("click")) {
        std::string id((*c)["id"].as_string());
        if (id.rfind("restore:", 0) == 0)
            return restore(id.substr(8));
    }
    return J::Value("none");
}

// ---------------------------------------------------------------------------------------------------------
// Build: the game's banks from their clean copies, with the replaced songs in.

fs::path original(const std::string &rel)
{
    J::Value p = call("game_original", J::Value::object({{"rel", rel}}));
    std::error_code ec;
    if (p.is_string() && fs::is_regular_file(from_utf8(p.as_string()), ec))
        return from_utf8(p.as_string());
    throw std::runtime_error("the launcher has no clean copy of " + rel);
}

std::string stamp(const fs::path &p)
{
    std::error_code ec;
    auto t = fs::last_write_time(p, ec);
    auto n = ec ? 0 : fs::file_size(p, ec);
    return std::to_string(t.time_since_epoch().count()) + ":" + std::to_string(n);
}

J::Value build(const J::Value &)
{
    wait_for_jobs();  // a song still converting goes in with the rest
    std::unique_lock<std::mutex> lock(g_mx);
    g.load();
    lock.unlock();
    std::vector<banks::Replacement> repl;
    J::Value warnings = J::Value::array();
    if (auto *o = g.songs().if_object())
        for (auto &[k, v] : *o) {
            int kind, slot;
            fs::path f = g.songs_dir() / from_utf8(v["file"].as_string());
            std::error_code ec;
            if (!parse_key(k, kind, slot))
                continue;
            if (!fs::is_regular_file(f, ec)) {
                warnings.push_back(k + ": its converted song is missing; replace it again");
                continue;
            }
            repl.push_back({static_cast<banks::Kind>(kind), slot, f});
        }
    if (repl.empty())
        return J::Value::object({{"lines", J::Value::array({"no songs replaced: the game's own music plays"})},
                                 {"warnings", std::move(warnings)}});

    std::string meta_rel = std::string(kSoundbanks) + "metadata.bnk", music_rel = std::string(kSoundbanks) + "music.bnk";
    fs::path meta_src = original(meta_rel), music_src = original(music_rel);
    fs::path out = g.cache_dir / "build";

    // rebuild only when the songs or the game's banks changed
    std::string sig = stamp(meta_src) + "|" + stamp(music_src);
    for (auto &r : repl)
        sig += "|" + std::to_string(r.kind) + "/" + std::to_string(r.slot) + "=" + stamp(r.wem);
    std::error_code ec;
    bool fresh = read_text(out / "sig.txt") == sig && fs::is_regular_file(out / "metadata.bnk", ec) &&
                 fs::is_regular_file(out / "music.bnk", ec) && fs::is_regular_file(out / "streams.json", ec);
    J::Value streams = J::Value::object();
    if (fresh) {
        J::Error err;
        streams = J::parse(read_text(out / "streams.json"), &err);
        fresh = err.ok() && streams.is_object();
    }
    if (!fresh) {
        banks::Bytes meta = banks::read_file(meta_src), music = banks::read_file(music_src);
        auto r = banks::patch(meta, music, repl, [&](uint32_t id) {
            return original(std::string(kSoundbanks) + "streams/" + std::to_string(id) + ".wem");
        });
        for (auto &w : r.warnings)
            warnings.push_back(w);
        streams = J::Value::object();
        for (auto &[id, wem] : r.streams)
            streams[std::to_string(id)] = to_utf8(fs::relative(wem, g.data_dir, ec));
        fs::remove(out / "sig.txt", ec);
        if (!write_file(out / "metadata.bnk", r.meta.data(), r.meta.size()) ||
            !write_file(out / "music.bnk", r.music.data(), r.music.size()) ||
            !write_file(out / "streams.json", J::dump(streams)) || !write_file(out / "sig.txt", sig))
            throw std::runtime_error("cannot write the rebuilt banks to the addon's cache folder");
    }

    J::Value ops = J::Value::array();
    auto put = [&](const char *root, const std::string &from, const std::string &target) {
        ops.push_back(J::Value::object({{"op", "file.put"},
                                        {"source", J::Value::object({{"root", root}, {"path", from}})},
                                        {"target", target}}));
    };
    put("cache", "build/metadata.bnk", meta_rel);
    put("cache", "build/music.bnk", music_rel);
    for (auto &[id, rel] : *streams.if_object()) {
        std::string from(rel.as_string());
        std::replace(from.begin(), from.end(), '\\', '/');
        put("data", from, std::string(kSoundbanks) + "streams/" + id + ".wem");
    }
    size_t ns = 0, nt = 0;
    for (auto &r : repl)
        (r.kind == banks::soundtrack ? ns : nt)++;
    std::string line = std::to_string(ns) + " soundtrack song(s) and " + std::to_string(nt) + " theme piece(s) replaced" +
                       (fresh ? "" : " (banks rebuilt)");
    return J::Value::object({{"ops", std::move(ops)}, {"lines", J::Value::array({line})}, {"warnings", std::move(warnings)}});
}

J::Value summary()
{
    std::lock_guard<std::mutex> lock(g_mx);
    g.load();
    size_t ns = count(0), nt = count(1);
    if (!ns && !nt)
        return J::Value();
    return J::Value::object({{"label", "MUSIC REPLACER"},
                             {"items", J::Value::array({std::to_string(ns) + " soundtrack song(s)",
                                                        std::to_string(nt) + " theme piece(s)"})}});
}

}  // namespace

int main()
{
    _setmode(_fileno(stdout), _O_BINARY);
    std::string line;
    while (read_line(line)) {
        if (line.empty())
            continue;
        J::Error err;
        J::Value msg = J::parse(line, &err);
        if (!err.ok() || !msg.is_object())
            continue;
        std::string method(msg["method"].as_string());
        if (!msg.contains("id"))
            continue;  // notifications: nothing to do
        J::Value reply = J::Value::object({{"id", msg["id"]}});
        std::string invalidate;  // a section to redraw, told after the reply (a notification: no id)
        try {
            const J::Value &p = msg["params"];
            if (method == "hello") {
                g.data_dir = from_utf8(p["data_dir"].as_string());
                g.cache_dir = from_utf8(p["cache_dir"].as_string());
                std::error_code ec;
                fs::create_directories(g.data_dir, ec);
                fs::create_directories(g.cache_dir, ec);
                reply["result"] = J::Value::object({{"api", 1}});
            } else if (method == "view") {
                reply["result"] = view();
            } else if (method == "event") {
                reply["result"] = event(p["event"]);
            } else if (method == "build") {
                reply["result"] = build(p);
            } else if (method == "summary") {
                reply["result"] = summary();
            } else if (method == "tick") {
                // redraw while a conversion moves on, and once it is done
                if (jobs_signature() != g_shown)
                    invalidate = std::string(p["section"].as_string("music"));
                reply["result"] = nullptr;
            } else if (method == "command") {
                reply["result"] = "Open the Music Replacer page in the launcher's sidebar.";
            } else {
                reply["error"] = "unknown method " + method;
            }
        } catch (const std::exception &e) {
            reply = J::Value::object({{"id", msg["id"]}, {"error", e.what()}});
        }
        send(reply);
        if (!invalidate.empty())
            send(J::Value::object({{"method", "invalidate"}, {"params", J::Value::object({{"section", invalidate}})}}));
    }
    return 0;
}
