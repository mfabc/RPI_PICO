#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <mqtt.h>

static uint8_t tx_buffer[MQTT_MAX_PACKET_SIZE];
static uint8_t rx_buffer[MQTT_MAX_PACKET_SIZE];

void mqtt_init(mqtt_client_t *client,uart_inst_t *uart, const char *client_id){
                client->uart = uart;
                strncpy(client->client_id, client_id, sizeof(client->client_id) - 1);
                client->client_id[sizeof(client->client_id) - 1] = '\0';
                client->packet_id = 1;
                client->state = MQTT_DISCONNECTED;
                client->keepalive = MQTT_KEEPALIVE;
                client->last_ping_time = 0;

}

uint16_t mqtt_encode_string(uint8_t *buf, const char *str) {
    uint16_t len = strlen(str);
    buf[0] = (len >> 8) & 0xFF;
    buf[1] = len & 0xFF;
    memcpy(&buf[2],str,len);
    return len + 2;
}

uint16_t mqtt_encode_remaining_length(uint8_t *buf, uint32_t length) {
    uint8_t pos = 0;
    do {
        uint8_t digit = length % 128;
        length /= 128;
        if (length > 0) {
            digit |= 0x80;
        }
        buf[pos++] = digit;
    } while (length > 0);
    return pos;
}

uint32_t mqtt_decode_remaining_length(uint8_t *buf, uint8_t *bytes_used) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    uint8_t pos = 0;
    uint8_t digit;

    do{
        digit = buf[pos++];
        value += (digit & 127) * multiplier;
        multiplier *= 128;
        } while ((digit & 128) != 0 && pos < 4);
        *bytes_used = pos;
        return value;
}

bool mqtt_connect(mqtt_client_t *client, const char *broker_ip, uint16_t port, 
                    const char *username, const char *password) {
                        uint16_t pos = 0;

                        //fixed header
                        tx_buffer[pos++] = MQTT_CONNECT;
                        //variable header
                        uint16_t var_header_start = pos + 4; //reserve space for remaining length
                        pos = var_header_start;

                        //protocol name
                        pos += mqtt_encode_string(&tx_buffer[pos], "MQTT");
                        //protocol level 3.1.1
                        tx_buffer[pos++] = MQTT_PROTOCOL_LEVEL;

                        //connect flags
                        uint8_t flags = 0x02;
                        if (username != NULL) flags |= 0x80;
                        if (password != NULL) flags |= 0x40;
                        tx_buffer[pos++] = flags;
                    
                        //Keepalive seconds
                        tx_buffer[pos++] = (client->keepalive >> 8) & 0xFF;
                        tx_buffer[pos++] = client->keepalive & 0xFF;
                    
                        //payload - client ID
                        pos += mqtt_encode_string(&tx_buffer[pos], client->client_id);

                        //username
                        if (username != NULL) {
                            pos += mqtt_encode_string(&tx_buffer[pos], username);
                        }

                        //passwrod
                        if (password != NULL) {
                            pos += mqtt_encode_string(&tx_buffer[pos], password);
                        }
                        //calculate and insert remaining length
                        uint32_t remaining_length = pos - var_header_start;
                        uint8_t rl_bytes = mqtt_encode_remaining_length(&tx_buffer[1], remaining_length);

                        if (rl_bytes > 4) {
                            return false;
                        }
                        memmove(&tx_buffer[1 + rl_bytes], &tx_buffer[var_header_start], remaining_length);
                        pos = 1 + rl_bytes + remaining_length;

                        //send packet
                        uart_write_blocking(client->uart, tx_buffer, pos);
                        client->state = MQTT_CONNECTING;

                        return true;
                    }
bool mqtt_publish(mqtt_client_t *client, const char *topic, const uint8_t *payload, 
                    uint16_t length, uint8_t qos, bool retain) {
    if (client->state != MQTT_CONNECTED) {
        return false;
    }
    
    uint16_t pos = 0;
    
    // Fixed header - MQTT_PUBLISH with QoS and retain flags
    uint8_t fixed_header = MQTT_PUBLISH;
    if (retain) {
        fixed_header |= 0x01;  // Retain flag
    }
    fixed_header |= (qos << 1);  // QoS level (bits 1-2)
    
    tx_buffer[pos++] = fixed_header;
    
    // Reserve space for remaining length
    uint16_t rl_pos = pos;
    pos += 4;
    
    // Variable header - Topic name
    pos += mqtt_encode_string(&tx_buffer[pos], topic);
    
    // Packet identifier (only for QoS > 0)
    if (qos > 0) {
        tx_buffer[pos++] = (client->packet_id >> 8) & 0xFF;
        tx_buffer[pos++] = client->packet_id & 0xFF;
        client->packet_id++;
    }
    
    // Payload
    if (payload != NULL && length > 0) {
        // Check if payload fits in buffer
        if (pos + length > MQTT_MAX_PACKET_SIZE) {
            printf("MQTT: Payload too large\n");
            return false;
        }
        memcpy(&tx_buffer[pos], payload, length);
        pos += length;
    }
    
    // Calculate and insert remaining length
    uint32_t remaining_length = pos - rl_pos - 4;
    uint8_t rl_bytes = mqtt_encode_remaining_length(&tx_buffer[rl_pos], remaining_length);
    
    if (rl_bytes > 4) {
        return false;
    }
    
    // Adjust for actual remaining length size
    memmove(&tx_buffer[rl_pos + rl_bytes], &tx_buffer[rl_pos + 4], remaining_length);
    pos = rl_pos + rl_bytes + remaining_length;
    
    // Send packet
    uart_write_blocking(client->uart, tx_buffer, pos);
    printf("MQTT: Published to '%s' (%d bytes)\n", topic, length);
    
    return true;
}




bool mqtt_subscribe(mqtt_client_t *client, const char *topic, uint8_t qos) {
                    if (client->state != MQTT_CONNECTED) {
                        return false;
                    }
                    uint16_t pos = 0;

                    //fixed header
                    tx_buffer[pos++] = MQTT_SUBSCRIBE;

                    //reserve space for remaining lenth
                    uint16_t rl_pos = pos;
                    pos += 4;

                    //Variable header - packet identifier
                    tx_buffer[pos++] = (client->packet_id >> 8) & 0xFF;
                    tx_buffer[pos++] = client->packet_id & 0xFF;
                    client->packet_id++;

                    //Payload topic filter
                    uint16_t topic_len = strlen(topic);
                    tx_buffer[pos++] = (topic_len >> 8) & 0xFF;
                    tx_buffer[pos++] = topic_len & 0xFF;
                    memcpy(&tx_buffer[pos],topic,topic_len);
                    pos += topic_len;

                    //QoS
                    tx_buffer[pos++] = qos;

                    //Calculate remaining length
                    uint32_t remaining_length = pos - rl_pos - 4;
                    uint8_t rl_bytes = mqtt_encode_remaining_length(&tx_buffer[rl_pos], remaining_length);

                    //Adjust for actual remaining length size
                    memmove(&tx_buffer[rl_pos + rl_bytes], &tx_buffer[rl_pos + 4], remaining_length);
                    pos = rl_pos + rl_bytes + remaining_length;

                    //Send packet
                    uart_write_blocking(client->uart, tx_buffer, pos);
                    printf("MQTT: Subscribed to '%s' \n",topic);

                    return true;
                    }

// Send ping request
static void mqtt_ping(mqtt_client_t *client) {
    tx_buffer[0] = MQTT_PINGREQ;
    tx_buffer[1] = 0;
    uart_write_blocking(client->uart, tx_buffer, 2);
    client->last_ping_time = to_ms_since_boot(get_absolute_time());
}

void mqtt_loop(mqtt_client_t *client, mqtt_message_callback_t callback) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (client->state == MQTT_CONNECTED &&
        (now - client->last_ping_time) > client->keepalive * 1000/2) {
            mqtt_ping(client);
        }
    
    //check for incoming data
    if(!uart_is_readable(client->uart)){
        return;
    }

    //Read the fixed header
    uint8_t packet_type = uart_getc(client->uart);
    //Read remaining length
    uint8_t rl_bytes_used;
    uint8_t rl_buf[4];
    uint8_t rl_idx = 0;

    do {
        while (!uart_is_readable(client->uart)) {
            sleep_us(10);
        }
        rl_buf[rl_idx] = uart_getc(client->uart);
    } while ((rl_buf[rl_idx++] & 0x80) && rl_idx < 4);

    uint32_t remaining_length = mqtt_decode_remaining_length(rl_buf, &rl_bytes_used);

    //Read packet data
    for(uint32_t i = 0; i < remaining_length && i < MQTT_MAX_PACKET_SIZE; i++) {
        while(!uart_is_readable(client->uart)) {
            sleep_us(10);
        }
        rx_buffer[i] = uart_getc(client->uart);
    }

    //Process packet
    switch(packet_type & 0xF0) {
        case MQTT_CONNACK:
            if (rx_buffer[1] == 0x00) {
                client->state = MQTT_CONNECTED;
                client->last_ping_time = now;
                printf("MQTT: Connected! \n");
                } else {
                    client->state = MQTT_ERROR;
                    printf("MQTT: Connection failed, code: %d\n",rx_buffer[1]);
                }
                break;
        
        case MQTT_PUBLISH: {
            //Parse topic
            uint16_t topic_len = (rx_buffer[0] << 8) | rx_buffer[1];
            char topic[128];
            if (topic_len < sizeof(topic)) {
                memcpy(topic, &rx_buffer[2], topic_len);
                topic[topic_len] = '\0';

                //Parse payload
                uint16_t payload_offset = 2 + topic_len;
                //Check if QoS > 0 (Has packet ID)
                uint8_t qos = (packet_type >> 1) & 0x03;
                if (qos > 0) {
                    payload_offset += 2; //skip packet ID
                }
                uint16_t payload_len = remaining_length - payload_offset;

                if (callback != NULL) {
                    callback(topic, &rx_buffer[payload_offset], payload_len);
                }

            }
            break;
        }
        case MQTT_PINGRESP:
            //Keep alive response received
            break;
        case MQTT_SUBACK:
            printf("MQTT: Subscription confirmed. \n");
            break;

    }
}
void mqtt_disconnect(mqtt_client_t *client) {
    tx_buffer[0] = MQTT_DISCONNECT;
    tx_buffer[1] = 0;
    uart_write_blocking(client->uart, tx_buffer, 2);
    client->state = MQTT_DISCONNECTED;
    printf("MQTT: Disconnected.\n");
}

bool mqtt_is_connected(mqtt_client_t *client) {
    return client->state == MQTT_CONNECTED;
}

