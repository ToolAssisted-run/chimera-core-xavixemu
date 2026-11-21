/* emulibc.h - NATIVE build shim. The reference build compiles the very same
 * cinterface.c the guest does; here the waterbox allocation and visibility
 * API degrades to plain malloc and no-op attributes. The guest build never
 * sees this file: its include path has miniBox's real emulibc first.
 */
#ifndef _EMULIBC_H
#define _EMULIBC_H

#include <stdint.h>
#include <stdlib.h>

#define ECL_ENTRY
#define ECL_EXPORT __attribute__((used))
#define ECL_SEALED
#define ECL_INVISIBLE

static inline void *alloc_sealed(size_t size) { return calloc(size, 1); }
static inline void *alloc_invisible(size_t size) { return calloc(size, 1); }
static inline void *alloc_plain(size_t size) { return calloc(size, 1); }

/* The sandbox answers clock_gettime with the second the project's clock
 * starts at. The native reference has no sandbox, so run-native sets the
 * same second here (its --clock-start, default the sandbox's own,
 * 1495889068, 2017-05-27 12:44:28 UTC). */
#define CHIMERA_NATIVE_CLOCK 1
extern int64_t chimera_native_clock_start;
static inline int64_t clock_start_seconds(void) { return chimera_native_clock_start; }

#endif
