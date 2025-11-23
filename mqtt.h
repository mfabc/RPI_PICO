#ifndef _MQTT_H_
#define _MQTT_H_

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

//Configuration
#define MQTT_MAX_PACKET_SIZE 256
#define MQTT_KEEPALIVE 60
#define MQTT_PROTOCOL_LEVEL 4

#define MQTT_CONNECT    0x10
#define MQTT_CONNACK    0x20
#define MQTT_PUBLISH    0x30
#define MQTT_PUBACK     0x40
#define MQTT_SUBSCRIBE  0x82
#define MQTT_SUBACK     0x90
#define MQTT_PINGREQ    0xC0
#define MQTT_PINGRESP   0xD0
#define MQTT_DISCONNECT 0xE0

//QoS Level
#define MQTT_QOS0 0
#define MQTT_QOS1 1
#define MQTT_QOS2 2

//MQTT client state
typedef enum {
    MQTT_DISCONNECTED,
    MQTT_CONNECTING,
    MQTT_CONNECTED,
    MQTT_ERROR
} mqtt_state_t;

//MQTT client structure
typedef struct {
    uart_inst_t *uart;
    char client_id[32];
    uint16_t packet_id;
    mqtt_state_t state;
    uint32_t last_ping_time;
    uint16_t keepalive;
} mqtt_client_t;

typedef void (*mqtt_message_callback_t)(const char* topic,const uint8_t* payload,const uint16_t length);

//function protorypes
void mqtt_init(mqtt_client_t *client, uart_inst_t *uart, const char *client_id);
bool mqtt_connect(mqtt_client_t *client, const char *broker_ip, uint16_t port,
                    const char *username, const char *password);
bool mqtt_publish(mqtt_client_t *client, const char *topic, const uint8_t *payload, 
                    uint16_t length, uint8_t qos, bool retain);
bool mqtt_subscribe(mqtt_client_t *client, const char *topic, uint8_t qos);
void mqtt_loop(mqtt_client_t *client, mqtt_message_callback_t callback);
void mqtt_disconnect(mqtt_client_t *client);
bool mqtt_is_connected(mqtt_client_t *client);

//Helper functions
uint16_t mqtt_encode_string(uint8_t *buf, const char *string);
uint16_t mqtt_encode_remaining_length(uint8_t *buf, uint32_t length);
uint32_t mqtt_decode_remaining_length(uint8_t *buf, uint8_t *bytes_used);


#endif