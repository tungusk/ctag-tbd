/***************
TBD-16 simulator — FreeRTOS semaphore shim

Single-threaded sim → mutex operations are no-ops. SemaphoreHandle_t is
an opaque pointer; xSemaphoreCreateMutex() returns a sentinel non-null
handle so callers see "creation succeeded".

If real cross-thread synchronisation is ever needed in the sim (e.g. a
worker thread for SD I/O), reimplement these with pthread_mutex_t —
do NOT push #ifdef TBD_SIM into the firmware sources.

(c) 2026 Johannes Elias Lohbihler for dadamachines.
Licensed under GPL-3.0.
***************/

#pragma once

#include "freertos/FreeRTOS.h"

typedef void* SemaphoreHandle_t;

// Sentinel: any non-null pointer so `if (handle != nullptr)` checks pass.
// We return the address of a static int — same address across the program,
// which is fine because Take/Give are no-ops anyway.
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    static int sentinel = 1;
    return (SemaphoreHandle_t)&sentinel;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t /*sem*/, TickType_t /*timeout*/) {
    return pdTRUE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t /*sem*/) {
    return pdTRUE;
}
