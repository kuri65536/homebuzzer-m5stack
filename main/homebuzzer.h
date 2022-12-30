/** @file homebuzzer.h
 *
 * Home Buzzer - application definitions
 * ==========================================
 *
 */
#pragma once

#define BUZZER_GAP_NAME "buzzer-dev1"
#define TAG_BUZZER "BUZZER"

#define BUZZER_STACK_SIZE 4096
#define BUZZER_CPUCORE 1        /// 0 or 1 for Core-ID

#define BUZZER_BYTES_FRAME 2048
#define BUZZER_MSEC_FRAME  200


#if CONFIG_IDF_TARGET_ESP32
    #define BUZZER_I2S_BCLK_IO1 GPIO_NUM_4   /// I2S clock
    #define BUZZER_I2S_WS_IO1   GPIO_NUM_5   /// I2S select
    #define BUZZER_I2S_DOUT_IO1 GPIO_NUM_18  /// I2S output
    #define BUZZER_I2S_DIN_IO1  GPIO_NUM_19  /// I2S input
#else
    #define BUZZER_I2S_BCLK_IO1 GPIO_NUM_2   /// I2S clock
    #define BUZZER_I2S_WS_IO1   GPIO_NUM_3   /// I2S select
    #define BUZZER_I2S_DOUT_IO1 GPIO_NUM_4   /// I2S output
    #define BUZZER_I2S_DIN_IO1  GPIO_NUM_5   /// I2S input
#endif


#if defined(__cplusplus)
extern "C" {
#endif

extern void buzzer_init(void);
extern bool buzzer(const char* sound_name);

#if defined(__cplusplus)
}
#endif

