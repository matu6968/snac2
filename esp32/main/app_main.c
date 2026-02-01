/* Snac2 ESP32-S3 entrypoint (ESP-IDF).
 * - Mount SD card with two partitions: CONFIG (p1) and DATA (p2)
 * - Expose CONFIG as USB MSC (RW) and provide USB CDC console
 * - Copy config files CONFIG -> DATA/snac before starting Snac2
 */

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "diskio_impl.h"
#include "diskio.h"

#include "usb_console.h"

/* Snac2 headers */
#include "xs.h"
#include "snac.h"

static const char *TAG = "snac2_esp32";

#define MOUNT_CONFIG "/config"
#define MOUNT_DATA   "/data"
#define SNAC_DATA_DIR MOUNT_DATA "/snac"

// SD card GPIOs
// change these if you have a different SD card pinout
#define SD_CS_PIN 10
#define SD_MOSI_PIN 11
#define SD_MISO_PIN 13
#define SD_SCLK_PIN 12

typedef struct {
    sdmmc_card_t *card;
    uint32_t start_lba;
    uint32_t lba_count;
} part_ctx_t;

static part_ctx_t s_part[2] = {0};

static DSTATUS part_init(unsigned char pdrv)
{
    (void)pdrv;
    return 0;
}

static DSTATUS part_status(unsigned char pdrv)
{
    (void)pdrv;
    return 0;
}

static DRESULT part_read(unsigned char pdrv, unsigned char *buff, uint32_t sector, UINT count)
{
    if (pdrv >= 2 || s_part[pdrv].card == NULL)
        return RES_ERROR;

    if (sector + count > s_part[pdrv].lba_count)
        return RES_PARERR;

    esp_err_t err = sdmmc_read_sectors(s_part[pdrv].card, buff, s_part[pdrv].start_lba + sector, count);
    return err == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT part_write(unsigned char pdrv, const unsigned char *buff, uint32_t sector, UINT count)
{
    if (pdrv >= 2 || s_part[pdrv].card == NULL)
        return RES_ERROR;

    if (sector + count > s_part[pdrv].lba_count)
        return RES_PARERR;

    esp_err_t err = sdmmc_write_sectors(s_part[pdrv].card, buff, s_part[pdrv].start_lba + sector, count);
    return err == ESP_OK ? RES_OK : RES_ERROR;
}

static DRESULT part_ioctl(unsigned char pdrv, unsigned char cmd, void *buff)
{
    if (pdrv >= 2 || s_part[pdrv].card == NULL)
        return RES_ERROR;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = s_part[pdrv].lba_count;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

static const ff_diskio_impl_t s_diskio = {
    .init = part_init,
    .status = part_status,
    .read = part_read,
    .write = part_write,
    .ioctl = part_ioctl,
};

static int mbr_parse_partition(const uint8_t mbr[512], int idx, uint32_t *start_lba, uint32_t *lba_count)
{
    if (idx < 0 || idx > 3)
        return 0;

    const uint8_t *p = mbr + 446 + idx * 16;
    uint32_t start = (uint32_t)p[8] | ((uint32_t)p[9] << 8) | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
    uint32_t size  = (uint32_t)p[12] | ((uint32_t)p[13] << 8) | ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24);

    if (start == 0 || size == 0)
        return 0;

    *start_lba = start;
    *lba_count = size;
    return 1;
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return ESP_OK;
        return ESP_FAIL;
    }
    return mkdir(path, 0775) == 0 ? ESP_OK : ESP_FAIL;
}

static esp_err_t copy_file(const char *src, const char *dst)
{
    FILE *fi = fopen(src, "rb");
    if (fi == NULL)
        return ESP_FAIL;

    FILE *fo = fopen(dst, "wb");
    if (fo == NULL) {
        fclose(fi);
        return ESP_FAIL;
    }

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) {
        if (fwrite(buf, 1, n, fo) != n) {
            fclose(fi);
            fclose(fo);
            return ESP_FAIL;
        }
    }

    fclose(fi);
    fclose(fo);
    return ESP_OK;
}

static int sync_config_once(void)
{
    ensure_dir(SNAC_DATA_DIR);

    const char *src = MOUNT_CONFIG "/server.json";
    const char *dst = SNAC_DATA_DIR "/server.json";

    struct stat st;
    if (stat(src, &st) == 0) {
        if (copy_file(src, dst) == ESP_OK) {
            ESP_LOGI(TAG, "synced %s -> %s", src, dst);
            return 0;
        }

        ESP_LOGW(TAG, "failed to sync %s -> %s", src, dst);
        return -1;
    }

    if (stat(dst, &st) == 0) {
        ESP_LOGI(TAG, "using existing %s", dst);
        return 0;
    }

    ESP_LOGW(TAG, "missing %s (create it on CONFIG, then run sync_config/init)", src);
    return -1;
}

static void sync_config_cb(void *arg)
{
    (void)arg;
    (void)sync_config_once();
}

static int time_is_sane(void)
{
    time_t now = 0;
    struct tm tm = {0};
    time(&now);
    localtime_r(&now, &tm);
    return tm.tm_year >= (2024 - 1900);
}

static void wait_for_ip_and_time(esp_netif_t *netif, int wait_ip_ms, int wait_time_ms)
{
    if (netif != NULL) {
        int waited = 0;
        while (waited < wait_ip_ms) {
            esp_netif_ip_info_t ip = {0};
            if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0)
                break;
            vTaskDelay(pdMS_TO_TICKS(200));
            waited += 200;
        }
    }

    if (!time_is_sane()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();

        int waited = 0;
        while (!time_is_sane() && waited < wait_time_ms) {
            vTaskDelay(pdMS_TO_TICKS(200));
            waited += 200;
        }

        if (!time_is_sane())
            ESP_LOGW(TAG, "time not synced (NTP); HTTP signatures may be rejected");
        else {
            time_t now = 0;
            time(&now);
            ESP_LOGI(TAG, "time synced: %ld", (long)now);
        }
    }
}

static void snac_task(void *arg)
{
    (void)arg;

    if (sync_config_once() != 0) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting Snac2 using basedir %s", SNAC_DATA_DIR);
    if (!srv_open(SNAC_DATA_DIR, 1)) {
        ESP_LOGE(TAG, "srv_open failed");
        vTaskDelete(NULL);
        return;
    }

    if (srv_config == NULL) {
        ESP_LOGE(TAG, "srv_config is NULL (server.json missing/invalid?)");
        vTaskDelete(NULL);
        return;
    }

    httpd();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t ret;
    esp_netif_t *sta_netif = NULL;

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#ifdef CONFIG_SNAC_WIFI_ENABLE
    if (strlen(CONFIG_SNAC_WIFI_SSID) > 0) {
        sta_netif = esp_netif_create_default_wifi_sta();

        wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

        wifi_config_t wifi_cfg = { 0 };
        strncpy((char *)wifi_cfg.sta.ssid, CONFIG_SNAC_WIFI_SSID, sizeof(wifi_cfg.sta.ssid));
        strncpy((char *)wifi_cfg.sta.password, CONFIG_SNAC_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());

        wait_for_ip_and_time(sta_netif, 15000, 20000);
    }
#endif

    ESP_LOGI(TAG, "Initializing SD card");
    sdmmc_card_t *card = NULL;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_PIN,
        .miso_io_num = SD_MISO_PIN,
        .sclk_io_num = SD_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 16 * 1024,
    };
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_PIN;
    slot_config.host_id = host.slot;

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return;
    }

    card = (sdmmc_card_t *)calloc(1, sizeof(sdmmc_card_t));
    if (card == NULL) {
        ESP_LOGE(TAG, "no memory for card");
        return;
    }

    ret = sdspi_host_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sdspi_host_init failed: %s", esp_err_to_name(ret));
        return;
    }

    sdspi_dev_handle_t sdspi_handle = 0;
    ret = sdspi_host_init_device((const sdspi_device_config_t *)&slot_config, &sdspi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init_device failed: %s", esp_err_to_name(ret));
        return;
    }

    host.slot = sdspi_handle;

    ret = sdmmc_card_init(&host, card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdmmc_card_init failed: %s", esp_err_to_name(ret));
        return;
    }

    uint8_t mbr[512];
    ret = sdmmc_read_sectors(card, mbr, 0, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed reading MBR");
        return;
    }

    uint32_t cfg_start = 0, cfg_count = 0;
    uint32_t dat_start = 0, dat_count = 0;
    if (!mbr_parse_partition(mbr, 0, &cfg_start, &cfg_count) ||
        !mbr_parse_partition(mbr, 1, &dat_start, &dat_count)) {
        ESP_LOGE(TAG, "expected SD partitions: p1=CONFIG, p2=DATA");
        return;
    }

    s_part[0] = (part_ctx_t){ .card = card, .start_lba = cfg_start, .lba_count = cfg_count };
    s_part[1] = (part_ctx_t){ .card = card, .start_lba = dat_start, .lba_count = dat_count };

    ff_diskio_register(0, &s_diskio);
    ff_diskio_register(1, &s_diskio);

    FATFS *fs_cfg = NULL;
    FATFS *fs_dat = NULL;

    esp_vfs_fat_conf_t cfg = {
        .base_path = MOUNT_CONFIG,
        .fat_drive = "0:",
        .max_files = 4,
    };
    esp_vfs_fat_conf_t dat = {
        .base_path = MOUNT_DATA,
        .fat_drive = "1:",
        .max_files = 8,
    };

    ESP_ERROR_CHECK(esp_vfs_fat_register_cfg(&cfg, &fs_cfg));
    ESP_ERROR_CHECK(esp_vfs_fat_register_cfg(&dat, &fs_dat));

    if (f_mount(fs_cfg, "0:", 1) != FR_OK) {
        ESP_LOGE(TAG, "failed to mount CONFIG partition");
        return;
    }
    if (f_mount(fs_dat, "1:", 1) != FR_OK) {
        ESP_LOGE(TAG, "failed to mount DATA partition");
        return;
    }

    ensure_dir(SNAC_DATA_DIR);

    usb_console_set_sync_callback(sync_config_cb, NULL);
    usb_console_set_msc_sdmmc(card, cfg_start, cfg_count);
    usb_console_init();

    ESP_LOGI(TAG, "Starting Snac2 task");
    xTaskCreate(snac_task, "snac2", 8192, NULL, 5, NULL);
}

