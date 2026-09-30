// Robot/storage/fs/fs.c — see fs.h.
//
// On-disk layout (sector size/count come from fs_hw_t, chosen by the
// board): sectors 0 and 1 are a ping-ponged directory — every mutation
// erases whichever of the two isn't currently active and writes the full
// new directory there, entries first and the {magic, generation, count}
// header last, so a power loss mid-write leaves an invalid header and
// mount falls back to the other, still-intact sector. Sectors 2..N are an
// append-only data log: file content is never modified in place, only
// ever written fresh at the current tail, so cp is just a directory entry
// pointing at the same bytes as another (copy-on-write, no data write at
// all). Deletes and superseded writes leave garbage in the log, reclaimed
// by compact() when the tail runs out of room.

#include <string.h>
#include "fs.h"
#include "cli.h"       // keyIn() for fs_run
#include "printers.h"

#define FS_MAX_FILES 32
#define FS_DIR_MAGIC 0x46533031u // "FS01"
#define FS_DIR_SECTOR_A 0u
#define FS_DIR_SECTOR_B 1u
#define FS_DATA_START_SECTOR 2u

// One static buffer, the size of a flash sector, is all the RAM the store
// needs beyond its directory. It stages one destination sector at a time
// while compacting, and is the chunk buffer when an append has to copy a file
// to the log tail. (Mount reads only the two directory headers and then the
// winning directory straight into place; a file that is already the last thing
// in the log is extended in place without any copy.) fs_bind_hw() refuses a
// board whose sector is larger than this.
#define FS_BUF_BYTES 1024

typedef struct {
    char name[FS_NAME_MAX];
    uint32_t offset; // within the data region
    uint32_t length;
} fs_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t generation;
    uint32_t count;
} fs_dir_header_t;

static uint8_t g_buf[FS_BUF_BYTES];

static const fs_hw_t *g_hw;
static uint32_t g_dir_sector;
static uint32_t g_generation;
static uint32_t g_write_ptr; // next free offset within the data region
static fs_entry_t g_entries[FS_MAX_FILES];
static uint32_t g_entry_count;

static uint32_t data_region_start(void) {
    return FS_DATA_START_SECTOR * g_hw->sector_size;
}

static uint32_t data_region_size(void) {
    return (g_hw->sector_count - FS_DATA_START_SECTOR) * g_hw->sector_size;
}

static bool fs_ready(void) {
    if (!g_hw) {
        print("nanofs: not mounted"); printCr();
        return false;
    }
    return true;
}

static int find(const char *name) {
    for (uint32_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static void remove_at(int idx) {
    memmove(&g_entries[idx], &g_entries[idx + 1],
            (g_entry_count - (uint32_t)idx - 1) * sizeof(fs_entry_t));
    g_entry_count--;
}

// Valid directory header in `sector`, or false.
static bool dir_header(uint32_t sector, fs_dir_header_t *hdr) {
    if (!g_hw->read(sector * g_hw->sector_size, (uint8_t *)hdr, sizeof(*hdr))) return false;
    return hdr->magic == FS_DIR_MAGIC && hdr->count <= FS_MAX_FILES;
}

// Erases whichever directory sector isn't current and writes the new
// state there (entries first, header last — see file header comment).
static bool dir_persist(void) {
    uint32_t target = (g_dir_sector == FS_DIR_SECTOR_A) ? FS_DIR_SECTOR_B : FS_DIR_SECTOR_A;
    if (!g_hw->erase_sector(target)) return false;

    uint32_t base = target * g_hw->sector_size;
    if (g_entry_count > 0 &&
        !g_hw->write(base + sizeof(fs_dir_header_t), (const uint8_t *)g_entries,
                      g_entry_count * sizeof(fs_entry_t))) {
        return false;
    }

    fs_dir_header_t hdr = { FS_DIR_MAGIC, g_generation + 1, g_entry_count };
    if (!g_hw->write(base, (const uint8_t *)&hdr, sizeof(hdr))) return false;

    g_dir_sector = target;
    g_generation += 1;
    return true;
}

// The tail is the first erased byte after the last programmed one, never less
// than where it already is. Not just the end of the last live file: a deleted
// file at the end of the log leaves programmed bytes behind, and so does a
// write that failed partway; writing over either is not possible in flash.
static void scan_tail(void) {
    for (uint32_t i = data_region_size(); i > g_write_ptr; i--) {
        uint8_t b;
        if (!g_hw->read(data_region_start() + i - 1, &b, 1)) return;
        if (b != 0xFF) { g_write_ptr = i; return; }
    }
}

static bool mount(void) {
    fs_dir_header_t hdr_a, hdr_b;
    bool valid_a = dir_header(FS_DIR_SECTOR_A, &hdr_a);
    bool valid_b = dir_header(FS_DIR_SECTOR_B, &hdr_b);
    if (!valid_a && !valid_b) return false;

    bool use_b = valid_b && (!valid_a || hdr_b.generation > hdr_a.generation);
    const fs_dir_header_t *hdr = use_b ? &hdr_b : &hdr_a;
    uint32_t sector = use_b ? FS_DIR_SECTOR_B : FS_DIR_SECTOR_A;

    if (hdr->count > 0 &&
        !g_hw->read(sector * g_hw->sector_size + sizeof(*hdr), (uint8_t *)g_entries,
                    hdr->count * sizeof(fs_entry_t))) {
        return false;
    }
    g_dir_sector = sector;
    g_generation = hdr->generation;
    g_entry_count = hdr->count;

    g_write_ptr = 0;
    for (uint32_t i = 0; i < g_entry_count; i++) {
        uint32_t end = g_entries[i].offset + g_entries[i].length;
        if (end > g_write_ptr) g_write_ptr = end;
    }
    scan_tail();
    return true;
}

static bool format(void) {
    if (!g_hw->erase_sector(FS_DIR_SECTOR_A) || !g_hw->erase_sector(FS_DIR_SECTOR_B)) return false;
    for (uint32_t s = FS_DATA_START_SECTOR; s < g_hw->sector_count; s++) {
        if (!g_hw->erase_sector(s)) return false;
    }
    g_entry_count = 0;
    g_write_ptr = 0;
    g_generation = 0;
    g_dir_sector = FS_DIR_SECTOR_B; // dir_persist() targets the *other* sector, i.e. A, first
    return dir_persist();
}

void fs_bind_hw(const fs_hw_t *hw) {
    g_hw = hw;
    if (g_hw->sector_count < FS_DATA_START_SECTOR + 1) {
        print("nanofs: volume too small"); printCr();
        g_hw = NULL;
        return;
    }
    if (g_hw->sector_size > FS_BUF_BYTES) {
        print("nanofs: sector exceeds FS_BUF_BYTES"); printCr();
        g_hw = NULL;
        return;
    }
    if (!mount() && !format()) {
        print("nanofs: format failed"); printCr();
        g_hw = NULL;
    }
}

// Repacks the live data from offset 0, one destination sector at a time.
//
// Live regions are taken in ascending source order and packed end to end, so
// every byte moves to an offset <= where it came from. Building destination
// sector d therefore reads only source bytes at offsets >= d * sector, in
// sectors that have not been erased yet; d is erased and rewritten only after
// its contents are staged in g_buf. Entries that share bytes (fs_copy) are one
// region, so packing never needs more room than the source held.
static bool compact(void) {
    uint32_t ss = g_hw->sector_size;

    // Entry indices ordered by source offset, longer first on a tie so a
    // shorter entry that shares its start (an fs_copy of a file that was later
    // extended in place) comes after the region that contains it.
    uint8_t order[FS_MAX_FILES];
    for (uint32_t i = 0; i < g_entry_count; i++) {
        uint32_t j = i;
        while (j) {
            const fs_entry_t *p = &g_entries[order[j - 1]], *e = &g_entries[i];
            if (p->offset < e->offset || (p->offset == e->offset && p->length >= e->length)) break;
            order[j] = order[j - 1]; j--;
        }
        order[j] = (uint8_t)i;
    }

    // Unique regions and their new offsets. An entry that lies inside the
    // previous region is part of it (same bytes), not a second copy.
    uint32_t src[FS_MAX_FILES], len[FS_MAX_FILES], dst[FS_MAX_FILES];
    uint32_t nreg = 0, total = 0;
    uint32_t new_offset[FS_MAX_FILES];
    for (uint32_t k = 0; k < g_entry_count; k++) {
        const fs_entry_t *e = &g_entries[order[k]];
        if (nreg && e->offset >= src[nreg - 1] &&
            e->offset + e->length <= src[nreg - 1] + len[nreg - 1]) {
            new_offset[order[k]] = dst[nreg - 1] + (e->offset - src[nreg - 1]);
            continue;
        }
        src[nreg] = e->offset; len[nreg] = e->length; dst[nreg] = total;
        new_offset[order[k]] = total;
        total += e->length;
        nreg++;
    }
    // The staging scheme needs every byte to move down (or stay). Damaged or
    // unexpected directory contents could break that: refuse before erasing.
    for (uint32_t r = 0; r < nreg; r++)
        if (dst[r] > src[r]) return false;

    uint32_t dest_sectors = (total + ss - 1) / ss;
    for (uint32_t d = 0; d < dest_sectors; d++) {
        uint32_t lo = d * ss;
        uint32_t hi = (total < lo + ss) ? total : lo + ss;       // dest range [lo, hi)
        for (uint32_t r = 0; r < nreg; r++) {
            uint32_t r_lo = dst[r], r_hi = dst[r] + len[r];
            uint32_t from = r_lo > lo ? r_lo : lo;
            uint32_t to   = r_hi < hi ? r_hi : hi;
            if (from >= to) continue;
            if (!g_hw->read(data_region_start() + src[r] + (from - r_lo), g_buf + (from - lo), to - from))
                return false;
        }
        if (!g_hw->erase_sector(FS_DATA_START_SECTOR + d)) return false;
        if (!g_hw->write(data_region_start() + lo, g_buf, hi - lo)) return false;
    }
    for (uint32_t s = FS_DATA_START_SECTOR + dest_sectors; s < g_hw->sector_count; s++) {
        if (!g_hw->erase_sector(s)) return false;
    }

    for (uint32_t i = 0; i < g_entry_count; i++) g_entries[i].offset = new_offset[i];
    g_write_ptr = total;
    return dir_persist();
}

void fs_list(void) {
    if (!fs_ready()) return;
    uint32_t used = 0;
    for (uint32_t i = 0; i < g_entry_count; i++) {
        print(g_entries[i].name);
        print("  ");
        printDec(g_entries[i].length);
        printCr();
        used += g_entries[i].length;
    }

    uint32_t total = data_region_size();
    uint32_t free_now = total - g_write_ptr;       // free before an automatic compaction would kick in
    uint32_t reclaimable = g_write_ptr - used;      // garbage from deletes/superseded writes

    printDec(g_entry_count); print("/"); printDec(FS_MAX_FILES); print(" files, name <= ");
    printDec(FS_NAME_MAX - 1); print(" chars"); printCr();
    printDec(used); print("/"); printDec(total); print(" bytes used, ");
    printDec(free_now); print(" free");
    if (reclaimable > 0) {
        print(" ("); printDec(reclaimable); print(" reclaimable via compaction)");
    }
    printCr();
}

bool fs_remove(const char *name) {
    if (!fs_ready()) return false;
    int idx = find(name);
    if (idx < 0) return false;
    remove_at(idx);
    return dir_persist();
}

bool fs_rename(const char *old_name, const char *new_name) {
    if (!fs_ready()) return false;
    int idx = find(old_name);
    if (idx < 0) return false;

    int existing = find(new_name);
    if (existing >= 0 && existing != idx) {
        remove_at(existing);
        if (idx > existing) idx--;
    }

    strncpy(g_entries[idx].name, new_name, FS_NAME_MAX - 1);
    g_entries[idx].name[FS_NAME_MAX - 1] = '\0';
    return dir_persist();
}

bool fs_copy(const char *src_name, const char *dst_name) {
    if (!fs_ready()) return false;
    int src = find(src_name);
    if (src < 0) return false;
    fs_entry_t copy = g_entries[src];

    int existing = find(dst_name);
    if (existing >= 0) remove_at(existing);

    if (g_entry_count >= FS_MAX_FILES) {
        print("nanofs: too many files"); printCr();
        return false;
    }
    int idx = (int)g_entry_count++;
    strncpy(g_entries[idx].name, dst_name, FS_NAME_MAX - 1);
    g_entries[idx].name[FS_NAME_MAX - 1] = '\0';
    g_entries[idx].offset = copy.offset;
    g_entries[idx].length = copy.length;
    return dir_persist();
}

bool fs_append_line(const char *name, const char *text) {
    if (!fs_ready()) return false;

    int idx = find(name);
    uint32_t old_len = (idx >= 0) ? g_entries[idx].length : 0;
    uint32_t text_len = (uint32_t)strlen(text);
    // cli.c's line parser ignores bare '\n' and only ends a line on '\r'
    // (see parse_keyq()'s CRETURN case) — fs_run() feeds these bytes
    // through keyIn() as if typed, so without '\r' a stored line would
    // never actually execute, just concatenate into the next one.
    uint32_t add = text_len + 2;                 // +2 for the trailing "\r\n"
    uint32_t new_len = old_len + add;

    if (new_len > data_region_size()) {
        print("nanofs: file too large"); printCr();
        return false;
    }
    if (idx < 0 && g_entry_count >= FS_MAX_FILES) {
        print("nanofs: too many files"); printCr();
        return false;
    }

    // A file that is the last thing in the log grows in place: only the new
    // bytes are written. Anything sharing its old bytes (fs_copy) keeps its own
    // shorter length, so it still sees exactly what it did before.
    bool at_tail = old_len > 0 && g_entries[idx].offset + old_len == g_write_ptr;
    uint32_t need = at_tail ? add : new_len;

    if (g_write_ptr + need > data_region_size()) {
        if (!compact()) return false;
        at_tail = old_len > 0 && g_entries[idx].offset + old_len == g_write_ptr;
        need = at_tail ? add : new_len;
        if (g_write_ptr + need > data_region_size()) {
            print("nanofs: no space"); printCr();
            return false;
        }
    }

    uint32_t base = data_region_start();
    uint32_t new_offset = at_tail ? g_entries[idx].offset : g_write_ptr;
    uint32_t at = g_write_ptr;                   // where the next byte goes

    if (!at_tail) {                              // copy the old content to the tail, in chunks
        for (uint32_t done = 0; done < old_len; ) {
            uint32_t n = old_len - done;
            if (n > FS_BUF_BYTES) n = FS_BUF_BYTES;
            if (!g_hw->read(base + g_entries[idx].offset + done, g_buf, n)) return false;
            if (!g_hw->write(base + at, g_buf, n)) { scan_tail(); return false; }
            done += n; at += n;
        }
    }
    // The new text and its line ending go out through RAM (g_buf): the flash
    // driver will not program from code flash above 128 KB, and callers pass
    // string literals as readily as buffers.
    for (uint32_t done = 0; done < text_len; ) {
        uint32_t n = text_len - done;
        if (n > FS_BUF_BYTES) n = FS_BUF_BYTES;
        memcpy(g_buf, text + done, n);
        if (!g_hw->write(base + at, g_buf, n)) { scan_tail(); return false; }
        done += n; at += n;
    }
    g_buf[0] = '\r'; g_buf[1] = '\n';
    if (!g_hw->write(base + at, g_buf, 2)) { scan_tail(); return false; }
    at += 2;
    g_write_ptr = at;

    if (idx < 0) {
        idx = (int)g_entry_count++;
        strncpy(g_entries[idx].name, name, FS_NAME_MAX - 1);
        g_entries[idx].name[FS_NAME_MAX - 1] = '\0';
    }
    g_entries[idx].offset = new_offset;
    g_entries[idx].length = new_len;
    return dir_persist();
}

bool fs_run(const char *name) {
    if (!fs_ready()) return false;
    int idx = find(name);
    if (idx < 0) return false;

    uint32_t pos = g_entries[idx].offset;
    uint32_t remaining = g_entries[idx].length;
    uint8_t buf[32];
    while (remaining > 0) {
        uint32_t chunk = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        if (!g_hw->read(data_region_start() + pos, buf, chunk)) return false;
        for (uint32_t i = 0; i < chunk; i++) keyIn(buf[i]);
        pos += chunk;
        remaining -= chunk;
    }
    return true;
}

int fs_read(const char *name, uint32_t offset, uint8_t *buf, uint32_t max) {
    if (!fs_ready()) return -1;
    int idx = find(name);
    if (idx < 0) return -1;
    uint32_t len = g_entries[idx].length;
    if (offset >= len) return 0;
    uint32_t n = len - offset;
    if (n > max) n = max;
    if (!g_hw->read(data_region_start() + g_entries[idx].offset + offset, buf, n)) return -1;
    return (int)n;
}

bool fs_cat(const char *name) {
    if (!fs_ready()) return false;
    int idx = find(name);
    if (idx < 0) return false;

    uint32_t pos = g_entries[idx].offset;
    uint32_t remaining = g_entries[idx].length;
    uint8_t buf[32];
    while (remaining > 0) {
        uint32_t chunk = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
        if (!g_hw->read(data_region_start() + pos, buf, chunk)) return false;
        for (uint32_t i = 0; i < chunk; i++) printChar(buf[i]);
        pos += chunk;
        remaining -= chunk;
    }
    return true;
}

bool fs_hexdump(const char *name) {
    if (!fs_ready()) return false;
    int idx = find(name);
    if (idx < 0) return false;

    uint32_t start = g_entries[idx].offset;
    uint32_t length = g_entries[idx].length;
    uint8_t buf[16];
    for (uint32_t off = 0; off < length; off += 16) {
        uint32_t chunk = (length - off < 16) ? (length - off) : 16;
        if (!g_hw->read(data_region_start() + start + off, buf, chunk)) return false;

        printnHex(4, off);
        for (uint32_t i = 0; i < 16; i++) {
            if (i < chunk) printHex2(buf[i]);
            else print("   ");
        }
        print("  ");
        for (uint32_t i = 0; i < chunk; i++) {
            uint8_t c = buf[i];
            printChar((c >= 32 && c < 127) ? c : '.');
        }
        printCr();
    }
    return true;
}
