/***************
TBD-16 — dadamachines WebUI & REST API

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

#include "esp_http_server.h"
#include "esp_err.h"

namespace CTAG {
    namespace REST {
        /**
         * OTA Firmware Update API v2.
         *
         * POST /api/v2/ota
         *   Receives a firmware .bin file, writes it to the next OTA
         *   partition (the one NOT currently running), sets it as boot
         *   partition.  The client should reboot the device after a
         *   successful response.
         *
         * GET /api/v2/ota
         *   Returns JSON with current OTA status:
         *   { "running": "ota_0", "next": "ota_1", "maxSize": 5242880 }
         */
        class OtaAPI final {
        public:
            OtaAPI() = delete;
            static esp_err_t ota_get_handler(httpd_req_t *req);
            static esp_err_t ota_post_handler(httpd_req_t *req);
        };
    }
}
