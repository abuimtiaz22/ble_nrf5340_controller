

void run_led_loop3(void)
{
    gpio_pin_configure_dt(&pulse_gpio, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&rst_gpio,   GPIO_OUTPUT_INACTIVE);

    for (int i = 0; i < 1000000; i++) {
        gpio_pin_set_dt(&pulse_gpio, 1);
        gpio_pin_set_dt(&rst_gpio,   0);
        gpio_pin_set_dt(&pulse_gpio, 0);
        gpio_pin_set_dt(&rst_gpio,   1);
    }

    if (notify_enabled) {
        strcpy((char *)command_value, "DONE_03");
        bt_gatt_notify(NULL, &my_service.attrs[4], command_value, strlen((char *)command_value));
    }
}

