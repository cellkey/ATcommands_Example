#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
