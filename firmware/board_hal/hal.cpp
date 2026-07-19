#include "hal.h"

#include "board.h"
#include "esp_log.h"

static const char *TAG = "hal";

void hal_init(void)
{
    ESP_LOGI(TAG, "HAL init for board: %s", BOARD_NAME);
}
