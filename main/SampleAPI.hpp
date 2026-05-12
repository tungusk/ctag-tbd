/***************
dadamachines TBD-16 — Sample Manager REST API

(c) 2014-2026 Johannes Elias Lohbihler for dadamachines.

Licensed under the GNU General Public License (GPL 3.0):
https://www.gnu.org/licenses/gpl-3.0.txt

A commercial licence is available — contact https://dadamachines.com/contact/

Provided "as is" without any express or implied warranties.
See LICENSE in the repository root for full terms.

SPDX-License-Identifier: GPL-3.0-only
***************/

#pragma once

#include "esp_http_server.h"
#include "esp_err.h"

namespace CTAG {
    namespace REST {
        /**
         * Sample Manager REST API
         *
         * Two URI handlers, consolidated per lead-dev guidance to stay within
         * max_uri_handlers = 20.  Actions are dispatched via query strings:
         *
         *   GET  /api/v2/samples*           — list files / kits / capacity
         *        ?preview=path/name         — stream a WAV for audio preview
         *        ?kit=N                     — switch active kit before listing
         *
         *   POST /api/v2/samples*
         *        ?action=upload&path=X&filename=Y  — binary WAV upload
         *        ?action=manage                    — JSON body: rename/delete/saveKit/createKit/createFolder
         *        ?action=reload                    — trigger PSRAM reload
         */
        class SampleAPI final {
        public:
            SampleAPI() = delete;

            static esp_err_t samples_get_handler(httpd_req_t *req);
            static esp_err_t samples_post_handler(httpd_req_t *req);
        };
    }
}
