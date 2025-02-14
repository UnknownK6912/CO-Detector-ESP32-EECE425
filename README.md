# CO-Detector-ESP32-EECE425
This is a group project for the "EECE 425: Embedded and IoT Systems" Course at the American University of Beirut (AUB)

Carbon Monoxide detector using the ESP32 (ESP32 Devkit-V1), programmed using PlatformIO (ESP_IDF)

### Components

- **ESP32 Devkit-V1**: Acts as the central microcontroller.
- **MQ-7 Carbon Monoxide Sensor**: Detects CO levels and outputs an analog signal.
- **Push Button**: Allows the user to reset alerts and wake the ESP32 from deep sleep.
- **Red LED**: Indicates high CO levels (>100 ppm, danger).
- **Yellow LED**: Indicates moderate CO levels (51–100 ppm, caution).
- **Green LED**: Indicates safe CO levels (≤50 ppm).
- **Resistors**: Used to limit current for the LEDs.
- **(Optional) MOSFET and/or external circuitry**: Allows the proper heating cycle of the MQ-7 sensor (90s at 5V, 30s at 1.4V).

---

## Functionality

### 1. **Hardware Initialization**

The `init_hw()` function initializes:
- The LED pins as outputs.
- The button as an input with an interrupt.
- The ADC for reading the MQ-7 sensor data.

```c
void init_hw() {
    gpio_pad_select_gpio(RED_LED);
    gpio_set_direction(RED_LED, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(YELLOW_LED);
    gpio_set_direction(YELLOW_LED, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(GREEN_LED);
    gpio_set_direction(GREEN_LED, GPIO_MODE_OUTPUT);
    gpio_pad_select_gpio(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_pulldown_en(BUTTON_GPIO);
    gpio_pullup_dis(BUTTON_GPIO);
    gpio_set_intr_type(BUTTON_GPIO, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL);
    configure_adc();
}
```

### 2. **ADC Configuration & CO Calculation**

The `configure_adc()` function sets up the ADC to read the MQ-7 sensor values, and `mq7_read_update()` converts ADC values to CO concentration in PPM.
It should be noted that ADC functions used in this code are now deprecated (but they still work).
It is recommended to use the updated libraries.

```c
static void configure_adc(void) {
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
}

float mq7_read_update(uint32_t adc_value) {
    float voltage = (adc_value * VC) / 4095.0;
    float RS = ((VC / voltage) - 1.0) * RL;
    float ppm = 100.0 * pow(RS / RO, -1.4);
    return ppm;
}
```

### 3. **LED Status Update**

The `update_led()` function updates the LED indicators based on the CO levels.

```c
void update_led(float ppm) {
    if (ppm > 100) {
        gpio_set_level(RED_LED, 1);
        gpio_set_level(YELLOW_LED, 0);
        gpio_set_level(GREEN_LED, 0);
    } else if (ppm > 50) {
        gpio_set_level(RED_LED, 0);
        gpio_set_level(YELLOW_LED, 1);
        gpio_set_level(GREEN_LED, 0);
    } else {
        gpio_set_level(RED_LED, 0);
        gpio_set_level(YELLOW_LED, 0);
        gpio_set_level(GREEN_LED, 1);
    }
}
```

### 4. **Button Interrupt & Deep Sleep Mode**

The system enters deep sleep mode when the button is pressed, disabling updates for 30 minutes.

```c
void IRAM_ATTR button_isr_handler(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (now > next) {
        next = now + 500/portTICK_PERIOD_MS;
        gpio_set_level(RED_LED, 0);
        gpio_set_level(YELLOW_LED, 0);
        gpio_set_level(GREEN_LED, 0);
        enter_reset_mode();
    }
}
```

### 5. **Deep Sleep Function**

The `enter_reset_mode()` function puts the ESP32 into deep sleep to reduce power consumption. The system will wake up either when the button is pressed or after the predefined duration (30 minutes).

```c
void enter_reset_mode() {
    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);
    esp_sleep_enable_timer_wakeup(DISABLE_DURATION_MS * 1000);
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
    printf("Entering deep sleep for %d seconds or until button press.\n", DISABLE_DURATION_MS);
    esp_deep_sleep_start();
}
```

### 6. **Wi-Fi Connection**

The `connect_wifi()` function initializes Wi-Fi, allowing the ESP32 to serve a webpage with CO level updates.

```c
void connect_wifi(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
```

### 7. **Web Server & Data Updates**

The ESP32 runs a web server, displaying live CO levels.

```c
esp_err_t ppm_data_handler(httpd_req_t *req) {
    char response[50];
    snprintf(response, sizeof(response), "{\"ppm\":%.2f}", current_ppm);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}
```


