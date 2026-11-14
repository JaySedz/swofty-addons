// Converts a "career helmet" mod (a repaint of the game's helmet template, charname_helmet_* in
// character_package_shared/drivers/gender/teamname_charname, usually shipped inside a replacement
// game_package/markdown/markdown_system.erp) into a helmet archive of its own, without Helmet Exchange.
//
// The shell is a stock driver's helmet archive from the player's own game files (Lewis Hamilton's 2026 one):
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
constexpr const char *kShellRel = "2025_asset_groups/character_package/drivers/male/2026-ferrari_lewis_hamilton/lewis_hamilton_helmet.erp";

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

struct Result {
    size_t textures = 0, renamed = 0;
    std::vector<std::string> replaced;
};

// The mod's texture for a shell texture suffix ("helmet_d"): the template's charname_<x> first, then (for normal
// and specular maps) a design's <design>_<x>, which these mods usually ship alongside.
inline const erp::Resource *mod_texture(const erp::Archive &mod, const std::string &x, const char *ext)
{
    std::string want = "/charname_" + x + ext;
    for (auto &r : mod.resources)
        if (ends_with(r.name, want))
            return &r;
    if (x.find("visor") != std::string::npos)
        return nullptr;
    std::string tail = "_" + x + ext;
    for (auto &r : mod.resources)
        if (ends_with(r.name, tail) && r.name.find("charname") == std::string::npos &&
            (x.find("wing") != std::string::npos || r.name.find("_wing_") == std::string::npos))
            return &r;
    return nullptr;
}

inline Result convert(const erp::Archive &mod, erp::Archive shell, const std::string &token,
                      const std::filesystem::path &out)
{
    if (token.size() != std::strlen(kShellToken))
        throw std::runtime_error("the new helmet name must be as long as the shell's");
    Result res;
    const std::string folder = std::string("eaid://character_package/drivers/male/") + kShellToken + "/textures/" +
                               kShellStem + "_";
    std::set<std::string> keep_stream;  // surfaces we don't replace: their stream paths stay on the game's files
    for (const char *x : {"helmet_d", "helmet_n", "helmet_s", "helmet_wing_d", "helmet_wing_n", "helmet_wing_s",
                          "visor_attachments_d", "visor_d", "visor_s"}) {
        erp::Resource *img = shell.find(folder + x + ".tif.image");
        erp::Resource *srv = shell.find(folder + x + ".tif");
        const erp::Resource *mimg = mod_texture(mod, x, ".tif.image");
        const erp::Resource *msrv = mod_texture(mod, x, ".tif");
        if (!img || !srv || !mimg || !msrv)
            continue;
        auto view = msrv->fragments.empty() ? std::vector<uint8_t>() : msrv->fragments[0].data();
        if (view.size() < 0x18 || mimg->fragments.size() < 2)
            continue;
        img->fragments = mimg->fragments;  // header, every mip, and (when it has one) an empty stream table
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
            if (std::string(d.begin(), d.end()).find(kShellToken) != std::string::npos)
                keep_stream.insert(r.name);
        }
    // rename the folder everywhere it is written
    for (auto &r : shell.resources) {
        bool changed = false;
        std::string name = r.name;
        for (size_t at = name.find(kShellToken); at != std::string::npos; at = name.find(kShellToken, at + 1))
            name.replace(at, token.size(), token), changed = true;
        for (size_t i = 0; i < r.fragments.size(); i++) {
            if (i == 2 && keep_stream.count(r.name))
                continue;
            auto d = r.fragments[i].data();
            size_t n = replace_all(d, kShellToken, token);
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
