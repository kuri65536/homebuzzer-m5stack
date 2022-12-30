/** @file homebuzzer.cpp
 *
 * Home Buzzer - Sound part
 * ==================================
 *
 */
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"


#include "homebuzzer.h"


static QueueHandle_t queue;


extern "C" void buzzer_task(void* params) {
    xQueueSend(queue, (void*)0, (TickType_t)0);

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

