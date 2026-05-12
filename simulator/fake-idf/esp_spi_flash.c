/***************
CTAG TBD >>to be determined<< is an open source eurorack synthesizer module.

A project conceived within the Creative Technologies Arbeitsgruppe of
Kiel University of Applied Sciences: https://www.creative-technologies.de

(c) 2020 by Robert Manzke. All rights reserved.

The CTAG TBD software is licensed under the GNU General Public License
(GPL 3.0), available here: https://www.gnu.org/licenses/gpl-3.0.txt

The CTAG TBD hardware design is released under the Creative Commons
Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0).
Details here: https://creativecommons.org/licenses/by-nc-sa/4.0/

CTAG TBD is provided "as is" without any express or implied warranties.

License and copyright details for specific submodules are included in their
respective component folders / files if different from this license.
***************/

// Desktop "SPI flash" emulation for the simulator: loads the sample-rom .tbd
// file into memory and serves spi_flash_read() from it.  Made robust so a
// missing / empty / stale-and-too-short sample-rom never aborts or crashes the
// simulator — out-of-range reads simply return zeros (the rompler / wavetable
// plugins are silent rather than reading garbage).

#include "esp_spi_flash.h"
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static char  *file_buffer = NULL;
static size_t file_size   = 0;

void spi_flash_emu_init(const char *sromFile) {
    if (file_buffer != NULL) return;          /* already loaded */
    if (NULL == sromFile)    return;
    FILE *f = fopen(sromFile, "rb");
    if (f == NULL) {
        fprintf(stderr, "[spi_flash_emu] sample-rom file not found: '%s' — "
                        "rompler/wavetable plugins will be silent (pass --srom to provide one)\n", sromFile);
        return;                                /* leave file_buffer NULL; reads return zeros */
    }
    if (fseek(f, 0L, SEEK_END) != 0) { fclose(f); return; }
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        fprintf(stderr, "[spi_flash_emu] sample-rom file '%s' is empty — ignoring\n", sromFile);
        return;
    }
    file_buffer = (char *)malloc((size_t)sz + 1);
    if (file_buffer == NULL) {
        fclose(f);
        fprintf(stderr, "[spi_flash_emu] out of memory loading sample-rom '%s' (%ld bytes)\n", sromFile, sz);
        return;
    }
    size_t got = fread(file_buffer, 1, (size_t)sz, f);
    fclose(f);
    file_size = got;
    if (got != (size_t)sz)
        fprintf(stderr, "[spi_flash_emu] short read on sample-rom '%s' (%zu of %ld bytes)\n", sromFile, got, sz);
}

void spi_flash_emu_release(void) {
    if (file_buffer != NULL) { free(file_buffer); file_buffer = NULL; }
    file_size = 0;
}

esp_err_t spi_flash_read(size_t src, void *dstv, size_t size) {
    /* Always succeed; never read out of bounds.  Bytes beyond the loaded
       sample-rom (or all of them, if no rom is loaded) read back as zero. */
    unsigned char *dst = (unsigned char *)dstv;
    if (file_buffer == NULL || src >= file_size) {
        memset(dst, 0, size);
        return ESP_OK;
    }
    size_t avail = file_size - src;
    size_t n     = (size < avail) ? size : avail;
    memcpy(dst, file_buffer + src, n);
    if (n < size) memset(dst + n, 0, size - n);
    return ESP_OK;
}
