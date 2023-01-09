/** @file homebuzzer.cpp
 *
 * Home Buzzer - Sound part
 * ==================================
 *
 */
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


static QueueHandle_t queue;

static const int buzzer_bus_width =
    #if defined(CONFIG_BUZZER_MMC_BUS_WIDTH_4)
    4;
    #else
    1;
    #endif
static const char mount_point[] = "/sdcard";
static const char tag[] = TAG_BUZZER;


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


static bool buzzer_sound(FILE* fp) {
    if (fp == NULL) {
        ESP_LOGE(tag, "Failed to open file for reading");
        return false;
    }
    char buf[BUZZER_BYTES_FRAME];
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

    auto card = buzzer_mount_tf();
    auto f = fopen((const char*)params, "r");
    if (buzzer_sound(f)) {
        fclose(f);
    }
    esp_vfs_fat_sdcard_unmount(mount_point, card);

    vQueueDelete(queue);
}


extern "C" bool buzzer(const char* src) {
    if (!uxQueueSpacesAvailable(queue)) {
        return true;
    }
    xTaskCreatePinnedToCore(buzzer_task, "BUZZER", BUZZER_STACK_SIZE,
                            (void*)src, 12, nullptr, BUZZER_CPUCORE);
    return false;
}


extern "C" void buzzer_init(void) {
    queue = xQueueCreate(1, sizeof(int32_t));
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
    for (int i = 0; i < fields.mfg_data_len; i++) {
        ESP_LOGI(tag, "buzzer_from_adv: data(%d)-%2x",
                 i, fields.mfg_data[i]);
        if (i != 2) {continue;}
        switch (fields.mfg_data[i]) {
        case 1:   result = "1.wav"; break;
        case 2:   result = "2.wav"; break;
        default:  break;
        }
    }
    return result;
}

