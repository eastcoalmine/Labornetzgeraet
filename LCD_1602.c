#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <hardware/adc.h>
#include "lcd1602_RGB_Module.h"

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 6
#define I2C_SCL 7

// Taster-Pin definieren
#define CALIBRATION_BUTTON 2

// FESTE GAIN-EINSTELLUNG: ±1.024V
#define ADS_GAIN_CONFIG 0x0600
#define ADS_RANGE_V 1.024f

// GLEITENDER MITTELWERT
#define AVG_BUFFER_SIZE 10

// LINEARE REGRESSION PARAMETER (Spannungskorrektur)
#define REGRESSION_SLOPE 1.97f
#define REGRESSION_INTERCEPT 31.42f

// MA UMRECHNUNGSPARAMETER (mV zu mA - NEU KALIBRIERT)
#define MA_SLOPE 1.1165f
#define MA_INTERCEPT -36.74f

// Update-Rate für Display (in ms)
#define DISPLAY_UPDATE_MS 200

// Temperatur-Mittelwert (für stabilere Anzeige)
#define TEMP_AVG_SIZE 5

// Manueller Offset (wird bei Kalibrierung aktualisiert)
float manual_offset_mV = 0.0f;

// Ringpuffer für gleitenden Mittelwert (Strom)
float avg_buffer[AVG_BUFFER_SIZE];
int avg_index = 0;
int avg_count = 0;

// Ringpuffer für Temperatur
float temp_buffer[TEMP_AVG_SIZE];
int temp_idx = 0;
int temp_count = 0;

// ADS1115 gefunden-Status (wird einmal beim Start gesetzt)
static bool ads1115_found = false;

// Hilfsfunktion: ADS1115 konfigurieren und messen
int read_ads1115_ain0(int16_t *raw_value) {
    uint8_t config[3];
    config[0] = 0x01;
    config[1] = 0x83 | ((ADS_GAIN_CONFIG >> 8) & 0x0F);
    config[2] = 0x83 | (ADS_GAIN_CONFIG & 0xFF);

    int config_ret = i2c_write_blocking(I2C_PORT, 0x48, config, 3, false);
    if (config_ret != 3) {
        return -1;
    }

    sleep_ms(10);

    uint8_t reg = 0x00;
    i2c_write_blocking(I2C_PORT, 0x48, &reg, 1, true);
    uint8_t buf[2];
    int ret = i2c_read_blocking(I2C_PORT, 0x48, buf, 2, false);
    
    if (ret == 2) {
        *raw_value = (int16_t)((buf[0] << 8) | buf[1]);
        return 0;
    } else {
        return -1;
    }
}

// Hilfsfunktion: Rohspannung in mV berechnen
static float calculate_raw_voltage_mV(int16_t raw) {
    return (raw * ADS_RANGE_V / 32768.0f) * 1000.0f;
}

// Hilfsfunktion: Lineare Regression anwenden (Spannungskorrektur)
static float apply_linear_regression(float measured_mV) {
    return (measured_mV * REGRESSION_SLOPE) + REGRESSION_INTERCEPT;
}

// Hilfsfunktion: Korrigierte Spannung berechnen
static float calculate_corrected_voltage_mV(int16_t raw) {
    float raw_mV = calculate_raw_voltage_mV(raw);
    float offset_corrected = raw_mV - manual_offset_mV;
    return apply_linear_regression(offset_corrected);
}

// Hilfsfunktion: mV in mA umrechnen (mit Begrenzung bei 0)
static float convert_mV_to_mA(float voltage_mV) {
    float result = (voltage_mV * MA_SLOPE) + MA_INTERCEPT;
    return (result < 0.0f) ? 0.0f : result;
}

// Hilfsfunktion: Pico-Temperatur lesen (interner Sensor)
float read_pico_temperature(void) {
    // ADC für Temperatursensor aktivieren
    adc_select_input(4);
    uint16_t raw = adc_read();
    
    // Spannung berechnen (3.3V Referenz)
    float voltage = raw * 3.3f / 4095.0f;
    
    // Temperatur berechnen (Formel von Raspberry Pi)
    // 27°C bei 0.706V, -1.72mV/°C
    float temperature = 27.0f - (voltage - 0.706f) / 0.00172f;
    
    return temperature;
}

// Hilfsfunktion: Temperatur-Mittelwert aktualisieren
static float update_temp_average(float new_temp) {
    temp_buffer[temp_idx] = new_temp;
    temp_idx = (temp_idx + 1) % TEMP_AVG_SIZE;
    
    if (temp_count < TEMP_AVG_SIZE) {
        temp_count++;
    }
    
    float sum = 0.0f;
    for (int i = 0; i < temp_count; i++) {
        sum += temp_buffer[i];
    }
    
    return sum / temp_count;
}

// Hilfsfunktion: Strom-Mittelwert aktualisieren
static float update_current_average(float new_current) {
    avg_buffer[avg_index] = new_current;
    avg_index = (avg_index + 1) % AVG_BUFFER_SIZE;
    
    if (avg_count < AVG_BUFFER_SIZE) {
        avg_count++;
    }
    
    float sum = 0.0f;
    for (int i = 0; i < avg_count; i++) {
        sum += avg_buffer[i];
    }
    
    return sum / avg_count;
}

// Hilfsfunktion: Puffer zurücksetzen
static void reset_buffers(void) {
    for (int i = 0; i < AVG_BUFFER_SIZE; i++) {
        avg_buffer[i] = 0.0f;
    }
    avg_index = 0;
    avg_count = 0;
    
    for (int i = 0; i < TEMP_AVG_SIZE; i++) {
        temp_buffer[i] = 0.0f;
    }
    temp_idx = 0;
    temp_count = 0;
}

// Hilfsfunktion: Taster entprellen
static bool button_pressed_with_debounce(void) {
    static uint32_t last_press_time = 0;
    
    bool current_state = gpio_get(CALIBRATION_BUTTON);
    uint32_t now = to_ms_since_boot(get_absolute_time());
    
    if (current_state == 0 && (now - last_press_time) > 50) {
        last_press_time = now;
        return true;
    }
    
    return false;
}

// Hilfsfunktion: Kalibrierung durchführen
void calibrate_zero_point(void) {
    int16_t raw;
    if (read_ads1115_ain0(&raw) == 0) {
        float measured_raw_mV = calculate_raw_voltage_mV(raw);
        manual_offset_mV = measured_raw_mV;
        
        reset_buffers();
        
        clear();
        setCursor(0, 0);
        send_string("Offset gespeichert");
        
        char line2[17];
        snprintf(line2, sizeof(line2), "Offset: %.2f mV", manual_offset_mV);
        setCursor(0, 1);
        send_string(line2);
        
        sleep_ms(1500);
    } else {
        clear();
        setCursor(0, 0);
        send_string("Kalibrierung");
        setCursor(0, 1);
        send_string("FEHLGESCHLAGEN");
        sleep_ms(1500);
    }
}

// Hilfsfunktion: I2C-Scan (nur einmal beim Start)
void scan_i2c_bus(void) {
    printf("Scanning I2C bus...\n");
    bool foundAny = false;
    for (int addr = 0x03; addr <= 0x77; addr++) {
        int ret = i2c_write_blocking(I2C_PORT, addr, NULL, 0, false);
        if (ret == PICO_ERROR_GENERIC || ret < 0) continue;
        printf("  found device at 0x%02X\n", addr);
        foundAny = true;
        if (addr == 0x48) {
            ads1115_found = true;
        }
    }
    if (!foundAny) {
        printf("  no devices found on I2C bus\n");
    }
}

int main()
{
    stdio_init_all();

    printf("Pico serial debugger started at 115200 baud\n");

    // ADC initialisieren (für Temperatursensor)
    adc_init();
    adc_set_temp_sensor_enabled(true);
    printf("ADC initialized with temperature sensor enabled\n");

    // I2C initialisieren
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    printf("I2C initialized on pins %d (SDA) und %d (SCL)\n", I2C_SDA, I2C_SCL);

    // I2C-Scan (nur einmal!)
    scan_i2c_bus();

    // LCD initialisieren
    LCD1602RGB_init(16, 2);
    printf("LCD initialized\n");

    // I2C-Kommunikation testen
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

    setRGB(30, 30, 30);
    printf("RGB set to dim white for better contrast\n");

    // Initialanzeige
    clear();
    setCursor(0, 0);
    send_string("System startet...");
    setCursor(0, 1);
    send_string(ads1115_found ? "ADS1115 OK" : "Kein ADS1115");
    sleep_ms(1500);

    // Taster initialisieren
    gpio_init(CALIBRATION_BUTTON);
    gpio_set_dir(CALIBRATION_BUTTON, GPIO_IN);
    gpio_pull_up(CALIBRATION_BUTTON);
    printf("Kalibrierungstaster initialisiert (GPIO %d)\n", CALIBRATION_BUTTON);

    // Kalibrierung beim Start durchführen
    sleep_ms(500);
    if (ads1115_found) {
        printf("Starte initiale Kalibrierung...\n");
        calibrate_zero_point();
    }

    printf("Starte Messung...\n");

    // Hauptloop
    while (1) {
        // Temperatur messen
        float raw_temp = read_pico_temperature();
        float smoothed_temp = update_temp_average(raw_temp);
        
        int16_t raw;
        
        if (ads1115_found && read_ads1115_ain0(&raw) == 0) {
            // Strom-Messung durchführen
            float raw_voltage_mV = calculate_raw_voltage_mV(raw);
            float corrected_voltage_mV = calculate_corrected_voltage_mV(raw);
            float smoothed_voltage_mV = update_current_average(corrected_voltage_mV);
            float current_mA = convert_mV_to_mA(smoothed_voltage_mV);
            
            // Kalibrierung prüfen
            if (button_pressed_with_debounce()) {
                calibrate_zero_point();
            }
            
            // Display aktualisieren (Temperatur oben, Strom unten)
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Temp: %.1f C", smoothed_temp);
            snprintf(line2, sizeof(line2), "Strom: %.2f mA", current_mA);

            clear();
            setCursor(0, 0);
            send_string(line1);
            setCursor(0, 1);
            send_string(line2);
        } else {
            // Fehleranzeige
            char line1[17];
            char line2[17];
            snprintf(line1, sizeof(line1), "Temp: %.1f C", smoothed_temp);
            snprintf(line2, sizeof(line2), "Kein ADS1115");

            clear();
            setCursor(0, 0);
            send_string(line1);
            setCursor(0, 1);
            send_string(line2);
        }

        sleep_ms(DISPLAY_UPDATE_MS);
    }
}