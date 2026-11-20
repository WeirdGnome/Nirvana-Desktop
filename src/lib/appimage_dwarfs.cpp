// SPDX-License-Identifier: LGPL-3.0-or-later
//
// libnirvana: DwarFS payloads read through libdwarfs (its reader API of the 0.12/0.13
// series: dwarfs::reader::filesystem_v2). The image is opened at the payload's offset;
// files are read into memory with read_string(), links resolved here, nothing run.
// Built only when meson finds libdwarfs (-Ddwarfs=enabled|auto).

#include "container.h"

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <dwarfs/logger.h>
#include <dwarfs/os_access_generic.h>
#include <dwarfs/reader/filesystem_options.h>
#include <dwarfs/reader/filesystem_v2.h>
#include <dwarfs/reader/metadata_types.h>

namespace {

struct dw {
    dwarfs::null_logger lgr;
    dwarfs::os_access_generic os;
    std::unique_ptr<dwarfs::reader::filesystem_v2> fs;
};

// what the C side holds: a standard-layout struct with the nv_fs first
struct dw_handle {
    nv_fs base;
    dw *impl;
};

dw *self(nv_fs *f) { return reinterpret_cast<dw_handle *>(f)->impl; }

// the inode a path leads to, its links followed; the path it came to in real
int lookup(dw *d, std::string path, dwarfs::reader::inode_view &out, std::string &real)
{
    for (int hops = 0; hops <= NV_MAX_LINKS; hops++) {
        char norm[4096];
        if (nv_path_join("", path.c_str(), norm, sizeof norm) < 0)
            return -ENOENT;
        auto root = d->fs->find(0);  // the root directory's inode
        if (!root)
            return -EIO;
        dwarfs::reader::inode_view cur = *root;
        std::string done;
        std::string rest = norm;
        bool restarted = false;
        while (!rest.empty()) {
            auto slash = rest.find('/');
            std::string comp = rest.substr(0, slash);
            rest = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
            if (!cur.is_directory())
                return -ENOTDIR;
            auto e = d->fs->find(static_cast<int>(cur.inode_num()), comp);
            if (!e)
                return -ENOENT;
            auto ino = e->inode();
            if (ino.is_symlink()) {
                std::error_code ec;
                std::string target = d->fs->readlink(ino, dwarfs::reader::readlink_mode::raw, ec);
                if (ec)
                    return -EIO;
                char joined[4096];
                int k = target[0] == '/' ? nv_path_join("", target.c_str() + 1, joined, sizeof joined)
                                         : nv_path_join(done.c_str(), target.c_str(), joined, sizeof joined);
                if (k < 0)
                    return -ENOENT;
                path = std::string(joined) + (rest.empty() ? "" : "/" + rest);
                restarted = true;
                break;
            }
            done += (done.empty() ? "" : "/") + comp;
            cur = ino;
        }
        if (restarted)
            continue;
        out = cur;
        real = done;
        return 0;
    }
    return -ELOOP;
}

int dw_read(nv_fs *f, const char *path, unsigned char **data, size_t *len, char *real, size_t rlen)
{
    dw *d = self(f);
    try {
        dwarfs::reader::inode_view ino;
        std::string where;
        int r = lookup(d, path, ino, where);
        if (r < 0)
            return r;
        if (ino.is_directory())
            return -EISDIR;
        if (!ino.is_regular_file())
            return -EINVAL;
        auto st = d->fs->getattr(ino);
        if (st.size() < 0 || static_cast<uint64_t>(st.size()) > NV_MAX_FILE)
            return -EFBIG;
        std::error_code ec;
        std::string s = d->fs->read_string(ino.inode_num(), ec);
        if (ec)
            return -EIO;
        auto *buf = static_cast<unsigned char *>(std::malloc(s.size() + 1));
        if (!buf)
            return -ENOMEM;
        std::memcpy(buf, s.data(), s.size());
        buf[s.size()] = 0;
        if (real && rlen)
            std::snprintf(real, rlen, "%s", where.c_str());
        *data = buf;
        *len = s.size();
        return 0;
    } catch (...) {
        return -EIO;
    }
}

int dw_list_root(nv_fs *f, char ***names, size_t *count)
{
    dw *d = self(f);
    try {
        auto root = d->fs->find(0);
        if (!root)
            return -EIO;
        auto dir = d->fs->opendir(*root);
        if (!dir)
            return -EIO;
        std::vector<std::string> list;
        size_t n = d->fs->dirsize(*dir);
        for (size_t i = 0; i < n; i++) {
            auto e = d->fs->readdir(*dir, i);
            if (!e)
                continue;
            std::string name = e->name();
            if (name == "." || name == "..")
                continue;
            auto ino = e->inode();
            if (ino.is_regular_file() || ino.is_symlink())
                list.push_back(name);
        }
        auto **out = static_cast<char **>(std::calloc(list.size() ? list.size() : 1, sizeof(char *)));
        if (!out)
            return -ENOMEM;
        for (size_t i = 0; i < list.size(); i++) {
            if (!(out[i] = strdup(list[i].c_str()))) {
                nv_names_free(out, i);
                return -ENOMEM;
            }
        }
        *names = out;
        *count = list.size();
        return 0;
    } catch (...) {
        return -EIO;
    }
}

void dw_close(nv_fs *f)
{
    auto *h = reinterpret_cast<dw_handle *>(f);
    delete h->impl;
    delete h;
}

const nv_fs_ops dw_ops = {dw_read, dw_list_root, dw_close};

}  // namespace

extern "C" int nv_dwarfs_open(const char *file, uint64_t offset, nv_fs **out)
{
    try {
        auto d = std::make_unique<dw>();
        dwarfs::reader::filesystem_options opts;
        opts.image_offset = static_cast<dwarfs::file_off_t>(offset);
        d->fs = std::make_unique<dwarfs::reader::filesystem_v2>(d->lgr, d->os, file, opts);
        auto *h = new dw_handle{{&dw_ops}, d.release()};
        *out = &h->base;
        return 0;
    } catch (...) {
        return -EIO;
    }
}
