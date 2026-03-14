#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "lcd1602_RGB_Module.h"

// I2C defines
// This example will use I2C0 on GPIO6 (SDA) and GPIO7 (SCL) running at 400KHz.
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define I2C_PORT i2c0
#define I2C_SDA 6
#define I2C_SCL 7

// UART defines
// By default the stdout UART is `uart0`, so we will use the second one
#define UART_ID uart1
#define BAUD_RATE 115200

// Use pins 4 and 5 for UART1
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define UART_TX_PIN 4
#define UART_RX_PIN 5



int main()
{
    stdio_init_all();

    printf("Pico serial debugger started at 115200 baud\n");

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    // For more examples of I2C use see https://github.com/raspberrypi/pico-examples/tree/master/i2c

    printf("I2C initialized on pins %d (SDA) and %d (SCL)\n", I2C_SDA, I2C_SCL);

    // Scan I2C bus for devices (useful to find ADS1115 at 0x48 by default)
    printf("Scanning I2C bus...\n");
    bool foundAny = false;
    for (int addr = 0x03; addr <= 0x77; addr++) {
        int ret = i2c_write_blocking(I2C_PORT, addr, NULL, 0, false);
        if (ret == PICO_ERROR_GENERIC || ret < 0) continue;
        printf("  found device at 0x%02X\n", addr);
        foundAny = true;
    }
    if (!foundAny) {
        printf("  no devices found on I2C bus\n");
    }

    // Initialize LCD
    LCD1602RGB_init(16, 2);
    printf("LCD initialized\n");

    // Test I2C communication with LCD addresses
    uint8_t test_data = 0x00;
    int ret = i2c_write_blocking(I2C_PORT, 0x3e, &test_data, 1, false);
    if (ret == 1) {
        printf("I2C LCD communication OK\n");
    } else {
        printf("I2C LCD communication failed: %d\n", ret);
    }

    ret = i2c_write_blocking(I2C_PORT, 0x60, &test_data, 1, false);
    if (ret == 1) {
        printf("I2C RGB communication OK\n");
    } else {
        printf("I2C RGB communication failed: %d\n", ret);
    }

    // Set RGB backlight (lower value for better contrast)
    // If text is hard to read, adjust the module's contrast potentiometer (on the board)
    // or reduce backlight brightness here.
    setRGB(30, 30, 30);
    printf("RGB set to dim white for better contrast\n");

    // Clear display
    clear();
    printf("Display cleared\n");

    // Set cursor to home
    setCursor(0, 0);
    send_string("Hello, World!");
    printf("Printed 'Hello, World!' on LCD\n");

    setCursor(0, 1);
    send_string("Pico LCD Test");
    printf("Printed 'Pico LCD Test' on LCD\n");

    // Setup gain settings for ADS1115
    typedef struct {
        uint16_t configBits;
        float rangeV;
        const char *label;
    } ads_gain_t;

    const ads_gain_t gains[] = {
        {0x0000, 6.144f, "±6.144V"},
        {0x0200, 4.096f, "±4.096V"},
        {0x0400, 2.048f, "±2.048V"},
        {0x0600, 1.024f, "±1.024V"},
        {0x0800, 0.512f, "±0.512V"},
        {0x0A00, 0.256f, "±0.256V"},
    };
    // Start with the maximum voltage range (lowest gain) so 3.3V can be read
    size_t gainIndex = 0; // ±6.144V

    printf("Use serial input 'g' to cycle gain (current %s)\n", gains[gainIndex].label);

    // Main loop: scan I2C, read ADS1115, and show values on LCD
    while (1) {
        // Allow gain change via serial
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            if (ch == 'g' || ch == 'G') {
                gainIndex = (gainIndex + 1) % (sizeof(gains) / sizeof(gains[0]));
                printf("Gain set to %s\n", gains[gainIndex].label);
            }
        }

        printf("\n--- I2C scan ---\n");
        int deviceCount = 0;
        bool foundAds = false;
        for (int addr = 0x03; addr <= 0x77; addr++) {
            int ret = i2c_write_blocking(I2C_PORT, addr, NULL, 0, false);
            if (ret == PICO_ERROR_GENERIC || ret < 0) continue;
            printf("  found device at 0x%02X\n", addr);
            deviceCount++;
            if (addr == 0x48) {
                foundAds = true;
            }
        }
        if (deviceCount == 0) {
            printf("  no devices found on I2C bus\n");
        } else {
            printf("  total devices found: %d\n", deviceCount);
        }

        if (foundAds) {
            // Read all four channels AIN0-AIN3 in single-shot mode
            for (int ch = 0; ch < 4; ch++) {
                // Config for single-shot: OS=1 (start conversion), MUX=ch to GND, PGA=gain, 128SPS
                uint8_t config[3];
                config[0] = 0x01; // Config register
                config[1] = 0x83 | ((gains[gainIndex].configBits >> 8) & 0x0F) | ((ch << 4) & 0x70); // OS=1, MUX, PGA MSB
                config[2] = 0x83 | (gains[gainIndex].configBits & 0xFF); // 128SPS, comparator off, PGA LSB

                // Write config to start conversion
                int config_ret = i2c_write_blocking(I2C_PORT, 0x48, config, 3, false);
                if (config_ret != 3) {
                    printf("ADS1115 ch%d config write failed (ret=%d)\n", ch, config_ret);
                    continue;
                }

                // Wait for conversion (at least 1/128 SPS ≈ 8ms, plus margin)
                sleep_ms(10);

                // Read conversion register
                uint8_t reg = 0x00;
                i2c_write_blocking(I2C_PORT, 0x48, &reg, 1, true);
                uint8_t buf[2];
                int ret = i2c_read_blocking(I2C_PORT, 0x48, buf, 2, false);
                if (ret == 2) {
                    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
                    float voltage = raw * gains[gainIndex].rangeV / 32768.0f;
                    printf("AIN%d: raw=%d (%.4fV)\n", ch, raw, voltage);
                } else {
                    printf("ADS1115 ch%d read failed (ret=%d)\n", ch, ret);
                }
            }

            // Display AIN0 on LCD (single-shot)
            uint8_t config[3];
            config[0] = 0x01;
            config[1] = 0x83 | ((gains[gainIndex].configBits >> 8) & 0x0F); // AIN0-GND, OS=1
            config[2] = 0x83 | (gains[gainIndex].configBits & 0xFF);

            i2c_write_blocking(I2C_PORT, 0x48, config, 3, false);
            sleep_ms(10);

            uint8_t reg = 0x00;
            i2c_write_blocking(I2C_PORT, 0x48, &reg, 1, true);
            uint8_t buf[2];
            int ret = i2c_read_blocking(I2C_PORT, 0x48, buf, 2, false);
            if (ret == 2) {
                int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
                float voltage = raw * gains[gainIndex].rangeV / 32768.0f;

                char line1[17];
                char line2[17];
                snprintf(line1, sizeof(line1), "Strom (Gesamt)");
                snprintf(line2, sizeof(line2), "%.4f A", voltage);

                clear();
                setCursor(0, 0);
                send_string(line1);
                setCursor(0, 1);
                send_string(line2);
            } else {
                printf("ADS1115 LCD read failed (ret=%d)\n", ret);
            }
        } else {
            // No ADS1115, show device count on LCD
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "I2C Devices: %d", deviceCount);
            snprintf(line2, sizeof(line2), "No ADS1115");

            clear();
            setCursor(0, 0);
            send_string(line1);
            setCursor(0, 1);
            send_string(line2);
        }

        sleep_ms(2000); // Update every 2 seconds
    }
}


