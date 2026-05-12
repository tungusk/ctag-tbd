/***************
TBD-16 — Pico Firmware Update System

(c) 2024-2026 Johannes Elias Lohbihler for dadamachines

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include "sdkconfig.h"
#if CONFIG_TBD_USE_RP2350

#include "esp_err.h"

namespace CTAG::DRIVERS {

/// GPIO4 (RP2350 RESET) + GPIO53 (RP2350 BOOTSEL3/GP23) control.
class PicoReset {
public:
    /// Configure GPIO4 and GPIO53 as outputs.
    static void Init();

    /// Assert RESET (GPIO4 LOW).
    static void ResetAssert();

    /// Release RESET (GPIO4 HIGH).
    static void ResetRelease();

    /// Assert BOOTSEL3 (GPIO53 LOW) — tells picoboot3 to stay in bootloader.
    static void Bootsel3Assert();

    /// Release BOOTSEL3 (GPIO53 HIGH / float) — picoboot3 jumps to app.
    static void Bootsel3Release();

    /// Combined sequence: enter picoboot3 mode.
    /// BOOTSEL3 LOW → RESET LOW 10ms → RESET HIGH → wait 50ms → BOOTSEL3 stays LOW.
    /// Call Bootsel3Release() + ResetAssert/Release after flashing to reboot into app.
    static void EnterPicoboot3Mode();

    /// Reboot Pico into normal app mode (release BOOTSEL3, pulse RESET).
    static void RebootIntoApp();
};

}  // namespace CTAG::DRIVERS

#endif // CONFIG_TBD_USE_RP2350
