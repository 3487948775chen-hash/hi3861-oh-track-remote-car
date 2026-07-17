#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "hi_io.h"
#include "hi_time.h"

#define GPIO_8 8
#define GPIO_7 7

#define GPIO_FUNC 0
#define TIMEOUT_US 30000

float GetDistance(void) {
    static unsigned long start_time = 0, time = 0;
    float distance = 0.0;
    IotGpioValue value = IOT_GPIO_VALUE0;
    unsigned int flag = 0;
    unsigned long timeout_start = hi_get_us();

    IoTWatchDogDisable();
    hi_io_set_func(GPIO_8, GPIO_FUNC);

    IoTGpioSetDir(GPIO_8, IOT_GPIO_DIR_IN);
    IoTGpioSetDir(GPIO_7, IOT_GPIO_DIR_OUT);

    IoTGpioSetOutputVal(GPIO_7, IOT_GPIO_VALUE1);
    hi_udelay(20);
    IoTGpioSetOutputVal(GPIO_7, IOT_GPIO_VALUE0);

    while (1) {
        IoTGpioGetInputVal(GPIO_8, &value);

        if (value == IOT_GPIO_VALUE1 && flag == 0) {
            start_time = hi_get_us();
            flag = 1;
        }
        if (value == IOT_GPIO_VALUE0 && flag == 1) {
            time = hi_get_us() - start_time;
            start_time = 0;
            break;
        }

        if ((hi_get_us() - timeout_start) >= TIMEOUT_US) {
            printf("HC-SR04 timeout!\r\n");
            return -1.0;
        }
    }

    distance = time * 0.034 / 2;
    return distance;
}