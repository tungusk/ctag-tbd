/***************
TBD-16 — dadamachines additions to the CTAG TBD platform

PSRAM-backed STL allocator + map alias.

The base ctagSoundProcessor keeps several large param maps (pMapPar /
pMapCv / pMapTrig) — for big plugins like GrooveBoxRack these grow into
the hundreds of entries.  Routing them through the default heap blows
the ESP32-P4's internal RAM budget (TinyUSB + network already eat the
bulk of it).  This allocator hands every allocation to PSRAM via
heap_caps_malloc(..., MALLOC_CAP_SPIRAM); a malloc() fallback covers
the (very unlikely) PSRAM-exhaustion path so old plugins keep working
on chips that lack PSRAM.

(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD platform by Robert Manzke (CTAG Kiel).

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "esp_heap_caps.h"

namespace CTAG { namespace SP { namespace HELPERS {

template <typename T>
struct PsramAllocator {
    using value_type = T;
    PsramAllocator() noexcept = default;
    template <typename U> PsramAllocator(const PsramAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM);
        if (!p) {
            // Fallback to default malloc when PSRAM is unavailable — keeps
            // GrooveBoxRack working on chips without PSRAM (sim builds, etc.).
            p = std::malloc(n * sizeof(T));
        }
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        heap_caps_free(p);
    }
};

template <class T, class U>
inline bool operator==(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return true; }
template <class T, class U>
inline bool operator!=(const PsramAllocator<T>&, const PsramAllocator<U>&) noexcept { return false; }

template <typename T>
using PsramVector = std::vector<T, PsramAllocator<T>>;

// PSRAM-backed std::map<std::string, V> — routes the red-black tree
// nodes (and the std::function targets they hold) to PSRAM, freeing
// ~30 KB of internal RAM on big plugins like GrooveBoxRack.  The key
// type stays std::string with its default SSO allocator (param IDs
// are typically <16 chars and fit inline anyway).
template <typename V>
using PsramStringMap = std::map<
    std::string, V,
    std::less<std::string>,
    PsramAllocator<std::pair<const std::string, V>>
>;

}}} // namespace CTAG::SP::HELPERS
