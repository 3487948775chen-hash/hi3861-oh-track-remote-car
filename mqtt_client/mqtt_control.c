#include "mqtt_control.h"
#include "hi_wifi_api.h"

#define WIFI_SSID "cyrmagic"
#define WIFI_PASSWORD "87654321"

#define MQTT_BROKER_IP "10.46.123.53"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "ohos_hi3861"
#define MQTT_USERNAME "Still"
#define MQTT_PASSWORD "123456chen"

#define MQTT_TOPIC_SUB "name"
#define MQTT_TOPIC_PUB_STATUS "car/status"
#define MQTT_TOPIC_PUB_ID "ID"

static Network n;
static MQTTClient mq_client;
static unsigned char *mqtt_buf;
static unsigned char *mqtt_readbuf;
static int buf_size;

static volatile WifiCommand mqtt_cmd = CMD_NONE;
static volatile int mqtt_speed = 50;
static volatile int mqtt_connected = 0;
static char car_ip[20] = {0};
static char car_status[50] = "IDLE";

static void messageArrived(MessageData* data) {
    char payload[100] = {0};
    int len = data->message->payloadlen < 99 ? data->message->payloadlen : 99;
    memcpy(payload, data->message->payload, len);
    
    printf("[MQTT] Received: %s\n", payload);
    
    if (strcmp(payload, "FWD") == 0) {
        mqtt_cmd = CMD_FWD;
    } else if (strcmp(payload, "BACK") == 0) {
        mqtt_cmd = CMD_BACK;
    } else if (strcmp(payload, "STOP") == 0) {
        mqtt_cmd = CMD_STOP;
    } else if (strcmp(payload, "LEFT") == 0) {
        mqtt_cmd = CMD_LEFT;
    } else if (strcmp(payload, "RIGHT") == 0) {
        mqtt_cmd = CMD_RIGHT;
    } else if (strncmp(payload, "SPEED:", 6) == 0) {
        mqtt_speed = atoi(payload + 6);
        if (mqtt_speed < 0) mqtt_speed = 0;
        if (mqtt_speed > 100) mqtt_speed = 100;
        mqtt_cmd = CMD_SPEED;
    }
}

static int hi_wifi_start_connect(void) {
    int ret;
    hi_wifi_assoc_request assoc_req = {0};
    char ifname[16] = {0};
    int len = sizeof(ifname);
    
    ret = hi_wifi_sta_start(ifname, &len);
    if (ret != 0) {
        printf("[WIFI] Start STA failed! ret=%d\n", ret);
        return -1;
    }
    
    strcpy(assoc_req.ssid, WIFI_SSID);
    assoc_req.auth = HI_WIFI_SECURITY_WPA2PSK;
    strcpy(assoc_req.key, WIFI_PASSWORD);
    
    ret = hi_wifi_sta_connect(&assoc_req);
    if (ret != 0) {
        printf("[WIFI] Connect failed! ret=%d\n", ret);
        return -1;
    }
    
    printf("[WIFI] Connect Success!\n");
    return 0;
}

static int mqtt_connect(void) {
    int rc = 0;
    
    NetworkInit(&n);
    printf("[MQTT] Connecting to Network...\n");
    
    rc = NetworkConnect(&n, MQTT_BROKER_IP, MQTT_BROKER_PORT);
    if (rc != 0) {
        printf("[MQTT] NetworkConnect failed! rc = %d\n", rc);
        return -1;
    }
    printf("[MQTT] NetworkConnect Success!\n");
    
    buf_size = 4096 + 1024;
    mqtt_buf = (unsigned char *) malloc(buf_size);
    mqtt_readbuf = (unsigned char *) malloc(buf_size);
    if (!(mqtt_buf && mqtt_readbuf)) {
        printf("[MQTT] No memory for MQTT client buffer!\n");
        return -2;
    }
    
    MQTTClientInit(&mq_client, &n, 1000, mqtt_buf, buf_size, mqtt_readbuf, buf_size);
    
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.keepAliveInterval = 30;
    data.cleansession = 1;
    data.clientID.cstring = MQTT_CLIENT_ID;
    data.username.cstring = MQTT_USERNAME;
    data.password.cstring = MQTT_PASSWORD;
    
    rc = MQTTConnect(&mq_client, &data);
    if (rc != 0) {
        printf("[MQTT] MQTTConnect failed, rc = %d\n", rc);
        return -1;
    }
    printf("[MQTT] Connected Successful!\n");
    
    MQTTStartTask(&mq_client);
    
    rc = MQTTSubscribe(&mq_client, MQTT_TOPIC_SUB, QOS0, messageArrived);
    if (rc != 0) {
        printf("[MQTT] Subscribe failed!\n");
    } else {
        printf("[MQTT] Subscribe Success! Listening on %s\n", MQTT_TOPIC_SUB);
    }
    
    MQTTMessage message;
    char payload[] = "37120242203893";
    message.qos = QOS0;
    message.retained = 0;
    message.payload = (void *)payload;
    message.payloadlen = strlen(payload);
    
    rc = MQTTPublish(&mq_client, MQTT_TOPIC_PUB_ID, &message);
    if (rc != 0) {
        printf("[MQTT] Publish ID failed!\n");
    } else {
        printf("[MQTT] Publish ID Success!\n");
    }
    
    return 0;
}

static void MqttTask(void *arg) {
    (void)arg;
    int ret;
    
    printf("[MQTT] Task Started\n");
    
    while(1) {
        if (mqtt_connected) {
            osDelay(1000);
            continue;
        }

        printf("[MQTT] Trying to connect Wi-Fi...\n");
        
        // 【关键修复】：重试前，先断开并清理 Wi-Fi 状态
        hi_wifi_sta_disconnect();
        hi_wifi_sta_stop();
        osDelay(1000); 

        ret = hi_wifi_start_connect();
        if (ret == 0) {
            struct netif *netif = netifapi_netif_find("wlan0");
            if (netif) {
                // 停止旧的 DHCP 状态
                netifapi_dhcp_stop(netif);
                netifapi_dhcp_start(netif);
                
                // 等待 DHCP 获取 IP (最多 10 秒)
                int timeout = 100;
                while (timeout-- > 0 && ip4_addr_isany(&netif->ip_addr.u_addr.ip4)) {
                    osDelay(100); 
                }
                
                if (!ip4_addr_isany(&netif->ip_addr.u_addr.ip4)) {
                    strcpy(car_ip, ipaddr_ntoa(&netif->ip_addr));
                    printf("[WIFI] Got IP: %s\n", car_ip);
                    
                    // 仅在获取到 IP 后才尝试连接 MQTT
                    if (mqtt_connect() == 0) {
                        mqtt_connected = 1;
                        printf("[MQTT] Connection Established!\n");
                    }
                } else {
                    printf("[WIFI] DHCP Timeout, check router settings!\n");
                }
            }
        }
        
        if (!mqtt_connected) {
            printf("[MQTT] Connection failed, retrying in 5 seconds...\n");
            osDelay(5000);
        }
    }
}

void MqttControlInit(void) {
    osThreadAttr_t attr = {0};
    attr.name = "MqttTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = 4096;
    attr.priority = osPriorityNormal;
    
    if (osThreadNew(MqttTask, NULL, &attr) == NULL) {
        printf("[MQTT] Failed to create MqttTask!\n");
    }
}

WifiCommand MqttGetCommand(void) {
    WifiCommand cmd = mqtt_cmd;
    mqtt_cmd = CMD_NONE;
    return cmd;
}

int MqttGetSpeed(void) {
    return mqtt_speed;
}

void MqttSetStatus(const char* status, int speed) {
    if (!mqtt_connected) return;
    
    snprintf(car_status, sizeof(car_status), "%s,%d", status, speed);
    
    MQTTMessage message;
    message.qos = QOS0;
    message.retained = 0;
    message.payload = (void *)car_status;
    message.payloadlen = strlen(car_status);
    
    int rc = MQTTPublish(&mq_client, MQTT_TOPIC_PUB_STATUS, &message);
    if (rc != 0) {
        printf("[MQTT] Publish status failed!\n");
    }
}

int MqttIsConnected(void) {
    return mqtt_connected;
}

void MqttGetIPString(char *ip, int len) {
    strncpy(ip, car_ip, len);
}
