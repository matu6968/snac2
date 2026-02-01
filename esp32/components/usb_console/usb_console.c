/* Minimal USB CDC console for ESP32-S3 (TinyUSB).
 * Implements a small REPL for instance management.
 */

#include "usb_console.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "esp_heap_caps.h"

#include "xs.h"
#include "snac.h"

#include "tinyusb.h"
#include "tusb.h"

static const char *TAG = "usb_console";

static usb_console_sync_cb_t s_sync_cb = NULL;
static void *s_sync_cb_arg = NULL;

static sdmmc_card_t *s_msc_card = NULL;
static unsigned int s_msc_start_lba = 0;
static unsigned int s_msc_lba_count = 0;

static void cdc_write_str(const char *s);

static volatile int s_worker_busy = 0;

typedef struct {
    char uid[64];
} adduser_args_t;

enum {
    USBCMD_DELUSER = 1,
    USBCMD_RESETPWD,
    USBCMD_QUEUE_USER,
    USBCMD_QUEUE_ALL,
    USBCMD_PURGE_ALL,
};

typedef struct {
    int op;
    char uid[64];
} usercmd_args_t;

static void adduser_worker_task(void *arg)
{
    adduser_args_t *a = (adduser_args_t *)arg;

    cdc_write_str("\r\nadduser: running (worker)\r\n");
    int ret = adduser(a->uid);
    if (ret == 0)
        cdc_write_str("\r\nadduser: done (password printed on log)\r\n");
    else {
        char msg[160];
        int e = errno;
        snprintf(msg, sizeof(msg), "\r\nadduser: failed (errno=%d: %s)\r\n", e, strerror(e));
        cdc_write_str(msg);
    }

    xs_free(a);
    s_worker_busy = 0;
    vTaskDelete(NULL);
}

static void usercmd_worker_task(void *arg)
{
    usercmd_args_t *a = (usercmd_args_t *)arg;
    int opened = 0;
    int ret = 0;

    const char *op = "cmd";
    if (a->op == USBCMD_DELUSER) op = "deluser";
    else if (a->op == USBCMD_RESETPWD) op = "resetpwd";
    else if (a->op == USBCMD_QUEUE_USER) op = "queue";
    else if (a->op == USBCMD_QUEUE_ALL) op = "queue_all";
    else if (a->op == USBCMD_PURGE_ALL) op = "purge_all";

    {
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\n%s: running (worker)\r\n", op);
        cdc_write_str(msg);
    }

    if (p_state != NULL && p_state->srv_running) {
        /* Server already running; reuse open globals. */
        if (srv_config == NULL || srv_basedir == NULL) {
            cdc_write_str("\r\ncmd: server context not ready yet (try again in a moment)\r\n");
            ret = 1;
            goto done;
        }
    }
    else {
        ret = srv_open("/data/snac", 1);
        if (!ret || srv_config == NULL) {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\ncmd: srv_open failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
            ret = 1;
            goto done;
        }
        opened = 1;
    }

    if (a->op == USBCMD_PURGE_ALL) {
        purge_all();
        ret = 0;
    }
    else if (a->op == USBCMD_QUEUE_ALL) {
        int cnt = process_queue();
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\nqueue_all: processed %d items\r\n", cnt);
        cdc_write_str(msg);
        ret = 0;
    }
    else {
        snac user;
        memset(&user, 0, sizeof(user));

        if (!user_open(&user, a->uid)) {
            char msg[160];
            snprintf(msg, sizeof(msg), "\r\n%s: invalid user '%s'\r\n", op, a->uid);
            cdc_write_str(msg);
            ret = 1;
        }
        else {
            if (a->op == USBCMD_DELUSER)
                ret = deluser(&user);
            else if (a->op == USBCMD_RESETPWD)
                ret = resetpwd(&user);
            else if (a->op == USBCMD_QUEUE_USER) {
                int cnt = process_user_queue(&user);
                char msg[96];
                snprintf(msg, sizeof(msg), "\r\nqueue: processed %d items\r\n", cnt);
                cdc_write_str(msg);
                ret = 0;
            }

            user_free(&user);
        }
    }

done:
    if (opened)
        srv_free();

    if (ret == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "\r\n%s: done\r\n", op);
        cdc_write_str(msg);
    }
    else {
        char msg[160];
        int e = errno;
        snprintf(msg, sizeof(msg), "\r\n%s: failed (errno=%d: %s)\r\n", op, e, strerror(e));
        cdc_write_str(msg);
    }

    xs_free(a);
    s_worker_busy = 0;
    vTaskDelete(NULL);
}

void usb_console_set_sync_callback(usb_console_sync_cb_t cb, void *arg)
{
    s_sync_cb = cb;
    s_sync_cb_arg = arg;
}

void usb_console_set_msc_sdmmc(sdmmc_card_t *card, unsigned int start_lba, unsigned int lba_count)
{
    s_msc_card = card;
    s_msc_start_lba = start_lba;
    s_msc_lba_count = lba_count;
}

static void cdc_write_str(const char *s)
{
    if (s == NULL)
        return;

    if (tud_cdc_connected()) {
        const char *p = s;
        size_t left = strlen(s);

        while (left > 0 && tud_cdc_connected()) {
            uint32_t n = tud_cdc_write(p, (uint32_t)left);
            if (n == 0) {
                vTaskDelay(1);
                continue;
            }

            p += n;
            left -= n;
        }

        tud_cdc_write_flush();
    }
}

static void prompt(void)
{
    cdc_write_str("\r\nsnac2> ");
}

static void snac_httpd_task(void *arg)
{
    (void)arg;

    if (!srv_open("/data/snac", 1)) {
        srv_log(xs_dup("ESP32: srv_open failed"));
        vTaskDelete(NULL);
    }

    httpd();
    srv_free();
    vTaskDelete(NULL);
}

static void handle_line(char *line)
{
    while (*line == ' ' || *line == '\t')
        line++;

    if (*line == '\0')
        return;

    if (strcmp(line, "help") == 0) {
        cdc_write_str(
            "\r\nCommands:\r\n"
            "  help          Show this help\r\n"
            "  init          Initialize /data/snac from /config/server.json\r\n"
            "  adduser UID   Add a user (writes into DATA)\r\n"
            "  deluser UID   Delete a user\r\n"
            "  resetpwd UID  Reset a user password (printed on log)\r\n"
            "  queue UID     Process a user queue\r\n"
            "  queue         Process global queue\r\n"
            "  purge         Purge all old data\r\n"
            "  state         Print server state\r\n"
            "  start         Start HTTP server task\r\n"
            "  sync_config   Copy CONFIG->DATA config files (callback)\r\n"
            "  reboot        Restart the ESP32\r\n"
        );
        return;
    }

    if (strcmp(line, "init") == 0) {
        cdc_write_str("\r\ninit: creating /data/snac\r\n");

        struct stat st;
        if (stat("/data", &st) != 0 || !S_ISDIR(st.st_mode)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "\r\ninit: /data is missing (DATA partition not mounted?) (errno=%d: %s)\r\n",
                errno, strerror(errno));
            cdc_write_str(msg);
            return;
        }
        if (stat("/config", &st) != 0 || !S_ISDIR(st.st_mode)) {
            char msg[160];
            snprintf(msg, sizeof(msg),
                "\r\ninit: /config is missing (CONFIG partition not mounted?) (errno=%d: %s)\r\n",
                errno, strerror(errno));
            cdc_write_str(msg);
            return;
        }

        int ret = snac_init_esp32("/data/snac", "/config/server.json");
        if (ret == 0)
            cdc_write_str("\r\ninit: done\r\n");
        else {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\ninit: failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
        }
        return;
    }

    if (strncmp(line, "adduser", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\nadduser: missing UID\r\n");
            return;
        }

        int ret = 0;

        if (p_state != NULL && p_state->srv_running) {
            /* Server already running (auto-start). Reuse open globals; do NOT call srv_open/srv_free. */
            if (srv_config == NULL || srv_basedir == NULL) {
                cdc_write_str("\r\nadduser: server context not ready yet (try again in a moment)\r\n");
                return;
            }

            if (s_worker_busy) {
                cdc_write_str("\r\nadduser: busy\r\n");
                return;
            }

            adduser_args_t *a = xs_realloc(NULL, sizeof(adduser_args_t));
            memset(a, 0, sizeof(*a));
            strncpy(a->uid, uid, sizeof(a->uid) - 1);

            s_worker_busy = 1;
            xTaskCreate(adduser_worker_task, "adduser", 16384, a, 5, NULL);
            return;
        }

        cdc_write_str("\r\nadduser: opening /data/snac\r\n");
        ret = srv_open("/data/snac", 1);
        if (!ret || srv_config == NULL) {
            char msg[160];
            int e = errno;
            snprintf(msg, sizeof(msg),
                "\r\nadduser: srv_open failed (ret=%d, errno=%d: %s)\r\n",
                ret, e, strerror(e));
            cdc_write_str(msg);
            srv_free();
            return;
        }

        cdc_write_str("\r\nadduser: running\r\n");
        ret = adduser(uid);
        if (ret == 0)
            cdc_write_str("\r\nadduser: done (password printed on log)\r\n");
        else
            cdc_write_str("\r\nadduser: failed\r\n");

        srv_free();
        return;
    }

    if (strcmp(line, "state") == 0) {
        if (p_state == NULL) {
            cdc_write_str("\r\nstate: not available\r\n");
            return;
        }

        char msg[320];
        size_t free_8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        snprintf(msg, sizeof(msg),
            "\r\nstate: running=%d threads=%d job_fifo=%d peak_job_fifo=%d heap8_free=%lu psram_free=%lu\r\n",
            p_state->srv_running,
            p_state->n_threads,
            p_state->job_fifo_size,
            p_state->peak_job_fifo_size,
            (unsigned long)free_8,
            (unsigned long)free_psram);
        cdc_write_str(msg);
        return;
    }

    if (strncmp(line, "deluser", 7) == 0 && (line[7] == '\0' || line[7] == ' ' || line[7] == '\t')) {
        char *uid = line + 7;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\ndeluser: missing UID\r\n");
            return;
        }

        if (s_worker_busy) {
            cdc_write_str("\r\ndeluser: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_DELUSER;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "deluser", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "resetpwd", 8) == 0 && (line[8] == '\0' || line[8] == ' ' || line[8] == '\t')) {
        char *uid = line + 8;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (*uid == '\0') {
            cdc_write_str("\r\nresetpwd: missing UID\r\n");
            return;
        }

        if (s_worker_busy) {
            cdc_write_str("\r\nresetpwd: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        a->op = USBCMD_RESETPWD;
        strncpy(a->uid, uid, sizeof(a->uid) - 1);

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "resetpwd", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "queue", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (s_worker_busy) {
            cdc_write_str("\r\nqueue: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        if (*uid == '\0')
            a->op = USBCMD_QUEUE_ALL;
        else {
            a->op = USBCMD_QUEUE_USER;
            strncpy(a->uid, uid, sizeof(a->uid) - 1);
        }

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "queue", 16384, a, 5, NULL);
        return;
    }

    if (strncmp(line, "purge", 5) == 0 && (line[5] == '\0' || line[5] == ' ' || line[5] == '\t')) {
        char *uid = line + 5;
        while (*uid == ' ' || *uid == '\t')
            uid++;

        if (s_worker_busy) {
            cdc_write_str("\r\npurge: busy\r\n");
            return;
        }

        usercmd_args_t *a = xs_realloc(NULL, sizeof(usercmd_args_t));
        memset(a, 0, sizeof(*a));
        if (*uid != '\0') {
            cdc_write_str("\r\npurge: per-user purge not supported\r\n");
            xs_free(a);
            return;
        }

        a->op = USBCMD_PURGE_ALL;

        s_worker_busy = 1;
        xTaskCreate(usercmd_worker_task, "purge", 16384, a, 5, NULL);
        return;
    }

    if (strcmp(line, "start") == 0) {
        if (p_state != NULL && p_state->srv_running) {
            cdc_write_str("\r\nstart: already running\r\n");
            return;
        }
        cdc_write_str("\r\nstart: starting snac2 httpd task\r\n");
        xTaskCreate(snac_httpd_task, "snac2_httpd", 8192, NULL, 5, NULL);
        return;
    }

    if (strcmp(line, "sync_config") == 0) {
        if (s_sync_cb != NULL) {
            cdc_write_str("\r\nsync_config: running\r\n");
            s_sync_cb(s_sync_cb_arg);
            cdc_write_str("\r\nsync_config: done\r\n");
        } else
            cdc_write_str("\r\nsync_config: not configured\r\n");
        return;
    }

    if (strcmp(line, "reboot") == 0) {
        cdc_write_str("\r\nrebooting...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
        return;
    }

    cdc_write_str("\r\nunknown command (try 'help')\r\n");
}

static void usb_console_task(void *arg)
{
    (void)arg;

    cdc_write_str("\r\nsnac2 USB console ready (type 'help')\r\n");
    prompt();

    char line[256];
    size_t n = 0;

    for (;;) {
        if (tud_cdc_available()) {
            uint8_t ch;
            if (tud_cdc_read(&ch, 1) == 1) {
                if (ch == '\r' || ch == '\n') {
                    line[n] = '\0';
                    handle_line(line);
                    n = 0;
                    prompt();
                }
                else if (ch == 0x08 || ch == 0x7f) {
                    if (n > 0)
                        n--;
                }
                else if (n + 1 < sizeof(line)) {
                    line[n++] = (char)ch;
                }
            }
        }

        /* NOTE: pdMS_TO_TICKS(1) can be 0 depending on tick rate, which would
           starve IDLE and trigger the task watchdog. */
        vTaskDelay(1);
    }
}

void usb_console_init(void)
{
    // Install TinyUSB driver (CDC enabled by default in TinyUSB device descriptors).
    tinyusb_config_t cfg = { 0 };
    cfg.task.size = 16384;
    cfg.task.priority = 5;
    cfg.task.xCoreID = 0;
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", (int)err);
        return;
    }

    ESP_LOGI(TAG, "TinyUSB installed");

    xTaskCreate(usb_console_task, "usb_console", 8192, NULL, 5, NULL);
}

#if 1
/* TinyUSB MSC callbacks.
 * These are compiled only when MSC is enabled in the ESP-IDF TinyUSB configuration.
 * They expose a contiguous LBA range (intended for the CONFIG partition).
 */

#include "sdmmc_cmd.h"

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return s_msc_card != NULL && s_msc_lba_count > 0;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_size = 512;
    *block_count = s_msc_lba_count;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "SNAC2   ", 8);
    memcpy(product_id, "CONFIG_VOL      ", 16);
    memcpy(product_rev, "0001", 4);
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    if (s_msc_card == NULL || offset != 0 || (bufsize % 512) != 0)
        return -1;

    uint32_t count = bufsize / 512;
    if (lba + count > s_msc_lba_count)
        return -1;

    esp_err_t err = sdmmc_read_sectors(s_msc_card, buffer, s_msc_start_lba + lba, count);
    return err == ESP_OK ? (int32_t)bufsize : -1;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    if (s_msc_card == NULL || offset != 0 || (bufsize % 512) != 0)
        return -1;

    uint32_t count = bufsize / 512;
    if (lba + count > s_msc_lba_count)
        return -1;

    esp_err_t err = sdmmc_write_sectors(s_msc_card, buffer, s_msc_start_lba + lba, count);
    return err == ESP_OK ? (int32_t)bufsize : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)lun;
    (void)buffer;
    (void)bufsize;

    /* Let TinyUSB handle the default SCSI commands. */
    uint8_t const cmd = scsi_cmd[0];
    ESP_LOGD(TAG, "MSC SCSI cmd 0x%02x", cmd);
    return -1;
}
#endif

