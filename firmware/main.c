// main.c

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(ble_led_loop, LOG_LEVEL_INF);

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* -------------------- Forward decls for loop functions -------------------- */
static void l4_dump_csv_notify(void);
void run_led_loop1(void);
void run_led_loop2(void);
void run_led_loop3(void);
void run_led_loop4(void);

/* -------------------- Loop 1 GPIOs -------------------- */
#define RST_LED_NODE      DT_ALIAS(rst)
#define RSTCTRL_LED_NODE  DT_ALIAS(rst_ctr)
#define ENBIAS_LED_NODE   DT_ALIAS(enbias)
#define STAB_LED_NODE     DT_ALIAS(stab)
static const struct gpio_dt_spec rst_led     = GPIO_DT_SPEC_GET(RST_LED_NODE, gpios);
static const struct gpio_dt_spec rstctrl_led = GPIO_DT_SPEC_GET(RSTCTRL_LED_NODE, gpios);
static const struct gpio_dt_spec enbias_led  = GPIO_DT_SPEC_GET(ENBIAS_LED_NODE, gpios);
static const struct gpio_dt_spec stab_led    = GPIO_DT_SPEC_GET(STAB_LED_NODE, gpios);

/* -------------------- Loop 2 GPIOs -------------------- */
#define CLK_WRD_NODE DT_ALIAS(clk_wrd)
#define IN_WRD_NODE  DT_ALIAS(in_wrd)
static const struct gpio_dt_spec clk_gpio  = GPIO_DT_SPEC_GET(CLK_WRD_NODE, gpios);
static const struct gpio_dt_spec data_gpio = GPIO_DT_SPEC_GET(IN_WRD_NODE, gpios);

/* 95 bits, MSB-first in the sender; we output 95 pulses. */
static bool data[95] = {
    1,1,1,1,1,
    1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,0,1,1,1,1,1,1,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
    0,1,1,1,1,0,1,1,1,1
};

/* -------------------- Loop 3 GPIOs -------------------- */
#define PULSE_NODE   DT_ALIAS(pulse)
#define RST_TDC_NODE DT_ALIAS(rst_tdc)
static const struct gpio_dt_spec pulse_gpio = GPIO_DT_SPEC_GET(PULSE_NODE, gpios);
static const struct gpio_dt_spec rst_gpio   = GPIO_DT_SPEC_GET(RST_TDC_NODE, gpios);

/* -------------------- Loop 4 GPIOs & state -------------------- */
#define CEL_NODE        DT_ALIAS(cell)
#define CLK_SHIFT_NODE  DT_ALIAS(clk_shift)
static const struct gpio_dt_spec cel_gpio       = GPIO_DT_SPEC_GET(CEL_NODE, gpios);
static const struct gpio_dt_spec clk_shift_gpio = GPIO_DT_SPEC_GET(CLK_SHIFT_NODE, gpios);

#define TX_COUNT  14
#define CYCLES    128

static const struct gpio_dt_spec tx_gpios[TX_COUNT] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(tx0), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx1), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx2), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx3), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx4), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx5), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx6), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx7), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx8), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx9), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx10), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx11), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx12), gpios),
    GPIO_DT_SPEC_GET(DT_ALIAS(tx13), gpios),
};

/* Loop-4 CSV readout state */
#define L4_NUM_PINS     TX_COUNT
#define L4_TOTAL_READS  CYCLES
static uint8_t l4_matrix[L4_TOTAL_READS][L4_NUM_PINS];  // 128 × 14
static volatile bool l4_ready = false;
static uint16_t l4_read_idx = 0;

/* ---- Loop-4 timing (tune if needed) ---- */
#define L4_T_HIGH_US   300   /* clk_shift high time per sample */
#define L4_T_LOW_US    300   /* clk_shift low time per sample  */
#define L4_ARM_US      1000   /* settle after (re)arming CEL    */
#define L4_SAMPLE_US   60 

/* -------------------- BLE globals -------------------- */
static bool notify_enabled = false;       /* used for both FFF2 and FFF4 CCCs */
static uint8_t command_value[20] = {0};   /* DONE_xx messages on FFF2 */
static struct bt_conn *current_conn = NULL;

/* -------------------- BLE callbacks -------------------- */
static void notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
    } else {
        LOG_INF("Connected");
        current_conn = bt_conn_ref(conn);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    LOG_INF("Disconnected (reason %u)", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

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

/* -------------------- FFF3 READ: return next CSV row "b0,b1,...,b13" -------------------- */
static ssize_t l4_read_csv(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset)
{
    char line[64];
    int off = 0;

    if (!l4_ready && (l4_read_idx == 0)) {
        /* Serve zeros until a capture is performed */
        for (int i = 0; i < L4_NUM_PINS; i++) {
            off += snprintk(line + off, sizeof(line) - off,
                            (i == L4_NUM_PINS - 1) ? "0" : "0,");
        }
        return bt_gatt_attr_read(conn, attr, buf, len, offset, line, off);
    }

    /* Clamp to last valid row if client keeps reading */
    uint16_t row = (l4_read_idx < L4_TOTAL_READS) ? l4_read_idx : (L4_TOTAL_READS - 1);

    for (int i = 0; i < L4_NUM_PINS; i++) {
        off += snprintk(line + off, sizeof(line) - off,
                        (i == L4_NUM_PINS - 1) ? "%d" : "%d,", l4_matrix[row][i]);
    }

    if (l4_read_idx < L4_TOTAL_READS) {
        l4_read_idx++;
        if (l4_read_idx >= L4_TOTAL_READS) {
            LOG_INF("Loop4: row-by-row read completed.");
        }
    }

    return bt_gatt_attr_read(conn, attr, buf, len, offset, line, off);
}

/* -------------------- FFF3 WRITE: 'R' capture, 'D' dump (no capture) -------------------- */
static ssize_t l4_write_ctrl(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len > 0) {
        const uint8_t c = ((const uint8_t*)buf)[0];
        if (c == 'R') {
            l4_read_idx = 0;
            l4_ready = false;
            l4_capture_once();
        } else if (c == 'D') {
            if (notify_enabled) {
                l4_dump_csv_notify();
            }
        }
    }
    return len;
}
/* Forward decl for FFF1 write handler */
static ssize_t write_callback(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len,
                              uint16_t offset, uint8_t flags);

/* -------------------- GATT Service --------------------
   Attr indices for reference (do not rely on these externally):
   [0]  PRIMARY SERVICE (0xFFF0)
   [1]  FFF1 decl
   [2]  FFF1 value (WRITE)
   [3]  FFF2 decl
   [4]  FFF2 value (NOTIFY)  <-- DONE_xx notifications
   [5]  FFF2 CCC
   [6]  FFF3 decl
   [7]  FFF3 value (READ|WRITE)  <-- row-by-row CSV read, 'R'/'D' write
   [8]  FFF4 decl
   [9]  FFF4 value (NOTIFY)  <-- bulk CSV stream
   [10] FFF4 CCC
------------------------------------------------------------------ */
BT_GATT_SERVICE_DEFINE(my_service,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_16(0xFFF0)),

    /* FFF1: command write */
    /* FFF1: command write */
BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0xFFF1), BT_GATT_CHRC_WRITE,
                       BT_GATT_PERM_WRITE, NULL, write_callback, NULL),


    /* FFF2: notification value + CCC (used for DONE_xx messages) */
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0xFFF2), BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, command_value),
    BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* FFF3: Loop-4 CSV read|write */
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0x2B29),
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           l4_read_csv, l4_write_ctrl, NULL),

    /* FFF4: Bulk CSV NOTIFY (entire matrix as text) */
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_16(0xFFF4), BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* -------------------- FFF1 command write handler -------------------- */
static ssize_t write_callback(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    char received[20] = {0};
    memcpy(received, buf, MIN(len, sizeof(received)-1));
    LOG_INF("Received CMD: %s", received);

    if (strncmp(received, "01", 2) == 0)      { run_led_loop1();  strcpy((char *)command_value, "DONE_01"); }
    else if (strncmp(received, "02", 2) == 0) { run_led_loop2();  strcpy((char *)command_value, "DONE_02"); }
    else if (strncmp(received, "03", 2) == 0) { run_led_loop3();  strcpy((char *)command_value, "DONE_03"); }
    else if (strncmp(received, "04", 2) == 0) {
        /* "04" -> capture; "04D" -> capture then dump */
        run_led_loop4();
        if (received[2] == 'D' && notify_enabled) {
            l4_dump_csv_notify();
        }
        strcpy((char *)command_value, "DONE_04");
    }

    if (notify_enabled) {
        bt_gatt_notify(NULL, &my_service.attrs[4], command_value, strlen((char *)command_value));
    }
    return len;
}

/* -------------------- New: bulk CSV notify (FFF4) -------------------- */
static void l4_dump_csv_notify(void)
{
    if (!current_conn) {
        LOG_WRN("No connection; cannot dump CSV.");
        return;
    }

    /* Determine max payload per notification (ATT_MTU - 3 for ATT header). */
    uint16_t mtu = bt_gatt_get_mtu(current_conn);
    uint16_t chunk = (mtu > 3) ? (mtu - 3) : 20;   /* safe fallback */
    if (chunk > 244) chunk = 244;                  /* practical upper bound */

    char outbuf[256];
    size_t used = 0;

    /* Stream all rows as CSV lines "b0,...,b13\n" */
    for (uint16_t row = 0; row < L4_TOTAL_READS; row++) {
        char line[64];
        int off = 0;
        for (int i = 0; i < L4_NUM_PINS; i++) {
            off += snprintk(line + off, sizeof(line) - off,
                            (i == L4_NUM_PINS - 1) ? "%d\n" : "%d,", l4_matrix[row][i]);
        }

        /* If this line would overflow the buffer, flush first */
        if (used + off > chunk) {
            (void)bt_gatt_notify(NULL, &my_service.attrs[9], outbuf, used); // FFF4 value attr
            used = 0;
            /* tiny pacing to avoid controller congestion */
            k_msleep(1);
        }

        memcpy(outbuf + used, line, off);
        used += off;
    }

    /* Flush any remainder */
    if (used) {
        (void)bt_gatt_notify(NULL, &my_service.attrs[9], outbuf, used); // FFF4 value attr
    }

    LOG_INF("Loop4: CSV dump complete (%u rows).", L4_TOTAL_READS);
}

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

void main(void)
{
    int err = bt_enable(bt_ready);
    if (err) {
        LOG_ERR("Bluetooth enable failed (err %d)", err);
        return;
    }

    /* Bind write handler to FFF1 now that stack is up */
    /* NOTE: In Zephyr, the write cb is part of the characteristic declaration above,
       so we don't need to register it here separately. */

    LOG_INF("System is ready.");
}
