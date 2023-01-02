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
#include "sdmmc_cmd.h"


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
                  sdmmc_slot_config_t> buzzer_tf_init() {
    esp_vfs_fat_sdmmc_mount_config_t ret1 = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
    };

    sdmmc_slot_config_t ret2 = SDMMC_SLOT_CONFIG_DEFAULT();
    ret2.width = buzzer_bus_width;
    #if defined(CONFIG_SOC_SDMMC_USE_GPIO_MATRIX)
    ret2.clk = CONFIG_BUZZER_MMC_CLK;
    ret2.cmd = CONFIG_BUZZER_MMC_CMD;
    ret2.d0 = CONFIG_BUZZER_MMC_D0;
    #if defined(CONFIG_BUZZER_MMC_BUS_WIDTH_4)
    ret2.d1 = CONFIG_BUZZER_MMC_D1;
    ret2.d2 = CONFIG_BUZZER_MMC_D2;
    ret2.d3 = CONFIG_BUZZER_MMC_D3;
    #endif
    #endif
    ret2.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    return {ret1, ret2};
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


extern "C" void buzzer_task(void* params) {
    xQueueSend(queue, (void*)0, (TickType_t)0);

    sdmmc_card_t *card;
    auto [mount_config, slot_config] = buzzer_tf_init();
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    auto ret = esp_vfs_fat_sdmmc_mount(
            mount_point, &host, &slot_config, &mount_config, &card);
    if (ret == ESP_FAIL) {
        ESP_LOGE(tag, "mmc-mount: Failed (not formatted)");
    } else if (ret != ESP_OK) {
        ESP_LOGE(tag, "mmc-mount: Failed (formatting) %s.",
                 esp_err_to_name(ret));
    }
    sdmmc_card_print_info(stdout, card);

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

