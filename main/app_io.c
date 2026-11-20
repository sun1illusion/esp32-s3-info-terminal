#include "app_io.h"

#include "driver/gpio.h"

const int app_io_pins[APP_IO_COUNT] = {8, 9, 10, 11, 12, 13, 14, 18};

/* 影子状态：网页和屏幕都读它，避免频繁 gpio_get_level */
static int s_level[APP_IO_COUNT];

void AppIO_Init(void)
{
    for (int i = 0; i < APP_IO_COUNT; i++) {
        gpio_reset_pin(app_io_pins[i]);
        gpio_set_direction(app_io_pins[i], GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(app_io_pins[i], 0);
        s_level[i] = 0;
    }
}

int AppIO_Get(int idx)
{
    if (idx < 0 || idx >= APP_IO_COUNT) return -1;
    return s_level[idx];
}

void AppIO_Set(int idx, int level)
{
    if (idx < 0 || idx >= APP_IO_COUNT) return;
    level = level ? 1 : 0;
    s_level[idx] = level;
    gpio_set_level(app_io_pins[idx], level);
}

void AppIO_Toggle(int idx)
{
    if (idx < 0 || idx >= APP_IO_COUNT) return;
    AppIO_Set(idx, !s_level[idx]);
}
