/** @file homebuzzer.cpp
 *
 * Home Buzzer - Sound part
 * ==================================
 *
 */
#include <stdint.h>
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


static bool buzzer_sound(FILE* fp) {
    if (fp == NULL) {
        ESP_LOGE(tag, "Failed to open file for reading");
        return false;
    }
    char buf[BUZZER_BYTES_FRAME];
    i2s_chan_handle_t i2sch_tx = 0;

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

