/***************
TBD-16 — dadamachines WebUI & REST API

(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include "FavoritesModel.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>

namespace CTAG {
    namespace FAV {
        class Favorites final {
        public:
            Favorites() = delete;
            static string GetAllFavorites();
            static void StoreFavorite(int const &id, const string &fav);
            static void ActivateFavorite(const int &id);
            static void DeactivateFavorite();
        private:
            static FavoritesModel model;
            static int32_t activeFav;
        };
    }
}

