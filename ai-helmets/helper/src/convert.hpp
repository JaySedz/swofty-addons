// Converts a "career helmet" mod (a repaint of the game's helmet template, charname_helmet_* in
// character_package_shared/drivers/gender/teamname_charname, usually shipped inside a replacement
// game_package/markdown/markdown_system.erp) into a helmet archive of its own, without Helmet Exchange.
//
// It also converts a design archive (a whole helmet design, such as the SERPs template's
// livery_package/unassigned/unassigned_livery18/unassigned_livery18_helmet.erp).
//
// The shell is a stock helmet archive from the player's own game files: the one the mod was painted for
// (detect(): the visor attachments model it carries, else the folder of its helmet texture), or Lewis Hamilton's
// 2026 one when that can't be told. The helmet itself (helmet_bell_hp7) is shared, but each helmet archive has its
// own visor attachments mesh and UVs, so a visor texture only lines up on the archive it was made for:
//  * its folder name (2026-ferrari_lewis_hamilton) becomes a same-length one (a001-ferrari_lewis_hamilton)
//    in every resource name and every string inside the resources, so nothing that refers to it by length or
//    offset moves. The streamed-mip paths of textures that are not replaced keep the original name: they read
//    the game's own .mipmaps files.
//  * its helmet, wing and visor textures are replaced by the mod's (whole texture resources: these carry every
//    mip in the archive and stream nothing), and each texture's view resource takes the mod's mip count.
// Fragments that change are written uncompressed (the format's "stored" fragments); the rest keep their bytes.
// Each changed resource's hash is the MD5 of its fragments' data, as the game's archives have it.
#pragma once

#include "erp.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <map>
#include <optional>
#include <set>

namespace convert {

constexpr const char *kShellToken = "2026-ferrari_lewis_hamilton";
constexpr const char *kShellStem = "lewis_hamilton";
// The SERPs template names its textures after unassigned_livery18 but is painted on replica_hamilton20a's visor.
constexpr const char *kSerpsTex = "unassigned_livery18";
constexpr const char *kSerpsShell = "replica_hamilton20a";
constexpr const char *kShellRel ="2025_asset_groups/character_package/drivers/male/2026-ferrari_lewis_hamilton/lewis_hamilton_helmet.erp";

// A stock helmet archive a mod is converted onto: 2025_asset_groups/<pkg>/<dir>/<token>/<stem>.erp, with the asset
// group asset_groups/<pkg>/<dir>/<token>/<stem>.assetgroup. Default: Lewis Hamilton's 2026 helmet.
struct Shell {
    std::string pkg = "character_package", dir = "drivers/male", token = kShellToken,
                stem = std::string(kShellStem) + "_helmet";
    std::string rel() const { return "2025_asset_groups/" + pkg + "/" + dir + "/" + token + "/" + stem + ".erp"; }
    bool is_default() const { return token == kShellToken && pkg == "character_package"; }
};

struct Detected {
    std::optional<Shell> shell;  // the archive the mod was made on, when it can be told
    std::string from;            // what told it: "visor attachments model" or "helmet texture"
    std::string mod_tex;         // the prefix of the mod's own texture names ("unassigned_livery18"), if any
};

inline void md5(const erp::Resource &r, uint8_t out[16])
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) != 0)
        throw std::runtime_error("MD5 is not available");
    BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    for (auto &f : r.fragments) {
        auto d = f.data();
        BCryptHashData(h, d.data(), ULONG(d.size()), 0);
    }
    BCryptFinishHash(h, out, 16, 0);
    BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
}

inline bool ends_with(const std::string &s, const std::string &t)
{
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

inline size_t replace_all(std::vector<uint8_t> &d, const std::string &from, const std::string &to)
{
    size_t n = 0;
    if (from.size() != to.size() || from.empty())
        return 0;
    for (size_t i = 0; i + from.size() <= d.size();) {
        if (std::memcmp(d.data() + i, from.data(), from.size()) == 0) {
            std::memcpy(d.data() + i, to.data(), to.size());
            i += from.size();
            n++;
        } else
            i++;
    }
    return n;
}

// Does the archive hold a career-helmet template texture?
inline bool is_career(const erp::Archive &a)
{
    for (auto &r : a.resources)
        if (r.name.find("charname_helmet_d.tif") != std::string::npos)
            return true;
    return false;
}

// "eaid://<pkg>/<dir...>/<token>/<sub>/<file>" -> its parts; false when it isn't shaped like that or names one of
// the game's templates. A "<pkg>_shared" package (where design textures live) counts as <pkg>.
inline bool split_eaid(const std::string &name, const std::string &sub, std::string &pkg, std::string &dir,
                       std::string &token, std::string &file)
{
    const std::string pre = "eaid://";
    if (name.rfind(pre, 0) != 0)
        return false;
    size_t q = name.find('?');
    std::string path = name.substr(pre.size(), q == std::string::npos ? std::string::npos : q - pre.size());
    size_t at = path.rfind("/" + sub + "/");
    if (at == std::string::npos)
        return false;
    file = path.substr(at + sub.size() + 2);
    std::string head = path.substr(0, at);  // <pkg>/<dir...>/<token>
    size_t a = head.find('/'), b = head.rfind('/');
    if (a == std::string::npos || b <= a)
        return false;
    pkg = head.substr(0, a);
    dir = head.substr(a + 1, b - a - 1);
    token = head.substr(b + 1);
    if (ends_with(pkg, "_shared"))
        pkg.resize(pkg.size() - 7);
    if (token.find("charname") != std::string::npos || dir.find("gender") != std::string::npos ||
        file.find('/') != std::string::npos)
        return false;
    return !pkg.empty() && !dir.empty() && !token.empty() && !file.empty();
}

// Which stock helmet the mod was painted for. The visor attachments model it carries decides: the SERPs template
// names its textures and mesh after unassigned_livery18 but carries replica_hamilton20a's attachments model and
// buffers, byte for byte. Without one, the folder its helmet texture sits in. shell is empty for career-helmet
// template mods (charname_*) and anything else that names no helmet of its own.
inline Detected detect(const erp::Archive &mod)
{
    Detected d;
    std::string pkg, dir, token, file;
    for (const char *want : {"?model", "?lod=all&ib=0", "?lod=0&mesh"}) {
        for (auto &r : mod.resources) {
            if (r.name.find(std::string("_helmet_attachments.idf") + want) == std::string::npos ||
                !split_eaid(r.name, "idf", pkg, dir, token, file))
                continue;
            Shell s;
            s.pkg = pkg, s.dir = dir, s.token = token;
            s.stem = file.substr(0, file.find("_attachments.idf"));
            d.shell = s;
            d.from = "visor attachments model";
            break;
        }
        if (d.shell)
            break;
    }
    for (auto &r : mod.resources) {
        if (!ends_with(r.name, "_helmet_d.tif.image") || !split_eaid(r.name, "textures", pkg, dir, token, file))
            continue;
        std::string tex = file.substr(0, file.size() - std::strlen("_helmet_d.tif.image"));
        d.mod_tex = tex;
        if (!d.shell && tex == kSerpsTex) {
            // a SERPs design without its attachments model: painted on replica_hamilton20a's visor UVs, not on the
            // stock unassigned_livery18's (a different mesh)
            Shell s;
            s.pkg = "livery_package", s.dir = "replica", s.token = kSerpsShell;
            s.stem = std::string(kSerpsShell) + "_helmet";
            d.shell = s;
            d.from = "SERPs template textures";
        } else if (!d.shell) {
            Shell s;
            s.pkg = pkg, s.dir = dir, s.token = token, s.stem = tex + "_helmet";
            d.shell = s;
            d.from = "helmet texture";
        }
        break;
    }
    // A SERPs design names itself unassigned_livery18, by its attachments model or its textures. That stock helmet
    // is the "The story is yours" design: its helmet_m masks hold that design's colour regions, which a mod's
    // textures don't replace (SERPs' own helmet shaders ignore them), so it would show through. SERPs designs are
    // painted on replica_hamilton20a, whose masks are blank.
    if (d.shell && d.shell->token == kSerpsTex && d.from == "visor attachments model") {
        d.shell->pkg = "livery_package", d.shell->dir = "replica", d.shell->token = kSerpsShell;
        d.shell->stem = std::string(kSerpsShell) + "_helmet";
        d.from = "SERPs template model";
    }
    return d;
}

// The prefix the shell's own textures are named with (lewis_hamilton, replica_hamilton20a): the file before
// _helmet_d in the shell's own texture folder.
inline std::string shell_tex(const erp::Archive &shell, const std::string &shell_token)
{
    std::string pkg, dir, token, file;
    for (auto &r : shell.resources)
        if (ends_with(r.name, "_helmet_d.tif.image") && split_eaid(r.name, "textures", pkg, dir, token, file) &&
            token == shell_token)
            return file.substr(0, file.size() - std::strlen("_helmet_d.tif.image"));
    return {};
}

// A same-length name for a converted helmet's folder: "a001" over the token's first four characters
// (2026-ferrari_lewis_hamilton -> a001-ferrari_lewis_hamilton, replica_hamilton20a -> a001ica_hamilton20a).
inline std::string token_for(const std::string &shell_token, int n)
{
    if (shell_token.size() < 5 || n < 1 || n > 999)
        return {};
    char b[8];
    std::snprintf(b, sizeof b, "a%03d", n);
    return b + shell_token.substr(4);
}

// A converted helmet's archive stem: the shell's, with its token renamed when the stem holds it
// (replica_hamilton20a_helmet -> a001ica_hamilton20a_helmet; lewis_hamilton_helmet stays).
inline std::string stem_for(const std::string &shell_stem, const std::string &shell_token, const std::string &token)
{
    std::string s = shell_stem;
    for (size_t at = s.find(shell_token); at != std::string::npos; at = s.find(shell_token, at + token.size()))
        s.replace(at, shell_token.size(), token);
    return s;
}

// A texture's streamed mip levels live in a loose file beside the archives
// (character_package/drivers/male/2026-redbull_max_verstappen/textures/max_verstappen_helmet_d.tif.2025.mipmaps),
// named in its surface's third fragment as a length-prefixed string. A helmet mod ships its own files at the stock
// paths; a copy of them under a folder of the helmet's own (same length, so the header keeps its size) leaves the
// stock driver's untouched.
struct MipFile {
    std::string path;  // where it sat in the pack, lower case with '/'
    std::filesystem::path file;
};
struct MipMove {
    std::string from, to;  // the stream path the mod named, and the helmet's own
    std::filesystem::path file;
};

inline std::string lower(std::string s)
{
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The stream path in a surface's third fragment, and where it starts.
inline bool stream_path(const std::vector<uint8_t> &d, size_t &at, std::string &path)
{
    std::string s(d.begin(), d.end());
    size_t e = s.find(".mipmaps");
    if (e == std::string::npos)
        return false;
    e += 8;
    size_t b = e;
    while (b > 0 && s[b - 1] >= 0x20 && s[b - 1] < 0x7f)
        b--;
    for (size_t k = std::max<size_t>(b, 1); k + 8 <= e; k++)
        if (static_cast<uint8_t>(s[k - 1]) == e - k) {
            at = k;
            path = s.substr(k, e - k);
            return true;
        }
    return false;
}

// Points a surface whose mod ships its mip file at a copy of it under "<tag><folder minus its first 4>"
// (2026-redbull_max_verstappen -> a001-redbull_max_verstappen). Returns false when the pack has no such file.
inline bool retarget_mips(erp::Resource &img, const std::vector<MipFile> &mips, const std::string &tag, MipMove &move)
{
    if (img.fragments.size() < 3 || tag.size() != 4)
        return false;
    auto d = img.fragments[2].data();
    size_t at = 0;
    std::string path;
    if (!stream_path(d, at, path))
        return false;
    std::string want = lower(path);
    const MipFile *hit = nullptr;
    for (auto &m : mips)
        if (m.path == want || ends_with(m.path, "/" + want))
            hit = &m;
    size_t tex = path.rfind("/textures/");
    if (!hit || tex == std::string::npos)
        return false;
    size_t f = path.rfind('/', tex - 1);
    f = f == std::string::npos ? 0 : f + 1;
    if (tex - f < 5)
        return false;
    std::string to = path;
    to.replace(f, 4, tag);
    std::memcpy(d.data() + at, to.data(), to.size());
    img.fragments[2].set(std::move(d));
    move = {path, to, hit->file};
    return true;
}

// The same for a helmet archive used as it is: a copy written to out with its surfaces on the pack's mip files.
inline std::vector<MipMove> retarget_archive(const std::filesystem::path &in, const std::filesystem::path &out,
                                             const std::vector<MipFile> &mips, const std::string &tag)
{
    std::vector<MipMove> moves;
    erp::Archive a = erp::load(in);
    for (auto &r : a.resources) {
        MipMove m;
        if (r.kind_name() == "GfxSurfaceRes" && retarget_mips(r, mips, tag, m)) {
            md5(r, r.hash);
            moves.push_back(std::move(m));
        }
    }
    if (moves.empty())
        return moves;
    auto bytes = erp::serialize(a);
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("can't write " + out.string());
    return moves;
}

struct Result {
    size_t textures = 0, renamed = 0;
    std::vector<std::string> replaced;
    std::vector<MipMove> mips;  // the mod's mip files the converted textures now name
};

// The mod's texture for a shell texture suffix ("helmet_d"): the template's charname_<x> first, then (for normal
// and specular maps) a design's <design>_<x>, which these mods usually ship alongside.
inline const erp::Resource *mod_texture(const erp::Archive &mod, const std::string &x, const char *ext,
                                        const std::string &mod_tex)
{
    std::string want = "/charname_" + x + ext;
    for (auto &r : mod.resources)
        if (ends_with(r.name, want))
            return &r;
    if (!mod_tex.empty()) {  // a design archive's own textures (unassigned_livery18_visor_attachments_d)
        want = "/" + mod_tex + "_" + x + ext;
        for (auto &r : mod.resources)
            if (ends_with(r.name, want))
                return &r;
    }
    if (x.find("visor") != std::string::npos)
        return nullptr;
    std::string tail = "_" + x + ext;
    for (auto &r : mod.resources)
        if (ends_with(r.name, tail) && r.name.find("charname") == std::string::npos &&
            (x.find("wing") != std::string::npos || r.name.find("_wing_") == std::string::npos))
            return &r;
    return nullptr;
}

inline Result convert(const erp::Archive &mod, erp::Archive shell, const std::string &shell_token,
                      const std::string &token, const std::filesystem::path &out,
                      const std::vector<MipFile> *mips = nullptr)
{
    if (token.size() != shell_token.size())
        throw std::runtime_error("the new helmet name must be as long as the shell's");
    Result res;
    const std::string tex = shell_tex(shell, shell_token), mod_tex = detect(mod).mod_tex;
    if (tex.empty())
        throw std::runtime_error("the helmet shell has no helmet texture of its own");
    // the shell's own textures: <pkg>[_shared]/<dir>/<token>/textures/<tex>_<x>
    auto shell_find = [&](const std::string &x, const char *ext) -> erp::Resource * {
        std::string pkg, dir, t, file;
        for (auto &r : shell.resources)
            if (ends_with(r.name, "/" + tex + "_" + x + ext) && split_eaid(r.name, "textures", pkg, dir, t, file) &&
                t == shell_token)
                return &r;
        return nullptr;
    };
    std::set<std::string> keep_stream;  // surfaces we don't replace: their stream paths stay on the game's files
    // A repaint of the shell's own design (a tier04_livery106 archive converted onto tier04_livery106) also brings
    // its colour masks (_m): the stock design's masks would tint the new paint. Other mods keep the shell's masks
    // (a SERPs design's are the stock unassigned_livery18 ones, which must not reach replica_hamilton20a).
    const bool same_design = !mod_tex.empty() && mod_tex == tex;
    auto mod_own = [&](const std::string &x, const char *ext) -> const erp::Resource * {
        std::string want = "/" + mod_tex + "_" + x + ext;
        for (auto &r : mod.resources)
            if (ends_with(r.name, want))
                return &r;
        return nullptr;
    };
    std::vector<std::pair<std::string, bool>> wanted;  // suffix, a mask (taken only from a same-design repaint)
    for (const char *x : {"helmet_d", "helmet_n", "helmet_s", "helmet_wing_d", "helmet_wing_n", "helmet_wing_s",
                          "visor_attachments_d", "visor_d", "visor_s"})
        wanted.push_back({x, false});
    if (same_design)
        for (const char *x : {"helmet_m", "helmet_wing_m", "visor_attachments_m", "visor_attachments_s"})
            wanted.push_back({x, true});
    for (auto &[xs, own_only] : wanted) {
        const std::string &x = xs;
        erp::Resource *img = shell_find(x, ".tif.image");
        erp::Resource *srv = shell_find(x, ".tif");
        const erp::Resource *mimg = own_only ? mod_own(x, ".tif.image") : mod_texture(mod, x, ".tif.image", mod_tex);
        const erp::Resource *msrv = own_only ? mod_own(x, ".tif") : mod_texture(mod, x, ".tif", mod_tex);
        if (!img || !srv || !mimg || !msrv)
            continue;
        auto view = msrv->fragments.empty() ? std::vector<uint8_t>() : msrv->fragments[0].data();
        if (view.size() < 0x18 || mimg->fragments.size() < 2)
            continue;
        img->fragments = mimg->fragments;  // header, every mip, and (when it has one) an empty stream table
        MipMove mm;
        if (mips && retarget_mips(*img, *mips, token.substr(0, 4), mm))
            res.mips.push_back(std::move(mm));
        std::vector<uint8_t> v(view.begin(), view.begin() + 0x18);
        std::string image_name = img->name;
        v.insert(v.end(), image_name.begin(), image_name.end());
        v.push_back(0);
        srv->fragments[0].set(std::move(v));
        res.textures++;
        res.replaced.push_back(x);
    }
    if (!res.textures)
        throw std::runtime_error("no helmet texture of the mod matches the helmet shell");
    for (auto &r : shell.resources)
        if (r.kind_name() == "GfxSurfaceRes" && r.fragments.size() >= 3) {
            auto d = r.fragments[2].data();
            if (std::string(d.begin(), d.end()).find(shell_token) != std::string::npos)
                keep_stream.insert(r.name);
        }
    // rename the folder everywhere it is written
    for (auto &r : shell.resources) {
        bool changed = false;
        std::string name = r.name;
        for (size_t at = name.find(shell_token); at != std::string::npos; at = name.find(shell_token, at + 1))
            name.replace(at, token.size(), token), changed = true;
        for (size_t i = 0; i < r.fragments.size(); i++) {
            if (i == 2 && keep_stream.count(r.name))
                continue;
            auto d = r.fragments[i].data();
            size_t n = replace_all(d, shell_token, token);
            if (n) {
                r.fragments[i].set(std::move(d));
                res.renamed += n;
                changed = true;
            }
        }
        r.name = name;
        (void)changed;
    }
    for (auto &r : shell.resources)
        md5(r, r.hash);
    auto bytes = erp::serialize(shell);
    std::filesystem::create_directories(out.parent_path());
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("can't write " + out.string());
    return res;
}

}  // namespace convert
