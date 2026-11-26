#include "display.h"
#include "ui_overview.h"

#include "esp_log.h"

static const char *TAG = "batmon5";

void app_main(void)
{
    ESP_ERROR_CHECK(display_init(NULL));
    ui_overview_create();

    /* Sample values from docs/ui/overview.svg until real sensors are connected. */
    ui_overview_set_battery(78, 124.0f, 12.84f);
    ui_overview_set_solar(186.0f, "Bulk");
    ui_overview_set_water(65, 40);

    ESP_LOGI(TAG, "started");
}
