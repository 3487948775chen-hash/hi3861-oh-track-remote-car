#ifndef __MQTT_CONTROL_H__
#define __MQTT_CONTROL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmsis_os2.h"
#include "MQTTLiteOS.h"
#include "MQTTClient.h"
#include "lwip/netifapi.h"
#include "lwip/netif.h"

typedef enum {
    CMD_NONE,
    CMD_FWD,
    CMD_BACK,
    CMD_STOP,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_SPEED
} WifiCommand;

void MqttControlInit(void);
WifiCommand MqttGetCommand(void);
int MqttGetSpeed(void);
void MqttSetStatus(const char* status, int speed);
int MqttIsConnected(void);
void MqttGetIPString(char *ip, int len);

#endif
