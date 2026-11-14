// erptool: inspect ERP archives (development aid for the AI Helmets converter).
//   erptool list <file.erp>
//   erptool strings <file.erp> <needle>        resources whose decompressed data contains needle, with offsets
//   erptool dump <file.erp> <name-substring> <out-dir>
#include "../helper/src/convert.hpp"

#include <cstdio>
#include <iostream>

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: erptool list|strings|dump <file.erp> ...\n");
        return 2;
    }
    try {
        std::string cmd = argv[1];
        erp::Archive a = erp::load(argv[2]);
        if (cmd == "list") {
            std::printf("version %u, %zu resources\n", a.version, a.resources.size());
            for (auto &r : a.resources) {
                std::printf("%-15s %s", r.kind_name().c_str(), r.name.c_str());
                for (auto &f : r.fragments)
                    std::printf(" [%.4s %llu/%zu %02x f%x]", f.name, (unsigned long long)f.size, f.raw.size(), f.compression, f.flags);
                std::printf("\n");
            }
        } else if (cmd == "strings" && argc >= 4) {
            std::string needle = argv[3];
            for (auto &r : a.resources)
                for (size_t i = 0; i < r.fragments.size(); i++) {
                    auto d = r.fragments[i].data();
                    std::string s(d.begin(), d.end());
                    for (size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1)) {
                        size_t b = at, e = at;
                        while (b > 0 && s[b - 1] >= 0x20 && s[b - 1] < 0x7f)
                            b--;
                        while (e < s.size() && s[e] >= 0x20 && s[e] < 0x7f)
                            e++;
                        std::printf("%s #%zu +0x%zx: %s\n", r.name.c_str(), i, at, s.substr(b, e - b).c_str());
                    }
                }
        } else if (cmd == "dump" && argc >= 5) {
            std::string sub = argv[3];
            std::filesystem::create_directories(argv[4]);
            int n = 0;
            for (auto &r : a.resources)
                if (r.name.find(sub) != std::string::npos)
                    for (size_t i = 0; i < r.fragments.size(); i++) {
                        auto d = r.fragments[i].data();
                        std::string safe;
                        for (char c : r.name)
                            safe += (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') ? c : '_';
                        auto p = std::filesystem::path(argv[4]) / (safe + "." + std::to_string(i));
                        std::ofstream(p, std::ios::binary).write((const char *)d.data(), std::streamsize(d.size()));
                        n++;
                    }
            std::printf("%d fragment(s) written\n", n);
        } else if (cmd == "hashcheck") {
            size_t ok = 0, bad = 0;
            for (auto &r : a.resources) {
                uint8_t h[16];
                convert::md5(r, h);
                (std::memcmp(h, r.hash, 16) == 0 ? ok : bad)++;
            }
            std::printf("%zu resource hash(es) match MD5 of their data, %zu don't\n", ok, bad);
        } else if (cmd == "convert" && argc >= 6) {
            erp::Archive shell = erp::load(argv[3]);
            auto r = convert::convert(a, shell, argv[4], argv[5]);
            std::printf("%zu texture(s) replaced, %zu name(s) renamed\n", r.textures, r.renamed);
            for (auto &x : r.replaced)
                std::printf("  %s\n", x.c_str());
        } else {
            std::fprintf(stderr, "unknown command\n");
            return 2;
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
