// Robot/storage/calib/calib.h — persistent calibration values.
//
// A calibration file is a plain CLI script in the fs volume: one line per
// value, "<value> <setter-word>", e.g. "1434 vint!". Loading it at boot just
// runs it, so every value is restored by the same word a person would type,
// and `cat calib` shows it in readable form. The loader suppresses the CLI
// prompt for each line it feeds, so a boot-time load prints nothing. Board-agnostic: it needs only
// Robot/storage/fs and the CLI — see Robot/CLAUDE.md.
//
// A module that owns a calibrated value registers it once at startup with
// the CLI word that sets it and a getter for the current value, and calls
// calib_save() after the value changes. calib_load() (board main, after the
// fs is mounted and the CLI is up) restores everything.
//
// Saving rewrites the whole file through a temp file and a rename, so a
// power loss leaves either the old or the new file, never a half-written one.

#ifndef ROBOT_STORAGE_CALIB_H
#define ROBOT_STORAGE_CALIB_H

#include <stdint.h>
#include <stdbool.h>

#define CALIB_MAX   16
#define CALIB_FILE  "calib"

// calib_register — setter_word must be a CLI word taking ( value -- ) that
// sets the calibrated value; get returns its current value. setter_word must
// stay valid (use a string literal). Returns false when the table is full.
bool calib_register(const char *setter_word, int32_t (*get)(void));

// calib_save — rewrite the calibration file from the registered values.
// Returns false (and prints why) if the fs is not mounted or full.
bool calib_save(void);

// calib_load — restore the saved values. A TEA state machine: it feeds the
// file to the CLI one line at a time, waiting for the CLI to go idle between
// lines, so a file of any length loads without overflowing the key queue and
// without blocking other actions. Call any time after fs mount and init_cli();
// the load runs once the scheduler is turning. Key echo is off while it runs,
// and nothing is printed when there is no file.
void calib_load(void);

// CLI words
void calib_cli_save(void);   // ( -- ) write every registered value now

#endif // ROBOT_STORAGE_CALIB_H
