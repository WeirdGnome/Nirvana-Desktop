/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * libnirvana: a small read-only SquashFS 4.0 reader, enough to find and read files of an
 * AppImage in memory. Written from the published format description (the superblock, the
 * metadata blocks of 8 KiB with a 16-bit header, the inode and directory tables, the
 * fragment table, data blocks with their size words). Every offset that comes out of the
 * image is checked against its size; a broken image gives -EIO, not a crash.
 *
 * Compressors: gzip (zlib), lzma, xz, lz4, zstd. LZO is not supported (-ENOTSUP).
 */
#define _GNU_SOURCE
#include "container.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lz4.h>
#include <lzma.h>
#include <zlib.h>
#include <zstd.h>

#define SQ_MAGIC 0x73717368u
#define SQ_METADATA_SIZE 8192u
#define SQ_INVALID_FRAG 0xffffffffu

enum {
    SQ_GZIP = 1, SQ_LZMA = 2, SQ_LZO = 3, SQ_XZ = 4, SQ_LZ4 = 5, SQ_ZSTD = 6,
};

enum {
    SQ_DIR = 1, SQ_FILE = 2, SQ_SYMLINK = 3, SQ_BLKDEV = 4, SQ_CHRDEV = 5, SQ_FIFO = 6, SQ_SOCKET = 7,
    SQ_LDIR = 8, SQ_LFILE = 9, SQ_LSYMLINK = 10,
};

typedef struct sq {
    struct nv_fs fs;
    const unsigned char *base;
    size_t size;
    uint32_t block_size;
    uint16_t compressor;
    uint32_t frag_count;
    uint64_t root_inode;
    uint64_t inode_table, dir_table, frag_table;
    /* the last metadata block read, kept (most lookups read the same one again) */
    uint64_t cache_pos;
    size_t cache_len, cache_next;
    unsigned char cache[SQ_METADATA_SIZE];
    bool cache_valid;
} sq;

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const unsigned char *p) { return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32; }

static bool in_image(const sq *s, uint64_t pos, uint64_t len)
{
    return pos <= s->size && len <= s->size - pos;
}

/* one compressed block into dst (dcap bytes at most); its length out */
static int decompress(const sq *s, const unsigned char *src, size_t slen, unsigned char *dst, size_t dcap, size_t *dlen)
{
    switch (s->compressor) {
    case SQ_GZIP: {
        uLongf n = dcap;
        if (uncompress(dst, &n, src, slen) != Z_OK)
            return -EIO;
        *dlen = n;
        return 0;
    }
    case SQ_XZ:
    case SQ_LZMA: {
        lzma_stream st = LZMA_STREAM_INIT;
        lzma_ret r = s->compressor == SQ_XZ ? lzma_stream_decoder(&st, UINT64_MAX, 0)
                                            : lzma_alone_decoder(&st, UINT64_MAX);
        if (r != LZMA_OK)
            return -EIO;
        st.next_in = src;
        st.avail_in = slen;
        st.next_out = dst;
        st.avail_out = dcap;
        r = lzma_code(&st, LZMA_FINISH);
        *dlen = dcap - st.avail_out;
        lzma_end(&st);
        return r == LZMA_STREAM_END || (r == LZMA_OK && st.avail_in == 0) ? 0 : -EIO;
    }
    case SQ_LZ4: {
        int n = LZ4_decompress_safe((const char *)src, (char *)dst, (int)slen, (int)dcap);
        if (n < 0)
            return -EIO;
        *dlen = (size_t)n;
        return 0;
    }
    case SQ_ZSTD: {
        size_t n = ZSTD_decompress(dst, dcap, src, slen);
        if (ZSTD_isError(n))
            return -EIO;
        *dlen = n;
        return 0;
    }
    default:
        return -ENOTSUP;
    }
}

/* the metadata block at pos (from the image's start) into the cache */
static int md_block(sq *s, uint64_t pos)
{
    if (s->cache_valid && s->cache_pos == pos)
        return 0;
    if (!in_image(s, pos, 2))
        return -EIO;
    uint16_t hdr = le16(s->base + pos);
    size_t clen = hdr & 0x7fff;
    if (clen == 0 || !in_image(s, pos + 2, clen))
        return -EIO;
    const unsigned char *src = s->base + pos + 2;
    s->cache_valid = false;
    if (hdr & 0x8000) {
        if (clen > SQ_METADATA_SIZE)
            return -EIO;
        memcpy(s->cache, src, clen);
        s->cache_len = clen;
    } else {
        int r = decompress(s, src, clen, s->cache, SQ_METADATA_SIZE, &s->cache_len);
        if (r < 0)
            return r;
    }
    s->cache_pos = pos;
    s->cache_next = pos + 2 + clen;
    s->cache_valid = true;
    return 0;
}

/* a metadata cursor: the block's position in the image and the offset in it */
typedef struct md_cursor {
    uint64_t block;
    size_t offset;
} md_cursor;

static int md_read(sq *s, md_cursor *c, void *dst, size_t n)
{
    unsigned char *out = dst;
    while (n) {
        int r = md_block(s, c->block);
        if (r < 0)
            return r;
        if (c->offset >= s->cache_len) {
            if (c->offset > s->cache_len)
                return -EIO;
            c->block = s->cache_next;
            c->offset = 0;
            continue;
        }
        size_t k = s->cache_len - c->offset;
        if (k > n)
            k = n;
        if (out) {
            memcpy(out, s->cache + c->offset, k);
            out += k;
        }
        c->offset += k;
        n -= k;
    }
    return 0;
}

/* ---------------------------------------------------------------- inodes */

typedef struct sq_inode {
    uint16_t type;
    /* a directory */
    uint32_t dir_block, dir_size;
    uint16_t dir_offset;
    /* a file */
    uint64_t blocks_start, file_size;
    uint32_t frag_index, frag_offset;
    md_cursor sizes; /* where its blocks' size words are */
    /* a link */
    char *target;
} sq_inode;

static int read_inode(sq *s, uint64_t ref, sq_inode *ino)
{
    memset(ino, 0, sizeof *ino);
    md_cursor c = {s->inode_table + (ref >> 16), (size_t)(ref & 0xffff)};
    unsigned char h[16], b[40];
    int r = md_read(s, &c, h, 16);
    if (r < 0)
        return r;
    ino->type = le16(h);
    switch (ino->type) {
    case SQ_DIR:
        if ((r = md_read(s, &c, b, 16)) < 0)
            return r;
        ino->dir_block = le32(b);
        ino->dir_size = le16(b + 8);
        ino->dir_offset = le16(b + 10);
        return 0;
    case SQ_LDIR:
        if ((r = md_read(s, &c, b, 24)) < 0)
            return r;
        ino->dir_size = le32(b + 4);
        ino->dir_block = le32(b + 8);
        ino->dir_offset = le16(b + 18);
        return 0;
    case SQ_FILE:
        if ((r = md_read(s, &c, b, 16)) < 0)
            return r;
        ino->blocks_start = le32(b);
        ino->frag_index = le32(b + 4);
        ino->frag_offset = le32(b + 8);
        ino->file_size = le32(b + 12);
        ino->sizes = c;
        return 0;
    case SQ_LFILE:
        if ((r = md_read(s, &c, b, 40)) < 0)
            return r;
        ino->blocks_start = le64(b);
        ino->file_size = le64(b + 8);
        ino->frag_index = le32(b + 28);
        ino->frag_offset = le32(b + 32);
        ino->sizes = c;
        return 0;
    case SQ_SYMLINK:
    case SQ_LSYMLINK: {
        if ((r = md_read(s, &c, b, 8)) < 0)
            return r;
        uint32_t n = le32(b + 4);
        if (n > 4096)
            return -EIO;
        ino->target = malloc(n + 1);
        if (!ino->target)
            return -ENOMEM;
        if ((r = md_read(s, &c, ino->target, n)) < 0) {
            free(ino->target);
            ino->target = NULL;
            return r;
        }
        ino->target[n] = 0;
        return 0;
    }
    default:
        return 0; /* devices, fifos, sockets: nothing to read */
    }
}

static bool is_dir(const sq_inode *i) { return i->type == SQ_DIR || i->type == SQ_LDIR; }
static bool is_file(const sq_inode *i) { return i->type == SQ_FILE || i->type == SQ_LFILE; }
static bool is_link(const sq_inode *i) { return i->type == SQ_SYMLINK || i->type == SQ_LSYMLINK; }

/* the entries of a directory, each given to fn (which returns 1 to stop) */
typedef int (*entry_fn)(const char *name, uint64_t ref, uint16_t type, void *data);

static int dir_walk(sq *s, const sq_inode *dir, entry_fn fn, void *data)
{
    if (dir->dir_size <= 3)
        return 0; /* empty: the size counts "." and ".." as 3 bytes */
    md_cursor c = {s->dir_table + dir->dir_block, dir->dir_offset};
    size_t left = dir->dir_size - 3;
    unsigned char h[12], e[8];
    char name[257];
    while (left >= 12) {
        int r = md_read(s, &c, h, 12);
        if (r < 0)
            return r;
        left -= 12;
        uint32_t count = le32(h) + 1, start = le32(h + 4);
        if (count > 256)
            return -EIO;
        while (count--) {
            if (left < 8 || (r = md_read(s, &c, e, 8)) < 0)
                return r < 0 ? r : -EIO;
            left -= 8;
            size_t nlen = (size_t)le16(e + 6) + 1;
            if (nlen > 256 || left < nlen || (r = md_read(s, &c, name, nlen)) < 0)
                return r < 0 ? r : -EIO;
            left -= nlen;
            name[nlen] = 0;
            uint64_t ref = (uint64_t)start << 16 | le16(e);
            r = fn(name, ref, le16(e + 4), data);
            if (r)
                return r;
        }
    }
    return 0;
}

struct find_data {
    const char *name;
    uint64_t ref;
    bool found;
};

static int find_cb(const char *name, uint64_t ref, uint16_t type, void *data)
{
    (void)type;
    struct find_data *f = data;
    if (strcmp(name, f->name))
        return 0;
    f->ref = ref;
    f->found = true;
    return 1;
}

/* ---------------------------------------------------------------- reading a file */

/* the fragment table's entry i: where the fragment's block is and its size word */
static int fragment(sq *s, uint32_t i, uint64_t *start, uint32_t *size)
{
    if (i >= s->frag_count)
        return -EIO;
    uint64_t ptr = s->frag_table + (uint64_t)(i / 512) * 8;
    if (!in_image(s, ptr, 8))
        return -EIO;
    md_cursor c = {le64(s->base + ptr), (size_t)(i % 512) * 16};
    unsigned char e[16];
    int r = md_read(s, &c, e, 16);
    if (r < 0)
        return r;
    *start = le64(e);
    *size = le32(e + 8);
    return 0;
}

/* a data block (its size word) into dst, which holds a block; the bytes it gave out */
static int data_block(sq *s, uint64_t pos, uint32_t word, unsigned char *dst, size_t want, size_t *got)
{
    size_t len = word & 0xffffff;
    if (len == 0) { /* sparse */
        memset(dst, 0, want);
        *got = want;
        return 0;
    }
    if (!in_image(s, pos, len))
        return -EIO;
    if (word & 0x1000000) {
        if (len > s->block_size)
            return -EIO;
        memcpy(dst, s->base + pos, len);
        *got = len;
        return 0;
    }
    return decompress(s, s->base + pos, len, dst, s->block_size, got);
}

static int read_file(sq *s, sq_inode *ino, unsigned char **data, size_t *len)
{
    if (ino->file_size > NV_MAX_FILE)
        return -EFBIG;
    size_t size = (size_t)ino->file_size, bs = s->block_size;
    bool frag = ino->frag_index != SQ_INVALID_FRAG;
    size_t nblocks = frag ? size / bs : (size + bs - 1) / bs;
    unsigned char *out = malloc(size + 1);
    unsigned char *tmp = malloc(bs);
    int r = 0;
    if (!out || !tmp) {
        r = -ENOMEM;
        goto fail;
    }
    uint64_t pos = ino->blocks_start;
    size_t done = 0;
    md_cursor c = ino->sizes;
    for (size_t i = 0; i < nblocks; i++) {
        unsigned char w[4];
        if ((r = md_read(s, &c, w, 4)) < 0)
            goto fail;
        uint32_t word = le32(w);
        size_t want = size - done < bs ? size - done : bs, got = 0;
        if ((r = data_block(s, pos, word, tmp, bs, &got)) < 0)
            goto fail;
        if ((word & 0xffffff) == 0)
            got = want;
        if (got < want) {
            r = -EIO;
            goto fail;
        }
        memcpy(out + done, tmp, want);
        done += want;
        pos += word & 0xffffff;
    }
    if (frag && done < size) {
        uint64_t fstart;
        uint32_t fword;
        size_t got = 0;
        if ((r = fragment(s, ino->frag_index, &fstart, &fword)) < 0)
            goto fail;
        if ((r = data_block(s, fstart, fword, tmp, bs, &got)) < 0)
            goto fail;
        size_t want = size - done;
        if (ino->frag_offset > got || want > got - ino->frag_offset) {
            r = -EIO;
            goto fail;
        }
        memcpy(out + done, tmp + ino->frag_offset, want);
        done += want;
    }
    if (done != size) {
        r = -EIO;
        goto fail;
    }
    out[size] = 0;
    free(tmp);
    *data = out;
    *len = size;
    return 0;
fail:
    free(out);
    free(tmp);
    return r;
}

/* ---------------------------------------------------------------- paths */

/* the inode a path leads to, its links followed (the last one too when follow_last); the
 * path it came to in real */
static int lookup(sq *s, const char *path, sq_inode *out, char *real, size_t rlen)
{
    char cur[4096];
    if (nv_path_join("", path, cur, sizeof cur) < 0)
        return -ENOENT;
    for (int hops = 0; hops <= NV_MAX_LINKS; hops++) {
        sq_inode dir;
        int r = read_inode(s, s->root_inode, &dir);
        if (r < 0)
            return r;
        char done[4096] = "";
        const char *p = cur;
        bool restarted = false;
        while (*p) {
            const char *slash = strchr(p, '/');
            size_t n = slash ? (size_t)(slash - p) : strlen(p);
            char comp[257];
            if (n == 0 || n > 256) {
                free(dir.target);
                return -ENOENT;
            }
            memcpy(comp, p, n);
            comp[n] = 0;
            const char *rest = slash ? slash + 1 : "";
            if (!is_dir(&dir)) {
                free(dir.target);
                return -ENOTDIR;
            }
            struct find_data f = {comp, 0, false};
            if ((r = dir_walk(s, &dir, find_cb, &f)) < 0)
                return r;
            if (!f.found)
                return -ENOENT;
            sq_inode next;
            if ((r = read_inode(s, f.ref, &next)) < 0)
                return r;
            if (is_link(&next)) {
                /* the link's target, relative to the directory it is in, then the rest */
                char joined[4096], target[4096];
                int k;
                if (next.target[0] == '/')
                    k = nv_path_join("", next.target + 1, target, sizeof target);
                else
                    k = nv_path_join(done, next.target, target, sizeof target);
                free(next.target);
                if (k < 0)
                    return -ENOENT;
                if (*rest)
                    k = snprintf(joined, sizeof joined, "%s%s%s", target, *target ? "/" : "", rest) >= (int)sizeof joined;
                else
                    k = snprintf(joined, sizeof joined, "%s", target) >= (int)sizeof joined;
                if (k)
                    return -ENAMETOOLONG;
                if (nv_path_join("", joined, cur, sizeof cur) < 0)
                    return -ENOENT;
                restarted = true;
                break;
            }
            size_t dl = strlen(done);
            if (dl + n + 2 > sizeof done)
                return -ENAMETOOLONG;
            if (dl)
                done[dl++] = '/';
            memcpy(done + dl, comp, n + 1);
            dir = next;
            p = rest;
        }
        if (restarted)
            continue;
        if (real && rlen) {
            if (strlen(done) + 1 > rlen)
                return -ENAMETOOLONG;
            strcpy(real, done);
        }
        *out = dir;
        return 0;
    }
    return -ELOOP;
}

static int sq_read(nv_fs *fs, const char *path, unsigned char **data, size_t *len, char *real, size_t rlen)
{
    sq *s = (sq *)fs;
    sq_inode ino;
    int r = lookup(s, path, &ino, real, rlen);
    if (r < 0)
        return r;
    if (is_dir(&ino))
        return -EISDIR;
    if (!is_file(&ino))
        return -EINVAL;
    return read_file(s, &ino, data, len);
}

struct list_data {
    char **names;
    size_t count;
    int err;
};

static int list_cb(const char *name, uint64_t ref, uint16_t type, void *data)
{
    (void)ref;
    struct list_data *l = data;
    if (type != SQ_FILE && type != SQ_SYMLINK)
        return 0; /* (directory entries carry the basic types) */
    char **n = realloc(l->names, (l->count + 1) * sizeof *n);
    if (!n || !(n[l->count] = strdup(name))) {
        if (n)
            l->names = n;
        l->err = -ENOMEM;
        return 1;
    }
    l->names = n;
    l->count++;
    return 0;
}

static int sq_list_root(nv_fs *fs, char ***names, size_t *count)
{
    sq *s = (sq *)fs;
    sq_inode root;
    int r = read_inode(s, s->root_inode, &root);
    if (r < 0)
        return r;
    if (!is_dir(&root))
        return -EIO;
    struct list_data l = {0};
    r = dir_walk(s, &root, list_cb, &l);
    if (r < 0 || l.err < 0) {
        nv_names_free(l.names, l.count);
        return r < 0 ? r : l.err;
    }
    *names = l.names;
    *count = l.count;
    return 0;
}

static void sq_close(nv_fs *fs)
{
    free(fs);
}

static const struct nv_fs_ops sq_ops = {sq_read, sq_list_root, sq_close};

int nv_squashfs_open(const unsigned char *base, size_t size, nv_fs **out)
{
    if (size < 96 || le32(base) != SQ_MAGIC)
        return -EINVAL;
    if (le16(base + 28) != 4)
        return -ENOTSUP; /* only version 4 */
    sq *s = calloc(1, sizeof *s);
    if (!s)
        return -ENOMEM;
    s->fs.ops = &sq_ops;
    s->base = base;
    s->size = size;
    s->block_size = le32(base + 12);
    s->frag_count = le32(base + 16);
    s->compressor = le16(base + 20);
    s->root_inode = le64(base + 32);
    uint64_t used = le64(base + 40);
    s->inode_table = le64(base + 64);
    s->dir_table = le64(base + 72);
    s->frag_table = le64(base + 80);
    if (used < s->size)
        s->size = (size_t)used; /* the image is no bigger than it says */
    bool sane = s->block_size >= 4096 && s->block_size <= (1u << 20) &&
                (s->block_size & (s->block_size - 1)) == 0 && s->inode_table < s->size &&
                s->dir_table < s->size;
    if (!sane) {
        free(s);
        return -EIO;
    }
    if (s->compressor != SQ_GZIP && s->compressor != SQ_LZMA && s->compressor != SQ_XZ &&
        s->compressor != SQ_LZ4 && s->compressor != SQ_ZSTD) {
        free(s);
        return -ENOTSUP;
    }
    *out = &s->fs;
    return 0;
}
