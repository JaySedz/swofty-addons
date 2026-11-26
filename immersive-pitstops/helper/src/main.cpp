// Immersive Pitstops launcher plugin: the "Pit Stops" page in Swofty Launcher's sidebar (1.0.42).
//
// Three tabs:
//   Teams        a table of the teams; clicking one opens a panel with its own pit stop values (stuck wheel nut,
//                loose wheel, wrong tyres and retirement chances, crew readiness, stop times). A team without its
//                own values uses the addon's settings. The values apply to the team's cars, yours and the AI's.
//   Ferrari Mode the joke mode: Off, Light or Full, and a switch for each of its features.
//   MyTeam       the MyTeam team's own values (more MyTeam features later).
//
// Speaks the launcher plugin protocol (newline-delimited JSON on stdin/stdout, launcher API 1; the same as AI
// Helmets' helper). Keeps config.json in the addon's data_dir; each edit is also written into the game folder's copy
// (swofty/addons/sedz.immersive-pitstops/), which the engine plugin reads again every 2 s, and the build hands it to
// the game with file.put.

#include <swofty/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace fs = std::filesystem;
namespace J = swofty::json;

namespace {

constexpr const char *kGameDir = "swofty/addons/sedz.immersive-pitstops/";

struct TeamDef {
    const char *key, *name, *note;
};
// the engine plugin's cfg::team_key names (the team badge without "_26" and underscores, mapped)
constexpr TeamDef kTeams[] = {
    {"mclaren", "McLaren", ""},
    {"mercedes", "Mercedes", ""},
    {"redbull", "Red Bull Racing", ""},
    {"ferrari", "Ferrari", ""},
    {"williams", "Williams", ""},
    {"racingbulls", "Racing Bulls", ""},
    {"astonmartin", "Aston Martin", ""},
    {"haas", "Haas", ""},
    {"audi", "Audi", "Kick Sauber in 2025"},
    {"alpine", "Alpine", ""},
    {"cadillac", "Cadillac", "2026"},
};

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

void log(const std::string &s) { std::fprintf(stderr, "%s\n", s.c_str()); }

std::string read_file(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool write_file(const fs::path &p, const std::string &s)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << s;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

fs::path from_utf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string pct(double v)
{
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-6)
        std::snprintf(b, sizeof b, "%.0f %%", v);
    else
        std::snprintf(b, sizeof b, "%.1f %%", v);
    return b;
}

// ---------------------------------------------------------------------------------------------------------
// State

// 1.0.43: every team's values (the addon settings up to 1.0.42); the engine plugin's cfg::Team defaults
struct Values {
    double stuck = 1, retire = 25, loose = 0.3, wrong = 0.3;
    std::string crew = "called", times = "default";
};

struct Plugin {
    fs::path data_dir, cache_dir;
    J::Value config;
    std::string tab = "teams";

    fs::path config_path() const { return data_dir / "config.json"; }

    void load()
    {
        config = J::Value::object();
        std::string text = read_file(config_path());
        if (!text.empty()) {
            J::Error err;
            J::Value v = J::parse(text, &err);
            if (err.ok() && v.is_object())
                config = std::move(v);
            else
                log("config.json is not valid JSON: " + err.message);
        }
        config["version"] = 1;
        if (!config["teams"].is_object())
            config["teams"] = J::Value::object();
        if (!config["myteam"].is_object())
            config["myteam"] = J::Value::object();
        if (!config["ferrari"].is_object())
            config["ferrari"] = J::Value::object();
        if (!config["all"].is_object()) {
            // 1.0.43: the addon settings moved here. 1.0.42's build kept them in options.json: start from them.
            J::Value all = J::Value::object();
            J::Value o = J::parse(read_file(data_dir / "options.json"));
            if (o.is_object()) {
                if (o["stuck_chance"].is_number())
                    all["stuck"] = o["stuck_chance"].as_double();
                if (o["retire_chance"].is_number())
                    all["retire"] = o["retire_chance"].as_double();
                if (o["loose_chance"].is_number())
                    all["loose"] = o["loose_chance"].as_double();
                if (o["wrong_tyres_chance"].is_number())
                    all["wrong"] = o["wrong_tyres_chance"].as_double();
                if (o["call_stop"].is_bool())
                    all["crew"] = o["call_stop"].as_bool() ? "called" : "always";
                if (o["stop_times"].is_string())
                    all["times"] = std::string(o["stop_times"].as_string());
            }
            config["all"] = std::move(all);
        }
    }

    // data_dir's copy goes to the game at the next Play; the game folder's copy is updated too, so a running game
    // picks the change up within 2 s. Never during a build (the build hands it over with file.put).
    bool save(bool live = true)
    {
        std::string text = J::dump(config, 2);
        bool ok = write_file(config_path(), text);
        if (!live)
            return ok;
        J::Value dir = call("addon_game_dir");
        std::error_code ec;
        if (dir.is_string() && fs::is_directory(from_utf8(dir.as_string()), ec))
            write_file(from_utf8(dir.as_string()) / "config.json", text);
        return ok;
    }
};

Plugin g;

// ---------------------------------------------------------------------------------------------------------
// Blocks

J::Value text_block(const std::string &t, const char *tone = "neutral")
{
    return J::Value::object({{"text", J::Value::object({{"text", t}, {"tone", tone}})}});
}

J::Value option(const std::string &value, const std::string &label)
{
    return J::Value::object({{"value", value}, {"label", label}});
}

J::Value card(const std::string &title, J::Value blocks)
{
    return J::Value::object({{"card", J::Value::object({{"title", title}, {"blocks", std::move(blocks)}})}});
}

// values on top of a base: each key the object has replaces the base's
Values values_of(const J::Value &t, Values v = Values{})
{
    if (t["stuck"].is_number())
        v.stuck = t["stuck"].as_double();
    if (t["retire"].is_number())
        v.retire = t["retire"].as_double();
    if (t["loose"].is_number())
        v.loose = t["loose"].as_double();
    if (t["wrong"].is_number())
        v.wrong = t["wrong"].as_double();
    std::string_view c = t["crew"].as_string("");
    if (c == "called" || c == "always" || c == "never")
        v.crew = std::string(c);
    std::string_view tm = t["times"].as_string("");
    if (tm == "default" || tm == "fast" || tm == "slow")
        v.times = std::string(tm);
    return v;
}

Values all_values() { return values_of(g.config["all"]); }

bool own(const J::Value &t) { return t.is_object() && t.size() > 0; }

std::string crew_label(const std::string &c)
{
    return c == "always" ? "Always ready" : c == "never" ? "Never ready" : "Ready when called";
}

std::string times_label(const std::string &t) { return t == "fast" ? "Fast" : t == "slow" ? "Slow" : "Default"; }

J::Value value_fields(const Values &v)
{
    J::Value fields = J::Value::array();
    fields.push_back(J::Value::object({{"slider", J::Value::object({
        {"id", "stuck"}, {"label", "Stuck wheel nut chance"}, {"value", v.stuck}, {"min", 0}, {"max", 100},
        {"step", 1}, {"unit", "%"}, {"help", "100 gives one at every stop."}})}}));
    fields.push_back(J::Value::object({{"slider", J::Value::object({
        {"id", "retire"}, {"label", "Retirement chance"}, {"value", v.retire}, {"min", 0}, {"max", 100},
        {"step", 1}, {"unit", "%"}, {"help", "Chance a stuck wheel nut ends the car's race"}})}}));
    fields.push_back(J::Value::object({{"slider", J::Value::object({
        {"id", "loose"}, {"label", "Loose wheel chance"}, {"value", v.loose}, {"min", 0}, {"max", 10},
        {"step", 0.1}, {"unit", "%"},
        {"help", "Your car will be told to box this lap. AI cars lose the wheel on its out lap"}})}}));
    fields.push_back(J::Value::object({{"slider", J::Value::object({
        {"id", "wrong"}, {"label", "Wrong tyres chance"}, {"value", v.wrong}, {"min", 0}, {"max", 10},
        {"step", 0.1}, {"unit", "%"}, {"help", "A different compound, always one for the current weather."}})}}));
    fields.push_back(J::Value::object({{"select", J::Value::object({
        {"id", "crew"}, {"label", "Crew readiness"}, {"value", v.crew},
        {"help", "When you don't call your stop with Box This Lap. The AI always calls its stops."},
        {"options", J::Value::array({option("called", "Ready when called"), option("always", "Always ready"),
                                     option("never", "Never ready")})}})}}));
    fields.push_back(J::Value::object({{"select", J::Value::object({
        {"id", "times"}, {"label", "Stop times"}, {"value", v.times},
        {"help", "Fast: more quick stops. Slow: more slow ones."},
        {"options", J::Value::array({option("default", "Default"), option("fast", "Fast"), option("slow", "Slow")})}})}}));
    return fields;
}

J::Value form(const std::string &id, J::Value fields)
{
    return J::Value::object({{"form", J::Value::object({{"id", id}, {"live", true}, {"fields", std::move(fields)}})}});
}

// The panel of a team (key "myteam" for MyTeam): its own values, or All teams'.
J::Value team_panel(const std::string &key, const J::Value &t)
{
    bool mine = own(t);
    Values v = values_of(t, all_values());
    J::Value fields = J::Value::array();
    fields.push_back(J::Value::object({{"toggle", J::Value::object({
        {"id", "own"}, {"label", "Own values"}, {"value", mine},
        {"help", mine ? "Off: back to the All teams values." : "Off: the All teams values (shown greyed)."}})}}));
    for (auto &f : *value_fields(v).if_array()) {
        J::Value field = f;
        for (auto &[kind, body] : *field.if_object())
            body["disabled"] = !mine;
        fields.push_back(std::move(field));
    }
    return J::Value::array({form("team:" + key, std::move(fields))});
}

J::Value cell(const std::string &text, bool own)
{
    return J::Value::object({{"text", text}, {"tone", own ? "neutral" : "muted"}});
}

J::Value row(const std::string &id, const std::string &name, const std::string &subtitle, const Values &v, bool mine,
             const std::string &status, J::Value detail)
{
    return J::Value::object({
        {"id", id},
        {"title", name},
        {"subtitle", subtitle},
        {"cells", J::Value::array({(id == "all" ? J::Value::object({{"text", name}, {"spans", J::Value::array({J::Value::object({{"text", name}, {"bold", true}})})}}) : J::Value::object({{"text", name}})), cell(pct(v.stuck), mine),
                                   cell(pct(v.retire), mine), cell(pct(v.loose), mine), cell(pct(v.wrong), mine),
                                   cell(crew_label(v.crew), mine), cell(times_label(v.times), mine),
                                   J::Value::object({{"text", status},
                                                     {"tone", id == "all" ? "neutral" : mine ? "accent" : "muted"},
                                                     {"pill", true}})})},
        {"detail", std::move(detail)}});
}

J::Value teams_tab()
{
    Values all = all_values();
    J::Value rows = J::Value::array();
    J::Value adetail = J::Value::array();
    adetail.push_back(text_block("Every team without its own values: their crews at their cars' stops, yours and the AI's.",
                                 "muted"));
    adetail.push_back(form("team:all", value_fields(all)));
    rows.push_back(row("all", "All teams", "the default for every team", all, true, "Default", std::move(adetail)));
    for (auto &d : kTeams) {
        const J::Value &t = g.config["teams"][d.key];
        bool mine = own(t);
        J::Value detail = J::Value::array();
        detail.push_back(text_block(std::string("The team's crew, at its cars' stops: yours and the AI's.") +
                                        (*d.note ? std::string(" ") + d.note + "." : std::string()),
                                    "muted"));
        for (auto &b : *team_panel(d.key, t).if_array())
            detail.push_back(b);
        rows.push_back(row(d.key, d.name, *d.note ? d.note : d.key, values_of(t, all), mine,
                           mine ? "Own values" : "All teams", std::move(detail)));
    }
    J::Value blocks = J::Value::array();
    blocks.push_back(J::Value::object({{"table", J::Value::object({
        {"id", "teams"},
        {"columns", J::Value::array({
            J::Value::object({{"key", "team"}, {"title", "Team"}}),
            J::Value::object({{"key", "stuck"}, {"title", "Stuck nut"}, {"width", 96}}),
            J::Value::object({{"key", "retire"}, {"title", "Retirement"}, {"width", 104}}),
            J::Value::object({{"key", "loose"}, {"title", "Loose wheel"}, {"width", 110}}),
            J::Value::object({{"key", "wrong"}, {"title", "Wrong tyres"}, {"width", 110}}),
            J::Value::object({{"key", "crew"}, {"title", "Crew"}}),
            J::Value::object({{"key", "times"}, {"title", "Stop times"}, {"width", 104}}),
            J::Value::object({{"key", "status"}, {"title", "Values"}, {"width", 120}}),
        })},
        {"rows", std::move(rows)},
        {"search", J::Value::object({{"placeholder", "Search teams"}})}})}}));
    blocks.push_back(text_block("Click All teams to set every team's values; click a team and turn on Own values to give "
                                "its crew its own. Grey values come from All teams. The testing switches and logging "
                                "are in the addon's settings.",
                                "muted"));
    return blocks;
}

// 1.0.45: Extra Features: Ferrari Mode (Off / Light / Full, no separate switches) and Papaya Rules (McLaren).
J::Value extras_tab()
{
    std::string mode(g.config["ferrari"]["mode"].as_string("off"));
    bool papaya = g.config["mclaren"]["identical"].as_bool(false);
    J::Value fields = J::Value::array();
    fields.push_back(J::Value::object({{"select", J::Value::object({
        {"id", "mode"}, {"label", "Ferrari Mode"}, {"value", mode},
        {"help", "Ferrari only, player and AI. In Light mode your crew is never ready for you, so your stops are a bit slower. Full mode increases the chance of a stuck or loose wheel to 50%. Adds Ferrari specific popups to Driver Display if installed."},
        {"options", J::Value::array({option("off", "Off"), option("light", "Light"), option("full", "Full")})}})}}));
    fields.push_back(J::Value::object({{"toggle", J::Value::object({
        {"id", "papaya"}, {"label", "Papaya Rules"}, {"value", papaya},
        {"help", "McLaren only, player and AI. Both McLarens get identical pit stops."}})}}));
    J::Value blocks = J::Value::array();
    blocks.push_back(text_block("Extra team features, on top of the team tab's values", "muted"));
    blocks.push_back(form("extras", std::move(fields)));
    return blocks;
}

J::Value myteam_tab()
{
    J::Value blocks = J::Value::array();
    blocks.push_back(text_block("Your MyTeam career's team: its crew at your stops and your team-mate's. Without its "
                                "own values it uses the Teams tab's All teams values. More MyTeam features are on the "
                                "way.",
                                "muted"));
    blocks.push_back(card("MyTeam crew", team_panel("myteam", g.config["myteam"])));
    return blocks;
}

J::Value view()
{
    g.load();
    size_t n = 0;
    if (auto *o = g.config["teams"].if_object())
        for (auto &[k, t] : *o)
            n += own(t) ? 1 : 0;
    std::string mode(g.config["ferrari"]["mode"].as_string("off"));
    J::Value blocks = J::Value::array();
    blocks.push_back(J::Value::object({{"tabs", J::Value::object({
        {"id", "page"},
        {"items", [&] {
            J::Value teams = J::Value::object({{"id", "teams"}, {"label", "Teams"}, {"icon", "users"}});
            if (n)
                teams["badge"] = std::to_string(n);
            J::Value fer = J::Value::object({{"id", "extras"}, {"label", "Extra Features"}, {"icon", "flag"}});
            int on = (mode != "off" ? 1 : 0) + (g.config["mclaren"]["identical"].as_bool(false) ? 1 : 0);
            if (on)
                fer["badge"] = std::to_string(on);
            return J::Value::array({std::move(teams), std::move(fer),
                                    J::Value::object({{"id", "myteam"}, {"label", "MyTeam"}, {"icon", "trophy"}})});
        }()},
        {"active", g.tab}})}}));
    J::Value body = g.tab == "extras" ? extras_tab() : g.tab == "myteam" ? myteam_tab() : teams_tab();
    for (auto &b : *body.if_array())
        blocks.push_back(b);
    J::Value notices = J::Value::array();
    if (call("game_status")["running"].as_bool(false))
        notices.push_back(J::Value::object({{"text", "The game is running: changes apply within a couple of seconds."},
                                            {"tone", "accent"}}));
    std::string sub = std::to_string(n) + " team(s) edited, Ferrari Mode " +
                      (mode == "full" ? "Full" : mode == "light" ? "Light" : "off") + ", Papaya Rules " +
                      (g.config["mclaren"]["identical"].as_bool(false) ? "on" : "off");
    return J::Value::object({{"subtitle", sub},
                             {"notices", std::move(notices)},
                             {"body", J::Value::object({{"blocks", std::move(blocks)}})},
                             {"footer", "Immersive Pitstops. Every chance is set here; the addon's settings keep the "
                                        "testing switches and logging."}});
}

// ---------------------------------------------------------------------------------------------------------
// Events

// A team's form: "own" on copies All teams' values into it, off drops them; a value sets it.
void apply_values(J::Value &t, const J::Value &values)
{
    if (!t.is_object())
        t = J::Value::object();
    for (const char *k : {"stuck", "retire", "loose", "wrong"})
        if (values[k].is_number())
            t[k] = values[k].as_double();
    for (const char *k : {"crew", "times"})
        if (values[k].is_string() && !values[k].as_string().empty())
            t[k] = std::string(values[k].as_string());
}

void apply_team(J::Value &t, const J::Value &values)
{
    if (values["own"].is_bool()) {
        if (values["own"].as_bool()) {
            Values a = all_values();
            t = J::Value::object({{"stuck", a.stuck}, {"retire", a.retire}, {"loose", a.loose}, {"wrong", a.wrong},
                                  {"crew", a.crew}, {"times", a.times}});
        } else {
            t = J::Value::object();
        }
        return;
    }
    apply_values(t, values);
}

J::Value event(const J::Value &e)
{
    g.load();
    if (e["tab"].is_object()) {
        g.tab = std::string(e["tab"]["tab"].as_string("teams"));
        return "reload";
    }
    if (e["click"].is_object()) {
        std::string id(e["click"]["id"].as_string());
        if (id.rfind("reset:", 0) == 0) {
            std::string key = id.substr(6);
            if (key == "myteam")
                g.config["myteam"] = J::Value::object();
            else
                g.config["teams"].erase(key);
            if (!g.save())
                return J::Value::object({{"notice", "Could not save config.json"}});
            return "reload";
        }
        return "reload";
    }
    if (e["submit"].is_object()) {
        std::string form(e["submit"]["form"].as_string());
        const J::Value &values = e["submit"]["values"];
        if (form == "team:all") {
            apply_values(g.config["all"], values);
        } else if (form.rfind("team:", 0) == 0) {
            std::string key = form.substr(5);
            J::Value &t = key == "myteam" ? g.config["myteam"] : g.config["teams"][key];
            apply_team(t, values);
            if (key != "myteam" && t.size() == 0)
                g.config["teams"].erase(key);
        } else if (form == "extras") {
            if (values["mode"].is_string()) // 1.0.45: the mode only; its parts aren't switched one by one any more
                g.config["ferrari"] = J::Value::object({{"mode", std::string(values["mode"].as_string())}});
            if (values["papaya"].is_bool()) {
                if (!g.config["mclaren"].is_object())
                    g.config["mclaren"] = J::Value::object();
                g.config["mclaren"]["identical"] = values["papaya"].as_bool();
            }
        } else {
            return "reload";
        }
        if (!g.save())
            return J::Value::object({{"notice", "Could not save config.json"}});
        return "reload";
    }
    return "reload";
}

// ---------------------------------------------------------------------------------------------------------
// Build: hand config.json to the game folder, and keep the addon settings for the page.

J::Value build(const J::Value &params)
{
    g.load();
    (void)params;
    g.save(false);  // never into the game folder here: the build's file.put does it
    J::Value ops = J::Value::array({J::Value::object({{"op", "file.put"},
                                                      {"source", J::Value::object({{"root", "data"}, {"path", "config.json"}})},
                                                      {"target", std::string(kGameDir) + "config.json"}})});
    size_t n = 0;
    if (auto *o = g.config["teams"].if_object())
        n = o->size();
    std::string mode(g.config["ferrari"]["mode"].as_string("off"));
    return J::Value::object({{"ops", std::move(ops)},
                             {"lines", J::Value::array({std::to_string(n) + " team(s) with their own pit stop values, "
                                                        "Ferrari Mode " + mode})}});
}

J::Value summary()
{
    g.load();
    size_t n = 0;
    if (auto *o = g.config["teams"].if_object())
        n = o->size();
    std::string mode(g.config["ferrari"]["mode"].as_string("off"));
    bool papaya = g.config["mclaren"]["identical"].as_bool(false);
    if (!n && mode == "off" && !papaya)
        return J::Value();
    J::Value items = J::Value::array({std::to_string(n) + " team(s) edited"});
    if (mode != "off")
        items.push_back(std::string("Ferrari Mode ") + (mode == "full" ? "Full" : "Light"));
    if (papaya)
        items.push_back("Papaya Rules on");
    return J::Value::object({{"label", "PIT STOPS"}, {"items", std::move(items)}});
}

}  // namespace

int main()
{
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
#endif
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
            continue;  // notifications (profile): nothing to do
        J::Value reply = J::Value::object({{"id", msg["id"]}});
        try {
            const J::Value &p = msg["params"];
            if (method == "hello") {
                g.data_dir = from_utf8(p["data_dir"].as_string());
                g.cache_dir = from_utf8(p["cache_dir"].as_string());
                std::error_code ec;
                fs::create_directories(g.data_dir, ec);
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
                reply["result"] = nullptr;
            } else if (method == "command") {
                reply["result"] = "Open the Pit Stops page in the launcher's sidebar.";
            } else {
                reply["error"] = "unknown method " + method;
            }
        } catch (const std::exception &e) {
            reply = J::Value::object({{"id", msg["id"]}, {"error", e.what()}});
        }
        send(reply);
    }
    return 0;
}
