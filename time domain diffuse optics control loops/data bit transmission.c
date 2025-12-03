
static bool data[95] = {
    1,1,1,1,1,
    1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,0,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
    0,1,1,1,1,0,1,1,1,1
};



void run_led_loop2(void)
{
    gpio_pin_configure_dt(&clk_gpio,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&data_gpio, GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < 95; i++) {
        int bit = data[94 - i];
        gpio_pin_set_dt(&data_gpio, bit);
        gpio_pin_set_dt(&clk_gpio, 0);
        gpio_pin_set_dt(&clk_gpio, 0);
        gpio_pin_set_dt(&clk_gpio, 1);
    }
    gpio_pin_set_dt(&data_gpio, 0);
    gpio_pin_set_dt(&clk_gpio, 0);

    if (notify_enabled) {
        strcpy((char *)command_value, "DONE_02");
        bt_gatt_notify(NULL, &my_service.attrs[4], command_value, strlen((char *)command_value));
    }
}

