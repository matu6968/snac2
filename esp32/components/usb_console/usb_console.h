// ... Previous content remains unchanged

#pragma once

#include <stddef.h>
#include "sdmmc_cmd.h"

typedef void (*usb_console_sync_cb_t)(void *arg);

void usb_console_init(void);
void usb_console_set_sync_callback(usb_console_sync_cb_t cb, void *arg);

// Optional: expose a partition of an SD card as USB MSC.
// The application is responsible for initializing the SD card and locating
// the partition start/count (in 512-byte blocks).
void usb_console_set_msc_sdmmc(sdmmc_card_t *card, unsigned int start_lba, unsigned int lba_count);

// ... Following content remains unchanged

