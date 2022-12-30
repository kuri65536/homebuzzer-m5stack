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


#if defined(__cplusplus)
extern "C" {
#endif

extern void buzzer_init(void);
extern bool buzzer(const char* sound_name);

#if defined(__cplusplus)
}
#endif

