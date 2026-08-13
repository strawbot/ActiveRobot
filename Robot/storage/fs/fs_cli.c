// Robot/storage/fs/fs_cli.c — see fs.h. Thin CLI stack-word wrappers over
// Robot/storage/fs/fs; no storage or hardware knowledge lives here.

#include <string.h>
#include "fs.h"
#include "cli.h"
#include "printers.h"

// postQuote()/parseWord() always parse into the same fixed scratch buffer
// (cli.c's "hp") rather than an advancing one — a second call clobbers the
// first's result. Any word taking more than one name must copy the first
// out to its own buffer before parsing the second.
static void copy_name(char *dst, const char *src) {
    strncpy(dst, src, FS_NAME_MAX - 1);
    dst[FS_NAME_MAX - 1] = '\0';
}

void fs_cli_ls(void) {
    fs_list();
}

void fs_cli_rm(void) {   // ( -- ) rm <name>
    char *name = postQuote();
    if (!fs_remove(name)) { print("nanofs: rm failed"); printCr(); }
}

void fs_cli_mv(void) {   // ( -- ) mv <old> <new>
    char old_name[FS_NAME_MAX];
    copy_name(old_name, postQuote());
    char *new_name = postQuote();
    if (!fs_rename(old_name, new_name)) { print("nanofs: mv failed"); printCr(); }
}

void fs_cli_cp(void) {   // ( -- ) cp <src> <dst>
    char src_name[FS_NAME_MAX];
    copy_name(src_name, postQuote());
    char *dst_name = postQuote();
    if (!fs_copy(src_name, dst_name)) { print("nanofs: cp failed"); printCr(); }
}

void fs_cli_echo(void) {   // ( -- ) echo <name> <text...>
    char name[FS_NAME_MAX];
    copy_name(name, postQuote());
    char *text = (char *)parseWord(0);   // rest of line, verbatim — no quoting
    if (!fs_append_line(name, text)) { print("nanofs: echo failed"); printCr(); }
}

void fs_cli_run(void) {   // ( -- ) run <name>
    char *name = postQuote();
    if (!fs_run(name)) { print("nanofs: run failed"); printCr(); }
}

void fs_cli_cat(void) {   // ( -- ) cat <name>
    char *name = postQuote();
    if (!fs_cat(name)) { print("nanofs: cat failed"); printCr(); }
}

void fs_cli_hexdump(void) {   // ( -- ) hexdump <name>
    char *name = postQuote();
    if (!fs_hexdump(name)) { print("nanofs: hexdump failed"); printCr(); }
}
