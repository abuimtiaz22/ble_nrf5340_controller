

/* -------------------- Loop-4 capture (CSV mode) -------------------- */
static void l4_capture_once(void)
{
    if (!device_is_ready(cel_gpio.port) || !device_is_ready(clk_shift_gpio.port)) {
        LOG_ERR("Loop4: control GPIOs not ready!");
        return;
    }

    /* Configure inputs with NO pulls (don’t force them low) */
    for (int i = 0; i < TX_COUNT; i++) {
        if (!device_is_ready(tx_gpios[i].port)) {
            LOG_ERR("Loop4: TX GPIO %d not ready!", i);
            return;
        }
        (void)gpio_pin_configure_dt(&tx_gpios[i], GPIO_INPUT);
    }

    (void)gpio_pin_configure_dt(&cel_gpio,       GPIO_OUTPUT_INACTIVE);
    (void)gpio_pin_configure_dt(&clk_shift_gpio, GPIO_OUTPUT_INACTIVE);

    /* Start capture window and keep it HIGH for the whole frame */
    gpio_pin_set_dt(&cel_gpio, 1);
    k_busy_wait(L4_ARM_US);

    for (int cycle = 0; cycle < L4_TOTAL_READS; cycle++) {
        /* Rising edge requests/advances the next word */
        gpio_pin_set_dt(&clk_shift_gpio, 1);
        k_busy_wait(L4_SAMPLE_US);

        /* Sample during the stable portion of HIGH */
        for (int i = 0; i < L4_NUM_PINS; i++) {
            l4_matrix[cycle][i] = gpio_pin_get_dt(&tx_gpios[i]) ? 1 : 0;
        }

        /* Finish the pulse */
            k_busy_wait(L4_T_HIGH_US - L4_SAMPLE_US);
        gpio_pin_set_dt(&clk_shift_gpio, 0);
        k_busy_wait(L4_T_LOW_US);
    }

    /* End capture window (only after all 128 pulses) */
    gpio_pin_set_dt(&cel_gpio, 0);

    /* Optional: pulse rstctrl if its device is ready (harmless if alias not present) */
    if (device_is_ready(rstctrl_led.port)) {
        (void)gpio_pin_configure_dt(&rstctrl_led, GPIO_OUTPUT_INACTIVE);
        gpio_pin_set_dt(&rstctrl_led, 0);
        gpio_pin_set_dt(&rstctrl_led, 1);
    }
int ones = 0, first_nonzero = -1, last_nonzero = -1;
for (int r = 0; r < L4_TOTAL_READS; r++) {
    uint16_t acc = 0;
    for (int i = 0; i < L4_NUM_PINS; i++) acc |= l4_matrix[r][i];
    if (acc) {
        ones++;
        if (first_nonzero < 0) first_nonzero = r;
        last_nonzero = r;
    }
}
LOG_INF("L4 diag: rows with any '1' = %d/%d (first=%d last=%d)",
        ones, L4_TOTAL_READS, first_nonzero, last_nonzero);

    l4_read_idx = 0;
    l4_ready = true;
    LOG_INF("Loop4: 128 pulses with continuous CEL=HIGH complete.");
}

/

void run_led_loop4(void)
{
    /* Perform capture; DONE_04 is sent by the command handler */
    l4_capture_once();
}

/* -------------------- Main -------------------- */
static void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    bt_le_adv_start(BT_LE_ADV_CONN_NAME, NULL, 0, NULL, 0);
    LOG_INF("Bluetooth advertising started.");
}

