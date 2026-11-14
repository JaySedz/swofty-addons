// ERP archives (the game's ERPK format, as Swofty's packages/formats/src/erp.rs reads and writes it): read the
// index and fragments, decompress zstd fragments, and write an archive with stored (uncompressed) fragments.
#pragma once

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#define ZSTD_STATIC_LINKING_ONLY
#include "../../third_party/zstd/lib/zstd.h"

namespace erp {

constexpr uint8_t kZstd = 0x11, kStored = 0x91;
constexpr size_t kHeaderLen = 0x30;

struct Fragment {
    char name[4] = {'t', 'e', 'm', 'p'};
    uint64_t size = 0;     // unpacked
    uint32_t flags = 0;
    uint8_t compression = kStored;
    std::vector<uint8_t> raw;  // as stored in the file

    std::vector<uint8_t> data() const
    {
        bool z = compression == kZstd ||
                 (compression != kStored && raw.size() >= 4 && raw[0] == 0x28 && raw[1] == 0xb5 && raw[2] == 0x2f && raw[3] == 0xfd);
        if (!z)
            return raw;
        std::vector<uint8_t> out(static_cast<size_t>(size));
        size_t n = ZSTD_decompress(out.data(), out.size(), raw.data(), raw.size());
        if (ZSTD_isError(n))
            throw std::runtime_error(std::string("zstd: ") + ZSTD_getErrorName(n));
        out.resize(n);
        return out;
    }
    void set(std::vector<uint8_t> d)
    {
        size = d.size();
        compression = kStored;
        raw = std::move(d);
    }
};

struct Resource {
    std::string name;
    uint8_t kind[16] = {};
    std::vector<uint8_t> unk;
    std::vector<Fragment> fragments;
    uint8_t hash[16] = {};

    std::string kind_name() const
    {
        std::string k(reinterpret_cast<const char *>(kind), 16);
        return k.substr(0, k.find('\0'));
    }
};

struct Archive {
    uint8_t header[kHeaderLen] = {};
    uint32_t version = 3;
    std::vector<Resource> resources;

    Resource *find(const std::string &name)
    {
        for (auto &r : resources)
            if (r.name == name)
                return &r;
        return nullptr;
    }
};

inline std::vector<uint8_t> read_all(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("can't open " + p.string());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Reader {
    const std::vector<uint8_t> &d;
    size_t pos = 0;
    void need(size_t n) const
    {
        if (pos + n > d.size())
            throw std::runtime_error("ERP index runs past the file");
    }
    template <class T> T get()
    {
        need(sizeof(T));
        T v;
        std::memcpy(&v, d.data() + pos, sizeof v);
        pos += sizeof v;
        return v;
    }
    void bytes(void *out, size_t n)
    {
        need(n);
        std::memcpy(out, d.data() + pos, n);
        pos += n;
    }
};

inline Archive parse(const std::vector<uint8_t> &d)
{
    Archive a;
    Reader r{d};
    r.bytes(a.header, kHeaderLen);
    if (std::memcmp(a.header, "ERPK", 4) != 0)
        throw std::runtime_error("not an ERP archive");
    std::memcpy(&a.version, a.header + 4, 4);
    uint64_t data_off = 0;
    std::memcpy(&data_off, a.header + 0x20, 8);
    uint32_t count = r.get<uint32_t>();
    r.get<uint32_t>();
    for (uint32_t i = 0; i < count; i++) {
        Resource res;
        r.get<uint32_t>();
        uint16_t nlen = r.get<uint16_t>();
        std::string name(nlen, '\0');
        r.bytes(name.data(), nlen);
        res.name = name.substr(0, name.find('\0'));
        r.bytes(res.kind, 16);
        res.unk.resize(a.version >= 4 ? 6 : 4);
        r.bytes(res.unk.data(), res.unk.size());
        uint8_t n = r.get<uint8_t>();
        for (uint8_t k = 0; k < n; k++) {
            Fragment f;
            r.bytes(f.name, 4);
            uint64_t off = r.get<uint64_t>();
            f.size = r.get<uint64_t>();
            f.flags = r.get<uint32_t>();
            f.compression = r.get<uint8_t>();
            uint64_t packed = r.get<uint64_t>();
            if (data_off + off + packed > d.size())
                throw std::runtime_error("ERP fragment runs past the file");
            f.raw.assign(d.begin() + ptrdiff_t(data_off + off), d.begin() + ptrdiff_t(data_off + off + packed));
            res.fragments.push_back(std::move(f));
        }
        r.bytes(res.hash, 16);
        a.resources.push_back(std::move(res));
    }
    if (r.pos != data_off)
        throw std::runtime_error("ERP index doesn't end where the data starts");
    return a;
}

inline Archive load(const std::filesystem::path &p) { return parse(read_all(p)); }

// Writes the archive in the layout erp.rs writes: header, index, then the fragments in order.
inline std::vector<uint8_t> serialize(const Archive &a)
{
    std::vector<uint8_t> idx;
    auto put = [&](const void *p, size_t n) { idx.insert(idx.end(), (const uint8_t *)p, (const uint8_t *)p + n); };
    uint32_t count = uint32_t(a.resources.size()), frags = 0;
    for (auto &r : a.resources)
        frags += uint32_t(r.fragments.size());
    put(&count, 4);
    put(&frags, 4);
    uint64_t offset = 0;
    for (auto &r : a.resources) {
        std::string name = r.name;
        name.push_back('\0');
        uint16_t nlen = uint16_t(name.size());
        uint32_t entry_len = uint32_t(2 + nlen + 16 + r.unk.size() + 1 + r.fragments.size() * (4 + 8 + 8 + 4 + 1 + 8) + 16);
        put(&entry_len, 4);
        put(&nlen, 2);
        put(name.data(), nlen);
        put(r.kind, 16);
        put(r.unk.data(), r.unk.size());
        uint8_t n = uint8_t(r.fragments.size());
        put(&n, 1);
        for (auto &f : r.fragments) {
            uint64_t packed = f.raw.size();
            put(f.name, 4);
            put(&offset, 8);
            put(&f.size, 8);
            put(&f.flags, 4);
            put(&f.compression, 1);
            put(&packed, 8);
            offset += packed;
        }
        put(r.hash, 16);
    }
    std::vector<uint8_t> out(kHeaderLen);
    std::memcpy(out.data(), a.header, kHeaderLen);
    uint64_t info_len = idx.size(), data_off = kHeaderLen + info_len, header_len = kHeaderLen;
    std::memcpy(out.data() + 0x10, &header_len, 8);
    std::memcpy(out.data() + 0x18, &info_len, 8);
    std::memcpy(out.data() + 0x20, &data_off, 8);
    out.insert(out.end(), idx.begin(), idx.end());
    for (auto &r : a.resources)
        for (auto &f : r.fragments)
            out.insert(out.end(), f.raw.begin(), f.raw.end());
    return out;
}

}  // namespace erp
