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

// Sized for this board's 8x1024-byte volume (6 data sectors = 6144 bytes).
// Shared by mount (reading a candidate directory's entries), compact
// (buffering every live file's bytes across an erase), and append
// (assembling old+new content) — none of these ever run concurrently, so
// one static buffer covers all three instead of three separate ones.
#define FS_SCRATCH_BYTES 6144

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

static union {
    uint8_t bytes[FS_SCRATCH_BYTES];
    fs_entry_t entries[FS_SCRATCH_BYTES / sizeof(fs_entry_t)];
} g_scratch;

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

static bool dir_read_sector(uint32_t sector, fs_dir_header_t *hdr, fs_entry_t *entries) {
    uint32_t base = sector * g_hw->sector_size;
    if (!g_hw->read(base, (uint8_t *)hdr, sizeof(*hdr))) return false;
    if (hdr->magic != FS_DIR_MAGIC) return false;
    if (hdr->count > FS_MAX_FILES) return false;
    if (hdr->count == 0) return true;
    return g_hw->read(base + sizeof(*hdr), (uint8_t *)entries,
                       hdr->count * sizeof(fs_entry_t));
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

static bool mount(void) {
    fs_dir_header_t hdr_a, hdr_b;
    bool valid_a = dir_read_sector(FS_DIR_SECTOR_A, &hdr_a, g_entries);
    bool valid_b = dir_read_sector(FS_DIR_SECTOR_B, &hdr_b, g_scratch.entries);
    if (!valid_a && !valid_b) return false;

    if (valid_b && (!valid_a || hdr_b.generation > hdr_a.generation)) {
        memcpy(g_entries, g_scratch.entries, hdr_b.count * sizeof(fs_entry_t));
        g_dir_sector = FS_DIR_SECTOR_B;
        g_generation = hdr_b.generation;
        g_entry_count = hdr_b.count;
    } else {
        g_dir_sector = FS_DIR_SECTOR_A;
        g_generation = hdr_a.generation;
        g_entry_count = hdr_a.count;
    }

    g_write_ptr = 0;
    for (uint32_t i = 0; i < g_entry_count; i++) {
        uint32_t end = g_entries[i].offset + g_entries[i].length;
        if (end > g_write_ptr) g_write_ptr = end;
    }
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
    if (data_region_size() > FS_SCRATCH_BYTES) {
        print("nanofs: data region exceeds FS_SCRATCH_BYTES"); printCr();
        g_hw = NULL;
        return;
    }
    if (!mount() && !format()) {
        print("nanofs: format failed"); printCr();
        g_hw = NULL;
    }
}

// Buffers every live file's bytes in RAM, erases the whole data region,
// and repacks them from offset 0 — avoids any in-place overlap between
// reading old data and writing the compacted copy.
static bool compact(void) {
    uint32_t total = 0;
    uint32_t new_offsets[FS_MAX_FILES];
    for (uint32_t i = 0; i < g_entry_count; i++) {
        if (!g_hw->read(data_region_start() + g_entries[i].offset,
                         g_scratch.bytes + total, g_entries[i].length)) {
            return false;
        }
        new_offsets[i] = total;
        total += g_entries[i].length;
    }

    for (uint32_t s = FS_DATA_START_SECTOR; s < g_hw->sector_count; s++) {
        if (!g_hw->erase_sector(s)) return false;
    }
    if (total > 0 && !g_hw->write(data_region_start(), g_scratch.bytes, total)) return false;

    for (uint32_t i = 0; i < g_entry_count; i++) g_entries[i].offset = new_offsets[i];
    g_write_ptr = total;
    return true;
}

// Appends content to the data log at the current tail, compacting once if
// it doesn't fit. Returns the offset it was written at.
static bool log_write(const uint8_t *content, uint32_t len, uint32_t *out_offset) {
    if (g_write_ptr + len > data_region_size()) {
        if (!compact() || g_write_ptr + len > data_region_size()) {
            print("nanofs: no space"); printCr();
            return false;
        }
    }
    if (!g_hw->write(data_region_start() + g_write_ptr, content, len)) return false;
    *out_offset = g_write_ptr;
    g_write_ptr += len;
    return true;
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
    uint32_t new_len = old_len + text_len + 2; // +2 for the trailing "\r\n"

    if (new_len > FS_SCRATCH_BYTES) {
        print("nanofs: file too large"); printCr();
        return false;
    }
    if (idx < 0 && g_entry_count >= FS_MAX_FILES) {
        print("nanofs: too many files"); printCr();
        return false;
    }

    if (old_len > 0 &&
        !g_hw->read(data_region_start() + g_entries[idx].offset, g_scratch.bytes, old_len)) {
        return false;
    }
    memcpy(g_scratch.bytes + old_len, text, text_len);
    g_scratch.bytes[old_len + text_len] = '\r';
    g_scratch.bytes[old_len + text_len + 1] = '\n';

    uint32_t new_offset;
    if (!log_write(g_scratch.bytes, new_len, &new_offset)) return false;

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
