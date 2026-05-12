/***************
TBD-16 — Macro/Preset System & GrooveBoxRack

(c) 2025-2026 Per-Olov Jernberg (possan). https://possan.codes
(c) 2024-2026 Johannes Elias Lohbihler for dadamachines.
Based in part on the CTAG TBD DrumRack / engine by Robert Manzke (CTAG Kiel).

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

#include "SpiProtocol.h"
#include <stdint.h>

class SpiProtocolHelper {
private:
  bool nextResponsePrepared;
  bool canPrepareNextResponse;

public:
  SpiProtocolHelper();
  bool shouldPrepareNextResponse();
  void markNextResponsePrepared(p4_spi_response_header *header, p4_spi_response2 *response);

  void updateResponseBeforeSending(p4_spi_response_header *header, p4_spi_response2 *response);
  bool shouldSendPreparedResponse();
  void queuedPreparedResponse();

  bool validateRequestPacket(p4_spi_request_header *header, p4_spi_request2 *request);

  uint16_t calcPayloadCrc(uint8_t *data, uint16_t length);
  uint8_t nextResponseSequenceCounter;
  uint8_t lastSeenRequestCounter;

  uint8_t getNextSequence(uint8_t currentNumber);
  void markRequestSeen(uint8_t seq);
};

#endif // CONFIG_TBD_USE_RP2350

