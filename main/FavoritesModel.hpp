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


#include <string>
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "ctagResources.hpp"
#include "SPManagerDataModel.hpp"

using namespace std;
using namespace rapidjson;

namespace CTAG {
    namespace FAV {
        class FavoritesModel final : public CTAG::SP::ctagDataModelBase {
        public:
            string GetAllFavorites();
            string GetFavorite(int const &i);
            string GetFavoriteName(int const &i);
            string GetFavoriteUString(int const &i);
            string GetFavoritePluginID(int const &i, int const &channel);
            int GetFavoritePreset(int const &i, int const &channel);
            void SetFavorite(int const &id, const string &data);

        private:
            Document m;
        };
    }
}
