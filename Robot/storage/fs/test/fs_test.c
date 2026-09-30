// Host test for fs.c:  cc -Wall -Itest/stub -I. test/fs_test.c fs.c -o fs_test && ./fs_test
//
// RAM-backed fake flash that enforces the real rule: a byte can only be written
// where it is erased (0xFF), and only a sector erase brings it back. A random
// stream of operations runs against a plain in-memory model, with every file
// compared after each step and the volume remounted now and then.

#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS 8
#define SECTOR  1024

static uint8_t flash[SECTORS * SECTOR];
static int violations, erases, fails;

static bool hw_erase(uint32_t s) { if (s >= SECTORS) return false; memset(flash + s * SECTOR, 0xFF, SECTOR); erases++; return true; }
static bool hw_write(uint32_t off, const uint8_t *d, uint32_t n) {
    if (off + n > sizeof flash) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (flash[off + i] != 0xFF) { violations++; return false; }   // write to a non-erased byte
        flash[off + i] = d[i];
    }
    return true;
}
static bool hw_read(uint32_t off, uint8_t *d, uint32_t n) {
    if (off + n > sizeof flash) return false;
    memcpy(d, flash + off, n); return true;
}
static const fs_hw_t hw = { SECTOR, SECTORS, hw_erase, hw_write, hw_read };

// stubs for the printing and key hooks fs.c calls
void keyIn(uint8_t c) { (void)c; }
void print(const char *s) { if (getenv("FS_VERBOSE")) fputs(s, stdout); }
void printCr(void) { if (getenv("FS_VERBOSE")) putchar('\n'); }
void printDec(unsigned int n) { if (getenv("FS_VERBOSE")) printf("%u", n); }
void printChar(unsigned char c) { (void)c; }
void printHex2(unsigned int n) { (void)n; }
void printHex4(unsigned int n) { (void)n; }
void printHex(unsigned int n) { (void)n; }
void printnHex(unsigned int n, unsigned int h) { (void)n; (void)h; }

// ---- reference model ----
#define FILES 6
static const char *names[FILES] = { "a", "b", "c", "d", "calib", "calib.tmp" };
static char model[FILES][4096];
static int  mlen[FILES];
static bool mex[FILES];

static void check(const char *what, int step) {
    for (int i = 0; i < FILES; i++) {
        uint8_t buf[4096];
        int n = fs_read(names[i], 0, buf, sizeof buf);
        if (!mex[i]) { if (n != -1) { printf("step %d %s: %s should not exist (n=%d)\n", step, what, names[i], n); fails++; } continue; }
        if (n != mlen[i] || memcmp(buf, model[i], (size_t)mlen[i])) {
            printf("step %d %s: %s content mismatch (got %d bytes, want %d)\n", step, what, names[i], n, mlen[i]); fails++;
        }
    }
}

int main(int argc, char **argv) {
    srand(argc > 1 ? (unsigned)atoi(argv[1]) : 12345);
    memset(flash, 0xFF, sizeof flash);
    fs_bind_hw(&hw);

    bool heavy = argc > 2;
    int steps = 6000;
    for (int step = 0; step < steps; step++) {
        int op = rand() % 100, f = rand() % FILES;
        if (heavy && op >= 4 && op < 96) op = 0;          // heavy: mostly appends, to fill the volume
        char line[64];
        if (op < 55) {                                   // append a line
            int n = rand() % 40;
            for (int i = 0; i < n; i++) line[i] = (char)('a' + rand() % 26);
            line[n] = 0;
            if (mlen[f] + n + 2 > (heavy ? 4000 : 1500)) continue;         // keep files modest so the volume can hold several
            bool ok = fs_append_line(names[f], line);
            if (ok) {
                memcpy(model[f] + mlen[f], line, (size_t)n); mlen[f] += n;
                model[f][mlen[f]++] = '\r'; model[f][mlen[f]++] = '\n';
                mex[f] = true;
            }
        } else if (op < 70) {                            // remove
            bool ok = fs_remove(names[f]);
            if (ok != mex[f]) { printf("step %d remove result\n", step); fails++; }
            if (ok) { mex[f] = false; mlen[f] = 0; }
        } else if (op < 82) {                            // copy f -> g
            int g = rand() % FILES;
            if (f == g || !mex[f]) continue;
            if (fs_copy(names[f], names[g])) { memcpy(model[g], model[f], (size_t)mlen[f]); mlen[g] = mlen[f]; mex[g] = true; }
        } else if (op < 92) {                            // rename f -> g (the calib.tmp -> calib pattern)
            int g = rand() % FILES;
            if (f == g || !mex[f]) continue;
            if (fs_rename(names[f], names[g])) { memcpy(model[g], model[f], (size_t)mlen[f]); mlen[g] = mlen[f]; mex[g] = true; mex[f] = false; mlen[f] = 0; }
        } else {                                         // remount from the same flash image
            fs_bind_hw(&hw);
        }
        check("after op", step);
        if (violations) { printf("step %d: wrote to non-erased flash\n", step); return 1; }
        if (fails > 5) return 1;
    }
    fs_bind_hw(&hw);
    check("final remount", steps);
    printf("%d steps, %d sector erases, %d flash-rule violations, %d failures\n", steps, erases, violations, fails);
    printf(fails || violations ? "FAILED\n" : "all ok\n");
    return fails || violations;
}
