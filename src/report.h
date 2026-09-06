#pragma once

#include "poop.h"
#include "term.h"

// Prints "Benchmark N (M runs): argv..." followed by the column-header row.
void report_command_header(const Term *t, const Command *cmd, size_t command_n,
                           size_t total_commands);

// Prints one measurement row. `first_m` is the matching measurement of the
// reference (first) command, or NULL for the first command itself; the delta
// column is emitted only when `command_count > 1`.
void report_measurement(const Term *t, const Measurement *m, const char *name,
                        const Measurement *first_m, size_t command_count);
