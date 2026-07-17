#include <stdio.h>
#include "ohos_init.h"
#include "iot_gpio.h"
#include "iot_pwm.h"
#include "hi_io.h"

#define GPIO11 11
#define GPIO12 12

/* 中断共享状态：ISR 只更新传感器与直角标志，电机控制在 TraceLine 中执行 */
static volatile IotGpioValue g_left_sensor = IOT_GPIO_VALUE0;
static volatile IotGpioValue g_right_sensor = IOT_GPIO_VALUE0;
static volatile int g_corner_left_flag = 0;
static volatile int g_corner_right_flag = 0;
static int g_sensor_isr_inited = 0;

#define MOTOR_L_IN1 0
#define MOTOR_L_IN2 1
#define MOTOR_R_IN1 9
#define MOTOR_R_IN2 10

#define PWM_PORT_L 3
#define PWM_PORT_R 0

#define MIN_DUTY 15
#define FINISH_CONSEC_COUNT 5
#define COMPENSATION_STEP 2
#define TURN_HOLD_FRAMES 150
#define TURN_INNER_RATIO 15

void MotorInit(void) {
    IoTGpioSetDir(MOTOR_L_IN2, IOT_GPIO_DIR_OUT);
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);

    IoTGpioSetDir(MOTOR_R_IN2, IOT_GPIO_DIR_OUT);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);

    hi_io_set_func(MOTOR_L_IN1, 5);
    hi_io_set_func(MOTOR_R_IN1, 5);

    IoTPwmInit(PWM_PORT_L);
    IoTPwmInit(PWM_PORT_R);
}

static void SensorUpdateState(void)
{
    IotGpioValue left, right;

    IoTGpioGetInputVal(GPIO11, &left);
    IoTGpioGetInputVal(GPIO12, &right);
    g_left_sensor = left;
    g_right_sensor = right;

    if (left == IOT_GPIO_VALUE1 && right == IOT_GPIO_VALUE0) {
        g_corner_left_flag = 1;
    } else if (left == IOT_GPIO_VALUE0 && right == IOT_GPIO_VALUE1) {
        g_corner_right_flag = 1;
    }
}

/* 只在中断里更新状态，不在 ISR 里卸载/重注册（部分 SDK 会链接失败） */
static void LeftSensorIsr(char *arg)
{
    (void)arg;
    SensorUpdateState();
}

static void RightSensorIsr(char *arg)
{
    (void)arg;
    SensorUpdateState();
}

static void SensorIsrInit(void)
{
    if (g_sensor_isr_inited) {
        return;
    }
    g_sensor_isr_inited = 1;

    /* 上升沿：检测到黑线（变高）时立即捕捉直角 */
    IoTGpioRegisterIsrFunc(GPIO11, IOT_INT_TYPE_EDGE, IOT_GPIO_EDGE_RISE_LEVEL_HIGH,
        LeftSensorIsr, NULL);
    IoTGpioRegisterIsrFunc(GPIO12, IOT_INT_TYPE_EDGE, IOT_GPIO_EDGE_RISE_LEVEL_HIGH,
        RightSensorIsr, NULL);
}

void SensorInit(void) {
    IoTGpioSetDir(GPIO11, IOT_GPIO_DIR_IN);
    hi_io_set_pull(GPIO11, 1);

    IoTGpioSetDir(GPIO12, IOT_GPIO_DIR_IN);
    hi_io_set_pull(GPIO12, 1);

    SensorUpdateState();
    SensorIsrInit();
}

void MotorStop(void) {
    IoTPwmStop(PWM_PORT_L);
    IoTPwmStop(PWM_PORT_R);
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
}

void MotorForward(int speed) {
    static int init_flag = 0;
    if (!init_flag) {
        MotorInit();
        SensorInit();
        init_flag = 1;
    }
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, speed, 4000);
    IoTPwmStart(PWM_PORT_R, speed , 4000);
}

void MotorBackward(int speed) {
    // 1. 将 IN2 设为高电平 (准备倒车)
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE1);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE1);
    
    // 2. 核心修复：反转占空比！
    // 假设你的 speed 范围是 0 - 100
    int reverse_speed = 100 - speed; 
    
    // 防止数值越界
    if (reverse_speed < 0) reverse_speed = 0;
    if (reverse_speed > 100) reverse_speed = 100;

    // 3. 将反转后的占空比输出给 IN1 (PWM 端口)
    IoTPwmStart(PWM_PORT_L, reverse_speed, 4000);
    IoTPwmStart(PWM_PORT_R, reverse_speed, 4000);
}

void MotorLeft(int speed) {
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, 0, 4000);
    IoTPwmStart(PWM_PORT_R, speed, 4000);
}

void MotorRight(int speed) {
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, speed, 4000);
    IoTPwmStart(PWM_PORT_R, 0, 4000);
}

typedef enum {
    TURN_NONE,
    TURN_LEFT,
    TURN_RIGHT
} TurnState;

static void TraceApplyTurnLeft(int base_speed)
{
    /* 左转：内侧为直行速度的比例，且不低于 MIN_DUTY，避免电机转不动 */
    int inner_speed = (base_speed * TURN_INNER_RATIO) / 100;
    if (inner_speed < MIN_DUTY) {
        inner_speed = MIN_DUTY;
    }
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, inner_speed, 4000);
    IoTPwmStart(PWM_PORT_R, base_speed, 4000);
}

static void TraceApplyTurnRight(int base_speed)
{
    /* 右转：内侧为直行速度的比例，且不低于 MIN_DUTY，避免电机转不动 */
    int inner_speed = (base_speed * TURN_INNER_RATIO) / 100;
    if (inner_speed < MIN_DUTY) {
        inner_speed = MIN_DUTY;
    }
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, base_speed, 4000);
    IoTPwmStart(PWM_PORT_R, inner_speed, 4000);
}

static void TraceApplyForward(int base_speed)
{
    IoTGpioSetOutputVal(MOTOR_L_IN2, IOT_GPIO_VALUE0);
    IoTGpioSetOutputVal(MOTOR_R_IN2, IOT_GPIO_VALUE0);
    IoTPwmStart(PWM_PORT_L, base_speed, 4000);
    IoTPwmStart(PWM_PORT_R, base_speed, 4000);
}

int TraceLine(int base_speed) {
    static int init_flag = 0;
    static int finish_count = 0;
    static TurnState turn_state = TURN_NONE;
    static int turn_hold_count = 0;
    if (!init_flag) {
        MotorInit();
        SensorInit();
        init_flag = 1;
    }

    IotGpioValue left_val, right_val;

    /* 优先响应中断捕捉到的直角拐弯 */
    if (g_corner_left_flag) {
        g_corner_left_flag = 0;
        turn_state = TURN_LEFT;
        turn_hold_count = TURN_HOLD_FRAMES;
        TraceApplyTurnLeft(base_speed);
        return 0;
    }
    if (g_corner_right_flag) {
        g_corner_right_flag = 0;
        turn_state = TURN_RIGHT;
        turn_hold_count = TURN_HOLD_FRAMES;
        TraceApplyTurnRight(base_speed);
        return 0;
    }

    /* 使用中断维护的最新状态，主循环轮询作为补充 */
    left_val = g_left_sensor;
    right_val = g_right_sensor;
    IoTGpioGetInputVal(GPIO11, &left_val);
    IoTGpioGetInputVal(GPIO12, &right_val);
    g_left_sensor = left_val;
    g_right_sensor = right_val;
  
    if (left_val == IOT_GPIO_VALUE1 && right_val == IOT_GPIO_VALUE1) {
        finish_count++;
        if (finish_count >= FINISH_CONSEC_COUNT) {
            MotorStop();
            return 1;
        } else {
            if (turn_state == TURN_LEFT) {
                TraceApplyTurnLeft(base_speed);
            } else if (turn_state == TURN_RIGHT) {
                TraceApplyTurnRight(base_speed);
            }
        }
    } else {
        finish_count = 0;
    }

    if (left_val == IOT_GPIO_VALUE0 && right_val == IOT_GPIO_VALUE0) {
        /* 转弯保持中双白：继续转弯，不中断；平时双白才直行 */
        if (turn_hold_count > 0) {
            turn_hold_count--;
            if (turn_state == TURN_LEFT) {
                TraceApplyTurnLeft(base_speed);
            } else if (turn_state == TURN_RIGHT) {
                TraceApplyTurnRight(base_speed);
            }
        } else {
            turn_state = TURN_NONE;
            TraceApplyForward(base_speed);
        }
    }
    else if (left_val == IOT_GPIO_VALUE1 && right_val == IOT_GPIO_VALUE0) {
        turn_state = TURN_LEFT;
        turn_hold_count = TURN_HOLD_FRAMES;
        TraceApplyTurnLeft(base_speed);
    }
    else if (left_val == IOT_GPIO_VALUE0 && right_val == IOT_GPIO_VALUE1) {
        turn_state = TURN_RIGHT;
        turn_hold_count = TURN_HOLD_FRAMES;
        TraceApplyTurnRight(base_speed);
    }
    else if (turn_hold_count > 0) {
        turn_hold_count--;
        if (turn_state == TURN_LEFT) {
            TraceApplyTurnLeft(base_speed);
        } else {
            TraceApplyTurnRight(base_speed);
        }
    }

    return 0;
}