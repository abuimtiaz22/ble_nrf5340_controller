

/* -------------------- Loop implementations -------------------- */
void run_led_loop1(void)
{
    gpio_pin_configure_dt(&rst_led,     GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&rstctrl_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&enbias_led,  GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&stab_led,    GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&stab_led, 1);

    gpio_pin_set_dt(&rst_led, 0);
    gpio_pin_set_dt(&rstctrl_led, 0);
    k_msleep(8);

    gpio_pin_set_dt(&rst_led, 1);
    gpio_pin_set_dt(&rstctrl_led, 1);

    gpio_pin_set_dt(&enbias_led, 1);

    gpio_pin_set_dt(&stab_led, 0);
    gpio_pin_set_dt(&stab_led, 0);
    gpio_pin_set_dt(&stab_led, 0);

    gpio_pin_set_dt(&stab_led, 1);

    if (notify_enabled) {
        strcpy((char *)command_value, "DONE_01");
        bt_gatt_notify(NULL, &my_service.attrs[4], command_value, strlen((char *)command_value));
    }
}

