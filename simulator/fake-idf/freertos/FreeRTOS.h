/***************
TBD-16 simulator — FreeRTOS shim

The simulator is single-threaded (RtAudio's audio callback + main thread is
all the concurrency we have). Headers that pull `freertos/FreeRTOS.h` only
need a handful of type / macro definitions to compile; the mutex and task
calls reduce to no-ops in this build.

If a sim translation unit ever needs a *real* freertos primitive, replace
the no-op with a pthread-backed equivalent here rather than littering the
device source with `#ifdef TBD_SIM`.

(c) 2026 Johannes Elias Lohbihler for dadamachines.
Licensed under GPL-3.0.
***************/

#pragma once

#include <stdint.h>
#include <stddef.h>

// FreeRTOS type aliases used by the firmware code paths the sim pulls in.
typedef long              BaseType_t;
typedef unsigned long     UBaseType_t;
typedef uint32_t          TickType_t;

// Boolean-ish return codes.
#define pdTRUE            ((BaseType_t)1)
#define pdFALSE           ((BaseType_t)0)
#define pdPASS            pdTRUE
#define pdFAIL            pdFALSE

// Wait-forever ticks. xSemaphoreTake / xQueueReceive ignore the value here
// because the shim's xSemaphoreTake is a no-op, but firmware code still
// names the constant.
#define portMAX_DELAY     ((TickType_t)0xFFFFFFFFu)

// Tick / ms conversion. The sim has no scheduler, so anything that goes
// through these macros never actually delays.
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))

// Priorities — referenced by xTaskCreate-style calls in firmware headers
// the sim transitively includes. Values are irrelevant in the shim.
#define tskIDLE_PRIORITY  0
