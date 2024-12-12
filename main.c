#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "rom/gpio.h"
#include "esp_sleep.h"
#include "driver/adc.h"
#include <math.h>

#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/api.h>
#include <lwip/netdb.h>



#define RED_LED GPIO_NUM_13
#define YELLOW_LED GPIO_NUM_12
#define GREEN_LED GPIO_NUM_14
#define BUTTON_GPIO GPIO_NUM_26
#define DISABLE_DURATION_MS 1800000 // 30 minutes
#define READ_DELAY_MS 15000

#define ADC_CHANNEL ADC1_CHANNEL_4 // GPIO 32, ADC1 Channel 4 according to github documentation
#define ADC_WIDTH ADC_WIDTH_BIT_12 // 12-bit ADC
#define ADC_ATTEN ADC_ATTEN_DB_11  // Attenuation for 0-3.3V range
#define DEFAULT_VREF 1100          // Default reference voltage in mV

#define EXAMPLE_ESP_WIFI_SSID "wifitest" //replace with wifi ssid
#define EXAMPLE_ESP_WIFI_PASS "deez6912" // replace with passsword
#define EXAMPLE_ESP_MAXIMUM_RETRY 5


#define VC 5.0       // Input voltage to the sensor (in volts)
#define RL 10.0      // Load resistor (in kOhms)
#define RO 10.0      // Sensor resistance in clean air (in kOhms)

static const char *TAG = "espressif";



//----------ADC Config--------------------



static void configure_adc(void) {
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
}



//----------WiFi Config and Functions--------------------



/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static int s_retry_num = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void connect_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    vEventGroupDelete(s_wifi_event_group);
}



//----------Deep Sleep Function--------------------



void enter_reset_mode() {  // sleep function, triggered by button press

    esp_sleep_enable_ext0_wakeup(BUTTON_GPIO, 0);  // wake when button is pressed again

    // enable timer wake-up source
    esp_sleep_enable_timer_wakeup(DISABLE_DURATION_MS * 1000);  // convert ms to us

    // disabling wifi before going to deep sleep mode
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    printf("Entering deep sleep for %d seconds or until button press.\n", DISABLE_DURATION_MS);
    esp_deep_sleep_start();
}



//----------Reset Button function--------------------



static TickType_t next = 0;

void IRAM_ATTR button_isr_handler(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (now > next) {
        next = now + 500/portTICK_PERIOD_MS;
        gpio_set_level(RED_LED, 0);
        gpio_set_level(YELLOW_LED, 0);
        gpio_set_level(GREEN_LED, 0);

        enter_reset_mode(); // esp32 goes to sleep until button pressed again or timer expiry
    }
}

//----------Initialize HW--------------------

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


    // configure adc
    configure_adc();

}

//----------Update LED Status based on PPM--------------------

void update_led(float ppm) {

    if (ppm > 100) {
        // enable Red LED, disable all others
        printf("Turning on RED_LED\n");
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 1);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 0);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 0);
    }
    else if ((ppm <= 100) && (ppm > 50)) {
        printf("Turning on Yellow_LED\n");
        // enable Yellow LED, disable all others
        gpio_pad_select_gpio(RED_LED);
        gpio_set_level(RED_LED, 0);

        gpio_pad_select_gpio(YELLOW_LED);
        gpio_set_level(YELLOW_LED, 1);

        gpio_pad_select_gpio(GREEN_LED);
        gpio_set_level(GREEN_LED, 0);
    }
    else if (ppm <= 50) {
        printf("Turning on Green_LED\n");
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

//----------Read ADC values form MQ-7 Sensor, and calculate PPM--------------------

float mq7_read_update(uint32_t adc_value) {
    // raw ADC value to voltage
    float voltage = (adc_value * VC) / 4095.0; // here we assume 12-bit resolution

    // sensor resistance Rs
    float RS = ((VC / voltage) - 1.0) * RL;

    // converting to PPM
    float resistance_ratio = RS / RO;
    float ppm = 100.0 * pow(resistance_ratio, -1.4); // based on the mq-7 dataset

    return ppm;
}

//----------Main function--------------------

void app_main() {

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
        printf("Woke up due to button press.\n");
    } else if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        printf("Woke up due to timer expiration.\n");
    } else {
        printf("Normal boot or other wake-up cause.\n");
    }


    init_hw();
    // start wifi (or re-start after deep sleep)
    connect_wifi();

    while(1) {

        int adc_value = adc1_get_raw(ADC_CHANNEL);
        float ppm = mq7_read_update(adc_value);
        update_led(ppm);
        printf("CO Concentration: %.2f ppm\n", ppm);
        vTaskDelay(pdMS_TO_TICKS(READ_DELAY_MS)); // configurable delay for sensor readings

    }
    
}
