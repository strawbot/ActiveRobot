// Robot/storage/calib/calib.c — see calib.h.

#include "calib.h"
#include "fs.h"
#include "printers.h"
#include "cli.h"
#include "tea.h"
#include "byteq.h"
#include <string.h>

#define CALIB_TMP "calib.tmp"

static struct { const char *word; int32_t (*get)(void); } items[CALIB_MAX];
static uint8_t count;

bool calib_register(const char *word, int32_t (*get)(void)) {
    if (count >= CALIB_MAX) return false;
    items[count].word = word;
    items[count].get  = get;
    count++;
    return true;
}

// Decimal text for v, into the end of buf; returns the start.
static char *dec(char *end, int32_t v) {
    uint32_t u = v < 0 ? 0U - (uint32_t)v : (uint32_t)v;
    char *p = end;
    *--p = 0;
    do { *--p = (char)('0' + u % 10U); u /= 10U; } while (u);
    if (v < 0) *--p = '-';
    return p;
}

bool calib_save(void) {
    fs_remove(CALIB_TMP);   // a stale temp from an interrupted save
    for (uint8_t i = 0; i < count; i++) {
        char num[16], line[16 + 1 + 24 + 1];
        char *v = dec(num + sizeof num, items[i].get());
        strcpy(line, v);
        strcat(line, " ");
        strncat(line, items[i].word, 24);
        if (!fs_append_line(CALIB_TMP, line)) {
            fs_remove(CALIB_TMP);
            print("calib: save failed (fs not mounted or full)"); printCr();
            return false;
        }
    }
    if (!count) { fs_remove(CALIB_FILE); return true; }
    if (!fs_rename(CALIB_TMP, CALIB_FILE)) {
        print("calib: save failed (rename)"); printCr();
        return false;
    }
    return true;
}

// Loading is a small state machine on the TEA scheduler. The CLI reads keys
// asynchronously (keyIn() only queues; the cli() action parses later), so the
// loader feeds ONE line, then waits until the CLI is idle and its key queue is
// empty before feeding the next. That bounds the queue use to one line whatever
// the file length, and each wait is a short in() so other actions keep running.
// Key echo is off for the duration, and no_prompt() before each line silences
// that line's prompt (the CLI resets it after every line).

#define LINE_MAX_BYTES 64
#define LOAD_POLL_MS   1

static uint32_t load_off;
static bool     loading;
static bool     saved_echo;

static void calib_step(void);

static void calib_done(void) {
    loading = false;
    if (saved_echo) autoEchoOn();
}

static void calib_step(void) {
    if (!cli_is_off() || qbq(keyq)) {   // previous line still being interpreted
        in(msec(LOAD_POLL_MS), calib_step);
        return;
    }
    uint8_t buf[LINE_MAX_BYTES];
    int n = fs_read(CALIB_FILE, load_off, buf, sizeof buf);
    if (n <= 0) { calib_done(); return; }   // end of file, or no file at all

    int len = 0;                            // through the newline, else the whole read
    while (len < n && buf[len++] != '\n') { }
    no_prompt();                            // no prompt for this line
    for (int i = 0; i < len; i++) keyIn(buf[i]);
    load_off += (uint32_t)len;
    in(msec(LOAD_POLL_MS), calib_step);
}

static void calib_startup(void) {
    if (loading) return;
    loading = true;
    load_off = 0;
    saved_echo = autoEchoIs();
    autoEchoOff();
    calib_step();
}

void calib_load(void) {
    static bool named;
    if (!named) { named = true; namedAction(calib_startup); namedAction(calib_step); }
    later(calib_startup);
}

void calib_cli_save(void) {
    if (calib_save()) { print("calib: saved "); printDec0(count); print(" value(s)"); printCr(); }
}
