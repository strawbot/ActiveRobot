// Robot/storage/fs/fs.h — minimal named-file store on top of a raw
// byte-addressable erase/write/read flash device.
//
// Board-agnostic: talks to flash only through the fs_hw_t function
// pointers the board supplies via fs_bind_hw(). No vendor headers, no
// board knowledge — see Robot/CLAUDE.md.
//
// Not a general-purpose filesystem: a fixed-size directory (ping-ponged
// across two sectors for power-loss safety) plus an append-only data log
// over the remaining sectors, compacted on demand when the log fills up.
// Sized for a small volume (a handful of KB) holding a handful of small
// text files — see fs.c for the on-disk layout.

#ifndef ROBOT_STORAGE_FS_H
#define ROBOT_STORAGE_FS_H

#include <stdbool.h>
#include <stdint.h>

// Longest filename this store can hold, including the null terminator.
#define FS_NAME_MAX 16

// Board-supplied raw flash primitives, all covering the whole volume the
// board hands this store (see fs_bind_hw()). Sector size/count and the
// volume's base address are board/hardware facts — Robot/ doesn't assume
// any particular geometry, only that erases happen a whole sector at a
// time and writes/reads can start and end anywhere within a sector.
typedef struct {
    uint32_t sector_size;
    uint32_t sector_count;
    bool (*erase_sector)(uint32_t sector);
    bool (*write)(uint32_t offset, const uint8_t *data, uint32_t len);
    bool (*read)(uint32_t offset, uint8_t *data, uint32_t len);
} fs_hw_t;

// fs_bind_hw — board supplies its raw flash primitives, then this call
// mounts the volume (formatting it if it's blank or the directory is
// unreadable). Call once, before any other fs_* call.
void fs_bind_hw(const fs_hw_t *hw);

// fs_list — print "name  size" for every file in the volume.
void fs_list(void);

bool fs_remove(const char *name);
bool fs_rename(const char *old_name, const char *new_name);
bool fs_copy(const char *src_name, const char *dst_name);

// fs_append_line — append text plus a trailing newline to name, creating
// it if it doesn't exist.
bool fs_append_line(const char *name, const char *text);

// fs_run — stream name's contents through the CLI interpreter one byte at
// a time via keyIn(), as though it had been typed.
bool fs_run(const char *name);

// fs_read — copy up to max bytes of name, starting at offset, into buf.
// Returns the number of bytes copied, 0 at or past the end of the file, or
// -1 if the volume is not ready or the file does not exist. For callers that
// consume a file in pieces (e.g. a state machine that must not queue a whole
// file at once, unlike fs_run).
int fs_read(const char *name, uint32_t offset, uint8_t *buf, uint32_t max);

// fs_cat — print name's contents as raw text.
bool fs_cat(const char *name);

// fs_hexdump — print name's contents as an offset/hex/ASCII dump.
bool fs_hexdump(const char *name);

#endif // ROBOT_STORAGE_FS_H
