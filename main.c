#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_sleep.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"


#define RED_LED GPIO_NUM
#define YELLOW_LED GPIO_NUM
#define GREEN_LED GPIO_NUM
#define BUTTON_GPIO GIO_NUM
#define SENSOR_GPIO GPIO_NUM
#define DISABLE_DURATION_MS 1800000 // 30 minutes

#define ADC_CHANNEL ADC1_CHANNEL_0 // GPIO 36
#define ADC_WIDTH ADC_WIDTH_BIT_12 // 12-bit ADC
#define ADC_ATTEN ADC_ATTEN_DB_11  // Attenuation for 0-3.3V range
#define DEFAULT_VREF 1100          // Default reference voltage in mV

static TickType_t next = 0;

void IRAM_ATTR button_isr_handler(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (now > next) {
        next = now + 200/portTICK_PERIOD_MS;
        
        gpio_set_level(RED_LED, 0);
        gpio_set_level(YELLOW_LED, 0);
        gpio_set_level(GREEN_LED, 0);
        
        enter_reset_mode(); // esp32 goes to sleep until button pressed again or timer expiry
    }
}

void enter_reset_mode() {  // sleep function, triggered by button press
    
    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);  // wake when button is pressed again

    // enable timer wake-up source
    esp_sleep_enable_timer_wakeup(DISABLE_DURATION_MS * 1000000);  // convert seconds to microseconds

    // add code to disable wifi appropriately

    printf("Entering deep sleep for %d seconds or until button press.\n", DISABLE_DURATION_MS);
    esp_deep_sleep_start();
}

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

    // initializing ADC channel 1
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);

}

float mq7_read_update() {

    // initialization
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, DEFAULT_VREF, &adc_chars);

    // get the raw voltage reading, will be converted to get PPM
    int raw_adc_value = adc1_get_raw(ADC_CHANNEL);
    uint32_t voltage = esp_adc_cal_raw_to_voltage(raw_adc_value, &adc_chars);

    const float VC = 5.0;   // sensor input voltage
    const float RL = 10.0;  // load resistor (in kOhms)
    const float RO = 10.0;  // sensor resistance in clean air (also in kOhms)

    // calculate Rs (sensor resistance)
    float RS = ((VC / (voltage / 1000.0)) - 1) * RL;

    // conversion based on MQ-7 Datasheet (linked in the report/final submission)
    float resistance_ratio = RS / RO;
    float ppm = 100.0 * pow(resistance_ratio, -1.4);  // conversion formula

    update_led(ppm);

    return ppm;

}

void update_led(ppm) {

    if (ppm > 100) {
        // enable Red LED, disable all others
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 1);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 0);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 0);
    }
    else if ((ppm <= 100) & (ppm > 50)) {
        // enable Yellow LED, disable all others
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 0);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 1);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 0);
    }
    else if (ppm <= 50) {
        // enable Green LED, disable all others
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 0);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 0);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 1);
    }
    else {
        // disable all LEDs (edge case)
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 0);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 0);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 0);
    }

}



void app_main() {

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        printf("Woke up due to button press.\n");
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        printf("Woke up due to timer expiration.\n");
    } else {
        printf("Normal boot or other wake-up cause.\n");
    }

    init_hw();
    vTaskDelay(pdMS_TO_TICKS(15000));
    mq7_read_update();
    

}
