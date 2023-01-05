/** @file homebuzzer.cpp
 *
 * Home Buzzer - Sound part
 * ==================================
 *
 */
#include <dirent.h>
#include <stdint.h>
#include <cstring>
#include <tuple>

#include "driver/i2s_std.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
// #include "host/util/util.h"
#include "sdmmc_cmd.h"
// #include "services/gap/ble_svc_gap.h"


#include "blecent.h"
#include "homebuzzer.h"


#define BUZZER_TASKTAG "BUZZER"

static QueueHandle_t queue;
static TaskHandle_t task_handle;

static const int buzzer_bus_width =
    #if defined(CONFIG_BUZZER_MMC_BUS_WIDTH_4)
    4;
    #else
    1;
    #endif
static const char mount_point[] = "/sdcard";
static const char tag[] = TAG_BUZZER;
static char* sounds[10] = {
    (char*)nullptr, (char*)nullptr, (char*)nullptr, (char*)nullptr,
    (char*)nullptr, (char*)nullptr, (char*)nullptr, (char*)nullptr,
    (char*)nullptr, (char*)nullptr,
};


static std::tuple<esp_vfs_fat_sdmmc_mount_config_t,
                  int, sdmmc_host_t,
                  sdspi_device_config_t> buzzer_tf_init() {
    esp_vfs_fat_sdmmc_mount_config_t ret1 = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    static bool f_init = true;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = CONFIG_BUZZER_MMC_MOSI,
        .miso_io_num = CONFIG_BUZZER_MMC_MISO,
        .sclk_io_num = CONFIG_BUZZER_MMC_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    auto hostid = (spi_host_device_t)host.slot;
    int rc;
    if (f_init) {
        rc = spi_bus_initialize(hostid, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (rc != ESP_OK) {
            ESP_LOGE(tag, "Failed to initialize bus.");
        }
        f_init = false;
    } else {
        rc = ESP_OK;
    }

    sdspi_device_config_t ret2 = SDSPI_DEVICE_CONFIG_DEFAULT();
    ret2.gpio_cs = (gpio_num_t)CONFIG_BUZZER_MMC_CS;
    ret2.host_id = hostid;

    return {ret1, rc, host, ret2};
}


static i2s_chan_handle_t buzzer_sound_init(void) {
    i2s_chan_handle_t hnd;
    i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(
            I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&cfg, &hnd, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(8000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BUZZER_I2S_BCLK_IO1,
            .ws   = BUZZER_I2S_WS_IO1,
            .dout = BUZZER_I2S_DOUT_IO1,
            .din  = BUZZER_I2S_DIN_IO1,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(hnd, &std_cfg));
    return hnd;
}


static std::tuple<int, bool, int> buzzer_sound_read_format(FILE* fp) {
    uint8_t tmp[8];
    fread(tmp, 1, 4, fp);  // - `RIFF`
    fread(tmp, 1, 4, fp);  // - file size
    fread(tmp, 1, 4, fp);  // - `WAVE`
    fread(tmp, 1, 4, fp);  // - `fmt\0`
    fread(tmp, 1, 4, fp);  // - 16 for header length.
    fread(tmp, 1, 2, fp);  // - 1:PCM
    fread(tmp, 1, 2, fp);  // - channel
    auto channels = *(uint16_t*)tmp;
    fread(tmp, 1, 4, fp);  // - sample rate (sample/sec)
    auto rate = *(uint32_t*)tmp;
    fread(tmp, 1, 4, fp);  // - sample rate (bytes/sec)
    fread(tmp, 1, 2, fp);  // - block alignment
    fread(tmp, 1, 2, fp);  // - bits per sample
    auto sample = *(uint16_t*)tmp;
    fread(tmp, 1, 4, fp);  // - `data` : beginning of data section.
    fread(tmp, 1, 4, fp);  // - size of data section

    // auto tick = (1000000 + (rate >> 1)) / rate;
    auto tick = 1000000 / rate;
    #if 0
    auto tick = rate == 44100 ? 23:  /// - 44100Hz -> 22.6[usec]
                rate == 48000 ? 21:  /// - 48000Hz -> 20.8[usec]
    #endif
    auto streao = channels != 1;
    return {sample, streao, tick};
}


static bool buzzer_sound(FILE* fp) {
    if (fp == NULL) {
        ESP_LOGE(tag, "Failed to open file for reading");
        return false;
    }
    int8_t buf[BUZZER_BYTES_FRAME];
    auto [bits, streao, tick] = buzzer_sound_read_format(fp);
    i2s_chan_handle_t i2sch_tx = buzzer_sound_init();

    while (true) {
        size_t n_write = 0;
        vTaskDelay(pdMS_TO_TICKS(BUZZER_MSEC_FRAME));
        auto n_read = fread(buf, sizeof(uint8_t), BUZZER_BYTES_FRAME, fp);
        if (n_read < 1) {break;}
        auto ret = i2s_channel_write(i2sch_tx, buf, n_read, &n_write, 1000);
        if (ret != ESP_OK) {
            ESP_LOGE(tag, "i2s-write: failed");
        }
        if (n_read < BUZZER_BYTES_FRAME) {break;}
    }
    return true;
}


static sdmmc_card_t* buzzer_mount_tf() {
    sdmmc_card_t *card;
    auto [mount_config, rc, host, slot_config] = buzzer_tf_init();
    if (rc != ESP_OK) {
        return nullptr;
    }

    auto ret = esp_vfs_fat_sdspi_mount(
            mount_point, &host, &slot_config, &mount_config, &card);
    if (ret == ESP_FAIL) {
        ESP_LOGE(tag, "mmc-mount: Failed (not formatted)");
        return nullptr;
    } else if (ret != ESP_OK) {
        ESP_LOGE(tag, "mmc-mount: Failed (formatting) %s.",
                 esp_err_to_name(ret));
        return nullptr;
    }
    sdmmc_card_print_info(stdout, card);
    return card;
}


extern "C" void buzzer_task(void* params) {
    static int32_t src = 1;
    xQueueSend(queue, (void*)&src, (TickType_t)0);

    auto card = buzzer_mount_tf();
    auto f = fopen((const char*)params, "r");
    if (buzzer_sound(f)) {
        fclose(f);
    }
    esp_vfs_fat_sdcard_unmount(mount_point, card);

    xQueueReset(queue);

    for (;;) {
        vTaskDelete(task_handle);
    }
}


extern "C" bool buzzer(const char* src) {
    int tmp = 0;
    if (xQueuePeek(queue, (void*)&tmp, (TickType_t)0)) {
        ESP_LOGE(tag, "buzzer: duplicated playing ignored...");
        return true;
    }
    xTaskCreatePinnedToCore(buzzer_task, BUZZER_TASKTAG, BUZZER_STACK_SIZE,
                            (void*)src, 12, &task_handle, BUZZER_CPUCORE);
    return false;
}


extern "C" void buzzer_init_task(void* params) {
    auto hnd_task = *(TaskHandle_t*)params;

    auto strendswith = [] (const char* s1, const char* s2) {
        auto len1 = strlen(s1);
        auto len2 = strlen(s2);
        return len1 >= len2 && !memcmp(&s1[len1] - len2, s2, len2);
    };

    auto check_fname = [strendswith] (const char* src) {
        auto ret = src[0] - '0';
        if (ret >= 0 && ret <= 9) {return ret;}
        if (!strendswith(src, ".WAV")) {return -1;}
        for (int i = 0; i < ARRAY_SIZE(sounds); i++) {
            if (sounds[i] == nullptr) {
                return i;
            }
        }
        return -1;
    };

    auto card = buzzer_mount_tf();

    auto d = opendir(mount_point);
    while (auto ent = readdir(d)) {
        auto fname = ent->d_name;
        ESP_LOGI(tag, "buzzer_init_task: %s", fname);
        auto n = check_fname(fname);
        if (n < 0) {continue;}

        auto len = strlen(fname);
        sounds[n] = (char*)malloc(len + 1);
        memcpy((void*)sounds[n], fname, len + 1);
        ESP_LOGI(tag, "buzzer_init_task: stored to %d", n);
    }
    closedir(d);

    esp_vfs_fat_sdcard_unmount(mount_point, card);

    for (;;) {
        vTaskDelete(hnd_task);
    }
}


extern "C" void buzzer_init(void) {
    queue = xQueueCreate(1, sizeof(int32_t));
    ESP_LOGI(tag, "buzzer_init: queue: %x", (int)queue);

    xTaskCreatePinnedToCore(buzzer_init_task, BUZZER_TASKTAG, BUZZER_STACK_SIZE,
                            &task_handle, 12, &task_handle, BUZZER_CPUCORE);
}


extern "C" bool buzzer_check_addr(const uint8_t* src, int len) {
    static uint8_t peer_addr[6] = {0};

    if (const_strcmp(CONFIG_BUZZER_PEER_ADDR, "ADDR_ANY") == 0) {
        ESP_LOGI(tag, "buzzer_chk_addr: any: %x:%x:%x:%x:%x:%x",
                 src[5], src[4], src[3],
                 src[2], src[1], src[0]);
        return false;
    }

    ESP_LOGI(tag, "Peer address from menuconfig: %s",
             CONFIG_BUZZER_PEER_ADDR);
    /* Convert string to address */
    if (peer_addr[0] == 0) {
        sscanf(CONFIG_BUZZER_PEER_ADDR, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &peer_addr[5], &peer_addr[4], &peer_addr[3],
           &peer_addr[2], &peer_addr[1], &peer_addr[0]);
    }
    return memcmp(peer_addr, src, len);
}


bool buzzer_check_service(const struct ble_hs_adv_fields* fields) {
    for (int i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == BLECENT_SVC_ALERT_UUID) {
            ESP_LOGI(tag, "buzzer_chk_serv: found %d/%d",
                     i, fields->num_uuids16);
            return false;
        }
    }
    return true;
}


static bool buzzer_check_history(uint16_t n_new) {
    static int history_adv_n = 0;
    static uint16_t history_adv[5] = {0, 0, 0, 0, 0};

    const int N = ARRAY_SIZE(history_adv);
    for (int i = 0; i < N; i++) {
        if (history_adv[i] == n_new) {
            ESP_LOGI(tag, "buzzer_chk_hist: found at %d(%d)", n_new, i);
            return true;
        }
    }
    auto j = history_adv_n++;
    history_adv_n = history_adv_n >= N ? 0: history_adv_n;
    history_adv[j] = n_new;
    ESP_LOGI(tag, "buzzer_chk_hist: update to %d(%d)", n_new, j);
    return false;
}


extern "C" const char* buzzer_from_advertise(
        const struct ble_gap_disc_desc* disc
        // const struct ble_hs_adv_fields* fields
) {
    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        ESP_LOGE(tag, "buzzer_from_adv: invalid type: %d", disc->event_type);
        return nullptr;
    }
    if (buzzer_check_addr(disc->addr.val, sizeof(disc->addr.val))) {
        return nullptr;
    }

    struct ble_hs_adv_fields fields;
    auto rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        ESP_LOGE(tag, "buzzer_from_adv: can't parse fields: %d", rc);
        return nullptr;
    }

    if (buzzer_check_service(&fields)) {
        ESP_LOGI(tag, "buzzer_from_adv: dont have service.");
        return nullptr;
    }
    const char* result = nullptr;
    int num = 0;
    for (int i = 0; i < fields.mfg_data_len; i++) {
        ESP_LOGI(tag, "buzzer_from_adv: data(%d)-%2x",
                 i, fields.mfg_data[i]);
        auto n = fields.mfg_data[i];
        if (i == 3 || i == 4) {
            num += n << (8 * (i - 3));
            continue;
        }
        if (i != 2) {continue;}
        if (n < ARRAY_SIZE(sounds)) {
            result = sounds[n];
        }
    }
    if (result && buzzer_check_history(num)) {
        return nullptr;
    }
    return result;
}

