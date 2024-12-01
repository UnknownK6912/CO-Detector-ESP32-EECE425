#include "driver/gpio.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"


#define RED_LED GPIO_NUM
#define YELLOW_LED GPIO_NUM
#define GREEN_LED GPIO_NUM
#define BUTTON_GPIO GIO_NUM
#define SENSOR_IN GPIO_NUM
#define SENSOR_OUT GPIO_NUM

static TickType_t next = 0;

void IRAM_ATTR button_isr_handler(void* arg) {
    TickType_t now = xTaskGetTickCountFromISR();
    if (now > next) {
        next = now + 200/portTICK_PERIOD_MS;
        // insert logic for reset here
    }
}


// the sensor requires a heating cycle before usage
// 5V for 60s and 1.4V for 90s
// switching will be done using an external mosfet
void mq7_heating_cycle() {


}

void app_main() {}