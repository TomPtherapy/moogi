/* SPDX-License-Identifier: GPL-3.0-only
 * wasi-sdk has no processes: tests/hostsim.c's fork / pipe / waitpid (its test paths, never reached in the
 * browser build) get stubs, so the build does not need Emscripten. */
#ifndef WEB_WASI_WAIT_H
#define WEB_WASI_WAIT_H
static inline int fork(void) { return -1; }
static inline int pipe(int fd[2]) { (void)fd; return -1; }
static inline int waitpid(int pid, int *st, int opt) { (void)pid; (void)st; (void)opt; return -1; }
#define WIFEXITED(s) 0
#define WEXITSTATUS(s) 1
#endif
