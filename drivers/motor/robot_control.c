#include <stdio.h>
#include "ohos_init.h"
#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "hi_time.h"
#include "hi_io.h"

#include "mqtt_control.h"

extern float GetDistance(void);
extern int TraceLine(int speed);
extern void MotorStop(void);
extern void MotorForward(int speed);
extern void OLED_Init(void);
extern void OLED_ShowStatus(const char* status, int speed);

extern void MqttControlInit(void);
extern int MqttGetSpeed(void);
extern void MqttSetStatus(const char* status, int speed);
extern int MqttIsConnected(void);
extern void MqttGetIPString(char *ip, int len);



#define BTN_GPIO 5
#define DISTANCE_THRESHOLD_STOP 15.0
#define DISTANCE_THRESHOLD_RESUME 20.0
#define LONG_PRESS_THRESHOLD 1000
#define FINISH_GUARD_TICKS 50

typedef enum {
    STATE_SET_SPEED,
    STATE_RUNNING,
    STATE_OBSTACLE,
    STATE_FINISHED,
    STATE_REMOTE
} CarState;

static CarState current_state = STATE_SET_SPEED;
static int car_speed = 50;
static int running_ticks = 0;

extern void MotorBackward(int speed);
extern void MotorLeft(int speed);
extern void MotorRight(int speed);

void RobotControlTask(void* param) {
    (void)param;
    IotGpioValue btn_val;
    float distance;
    int finish_flag = 0;
    unsigned int btn_start_time = 0;
    int btn_pressed = 0;
    WifiCommand wifi_cmd;
    char ip_str[20] = {0};

    printf("Robot Control Task Started.\r\n");

    OLED_Init();
    IoTGpioSetDir(BTN_GPIO, IOT_GPIO_DIR_IN);
    hi_io_set_pull(BTN_GPIO, 1);

    MqttControlInit();

    osDelay(100);
    
    if (MqttIsConnected()) {
        MqttGetIPString(ip_str, sizeof(ip_str));
        OLED_ShowStatus(ip_str, car_speed);
        current_state = STATE_REMOTE;
        MqttSetStatus("REMOTE", car_speed);
    } else {
        OLED_ShowStatus("SET SPEED", car_speed);
    }

    while(1) {
        switch(current_state) {
            case STATE_SET_SPEED:
            if (MqttIsConnected()) {
                    MqttGetIPString(ip_str, sizeof(ip_str));
                    OLED_ShowStatus(ip_str, car_speed);
                    current_state = STATE_REMOTE;
                    MqttSetStatus("REMOTE", car_speed);
                    break;
                }
                
                IoTGpioGetInputVal(BTN_GPIO, &btn_val);
                if (btn_val == IOT_GPIO_VALUE0) {
                    if (!btn_pressed) {
                        btn_pressed = 1;
                        btn_start_time = hi_get_us() / 1000;
                    } else {
                        if ((hi_get_us() / 1000 - btn_start_time) >= LONG_PRESS_THRESHOLD) {
                            current_state = STATE_RUNNING;
                            running_ticks = 0;
                            MotorForward(car_speed);
                            OLED_ShowStatus("FORWARD", car_speed);
                            MqttSetStatus("FORWARD", car_speed);
                            btn_pressed = 0;
                        }
                    }
                } else {
                    if (btn_pressed) {
                        if ((hi_get_us() / 1000 - btn_start_time) < LONG_PRESS_THRESHOLD) {
                            car_speed += 2;
                            if (car_speed > 70) {
                                car_speed = 50;
                            }
                            if (MqttIsConnected()) {
                                MqttGetIPString(ip_str, sizeof(ip_str));
                                OLED_ShowStatus(ip_str, car_speed);
                                current_state = STATE_REMOTE;
                                MqttSetStatus("REMOTE", car_speed);
                                btn_pressed = 0;
                                break;
                            }
                            OLED_ShowStatus("SET SPEED", car_speed);
                        }
                        btn_pressed = 0;
                    }
                }
                osDelay(5);
                break;

            case STATE_RUNNING:
                running_ticks++;
                distance = GetDistance();
                if (distance > 0 && distance < DISTANCE_THRESHOLD_STOP) {
                    current_state = STATE_OBSTACLE;
                    MotorStop();
                    OLED_ShowStatus("STOP(OBST)", 0);
                    MqttSetStatus("STOP(OBST)", 0);
                } else {
                    if (running_ticks > FINISH_GUARD_TICKS) {
                        finish_flag = TraceLine(car_speed);
                        if (finish_flag) {
                            current_state = STATE_FINISHED;
                            MotorStop();
                            OLED_ShowStatus("FINISHED!", 0);
                            MqttSetStatus("FINISHED!", 0);
                        } else {
                            OLED_ShowStatus("FORWARD", car_speed);
                            MqttSetStatus("FORWARD", car_speed);
                        }
                    } else {
                        TraceLine(car_speed);
                        OLED_ShowStatus("FORWARD", car_speed);
                        MqttSetStatus("FORWARD", car_speed);
                    }
                }
                osDelay(2);
                break;

            case STATE_OBSTACLE:
                distance = GetDistance();
                if (distance >= DISTANCE_THRESHOLD_RESUME || distance < 0) {
                    current_state = STATE_RUNNING;
                }
                osDelay(5);
                break;

            case STATE_FINISHED:
                MotorStop();
                osDelay(50);
                break;

            case STATE_REMOTE:
                wifi_cmd = MqttGetCommand();
                switch(wifi_cmd) {
                    case CMD_FWD:
                        MotorForward(car_speed);
                        OLED_ShowStatus("REMOTE FWD", car_speed);
                        MqttSetStatus("REMOTE FWD", car_speed);
                        break;
                    case CMD_BACK:
                        MotorBackward(car_speed);
                        OLED_ShowStatus("REMOTE BACK", car_speed);
                        MqttSetStatus("REMOTE BACK", car_speed);
                        break;
                    case CMD_STOP:
                        MotorStop();
                        OLED_ShowStatus("REMOTE STOP", 0);
                        MqttSetStatus("REMOTE STOP", 0);
                        break;
                    case CMD_LEFT:
                        MotorLeft(car_speed);
                        OLED_ShowStatus("REMOTE LEFT", car_speed);
                        MqttSetStatus("REMOTE LEFT", car_speed);
                        break;
                    case CMD_RIGHT:
                        MotorRight(car_speed);
                        OLED_ShowStatus("REMOTE RIGHT", car_speed);
                        MqttSetStatus("REMOTE RIGHT", car_speed);
                        break;
                    case CMD_SPEED:
                        car_speed = MqttGetSpeed();
                        OLED_ShowStatus("REMOTE SPEED", car_speed);
                        MqttSetStatus("REMOTE SPEED", car_speed);
                        break;
                    default:
                        break;
                }
                osDelay(5);
                break;
        }
    }
}

static void RobotControlDemo(void) {
    osThreadAttr_t attr;
    attr.name = "RobotControlTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 10240;
    attr.priority = osPriorityNormal;

    if (osThreadNew(RobotControlTask, NULL, &attr) == NULL) {
        printf("Failed to create RobotControlTask!\n");
    }
}
APP_FEATURE_INIT(RobotControlDemo);