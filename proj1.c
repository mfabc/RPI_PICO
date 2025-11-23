#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "CH9120.h"
#include "mqtt.h"
#include <string.h>

#define MQTT_BROKER_IP "192.168.1.10"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "Kutasznik"
#define MQTT_USERNAME NULL
#define MQTT_PASSWORD NULL
#define MQTT_TOPIC "sensors/position"


#define PIN_A           2       // Channel A (Brown wire)
#define PIN_B           3       // Channel B (Gray wire)
#define PIN_Z           6       // Index/Reference (Red wire)
#define PULSES_PER_REV  512     // ROD426 typically 512 PPR

// ===== GLOBAL VARIABLES =====
volatile int32_t position = 0;
volatile uint32_t revolutions = 0;
volatile uint8_t last_a = 0;
volatile uint8_t last_b = 0;
volatile uint8_t last_z = 0;

static uint32_t seed_core0 = 123456789;
static uint32_t seed_core1 = 987654321;

static mqtt_client_t mqtt_client;

// Callback function for incoming MQTT messages
void message_callback(const char *topic, const uint8_t *payload, uint16_t length) {
    printf("Received message on topic '%s': ", topic);
    
    // Print payload as string
    for (uint16_t i = 0; i < length; i++) {
        printf("%c", payload[i]);
    }
    printf("\n");
    
    // Example: Handle specific topics
    //if (strcmp(topic, "devices/pico/command") == 0) {
    //    if (strncmp((char*)payload, "LED_ON", length) == 0) {
    //        printf("Turning LED on\n");
    //        // gpio_put(LED_PIN, 1);
    //    } else if (strncmp((char*)payload, "LED_OFF", length) == 0) {
    //        printf("Turning LED off\n");
    //        // gpio_put(LED_PIN, 0);
    //    }
    //}
}



uint32_t rng0() {
    seed_core0 = (1103515245 * seed_core0 + 12345);
    return (seed_core0 >> 16);
}

uint32_t rng1() {
    seed_core1 = (1103515245 * seed_core1 + 12345);
    return (seed_core1 >> 16);
}

int random_range0(int min, int max) {
    return (rng0() % (max - min + 1)) + min;
}

int random_range1(int min, int max) {
    return (rng1() % (max - min + 1)) + min;
}

void core1_main() {
    while (1) {
        uint32_t r = rng1();
        uint32_t s = random_range1(1, 1000);
        double t = (double)r * (double)s;
        // Calculations running on core 1
        tight_loop_contents(); // Helps with multicore stability
    }
}

// ===== GPIO INTERRUPT HANDLER =====
void gpio_callback(uint gpio, uint32_t events) {
    uint8_t a = gpio_get(PIN_A);
    uint8_t b = gpio_get(PIN_B);
    uint8_t z = gpio_get(PIN_Z);
    
    // Quadrature decoding on Channel A change
    if (gpio == PIN_A) {
        if (a == b) {
            position--;  // Clockwise
        } else {
            position++;  // Counter-clockwise
        }
        last_a = a;
    }
    
    // Quadrature decoding on Channel B change
    if (gpio == PIN_B) {
        if (a != b) {
            position--;  // Clockwise
        } else {
            position++;  // Counter-clockwise
        }
        last_b = b;
    }
    
    // Index pulse detection (falling edge)
    if (gpio == PIN_Z && !z && last_z) {
        revolutions++;
    }
    last_z = z;
}

int main() {
    // CH9120_init() calls stdio_init_all() internally, so we call it first
    CH9120_init();
    
    // Wait for USB serial connection to stabilize
    sleep_ms(2000);
    
    printf("\n=== CH9120 Ethernet Module Test ===\n");
    printf("Configured as TCP Client\n");
    printf("Local IP: 192.168.1.200:1000\n");
    printf("Target IP: 192.168.1.10:2000\n");
    printf("UART Baud Rate: 115200\n\n");
    
    // Initialize ADC for temperature sensor
    adc_init();
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);
    
    // Launch second core for background calculations
    multicore_launch_core1(core1_main);
    printf("Core 1 launched\n\n");

    gpio_init(PIN_A);
    gpio_init(PIN_B);
    gpio_init(PIN_Z);
    gpio_set_dir(PIN_A, GPIO_IN);
    gpio_set_dir(PIN_B, GPIO_IN);
    gpio_set_dir(PIN_Z, GPIO_IN);
    
    gpio_pull_up(PIN_A);
    gpio_pull_up(PIN_B);
    gpio_pull_up(PIN_Z);

    // Read initial states
    last_a = gpio_get(PIN_A);
    last_b = gpio_get(PIN_B);
    last_z = gpio_get(PIN_Z);
    
    // Enable interrupts on both edges
    gpio_set_irq_enabled_with_callback(PIN_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(PIN_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIN_Z, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    
    const float conversion_factor = 3.3f / (1 << 12);
    uint32_t loop_count = 0;
    // Initialize MQTT client


    mqtt_init(&mqtt_client, UART_ID1, MQTT_CLIENT_ID);
    printf("Connecting to MQTT broker...\n");
    
    // Connect to MQTT broker
    if (!mqtt_connect(&mqtt_client, MQTT_BROKER_IP, MQTT_BROKER_PORT, 
                      MQTT_USERNAME, MQTT_PASSWORD)) {
        printf("Failed to send CONNECT packet\n");
        return 1;
    }


    // Wait for connection to be established
    uint32_t timeout = 10000; // 10 seconds
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    
    while (!mqtt_is_connected(&mqtt_client)) {
        mqtt_loop(&mqtt_client, message_callback);
        
        if (to_ms_since_boot(get_absolute_time()) - start_time > timeout) {
            printf("Connection timeout\n");
            return 1;
        }
        sleep_ms(10);
    }
    
    printf("Connected successfully!\n\n");
    
    // Subscribe to topics
    printf("Subscribing to topics...\n");
    mqtt_subscribe(&mqtt_client, "devices/pico/command", 0);
    mqtt_subscribe(&mqtt_client, "sensors/#", 0); // Wildcard subscription
    
    sleep_ms(1000);
    
    // Main loop
    printf("Entering main loop...\n\n");
    uint32_t last_publish = to_ms_since_boot(get_absolute_time());
    uint32_t publish_interval = 1000; // Publish every 1 seconds
    absolute_time_t last_print = get_absolute_time();
    while (1) {
        //uint8_t a_state = gpio_get(PIN_A);
        //uint8_t b_state = gpio_get(PIN_B);
        //printf("Channel A on GPIO %d\n", a_state);
        //printf("Channel B on GPIO %d\n", b_state);
        
        float angle = (float)((position % PULSES_PER_REV) * 360) / PULSES_PER_REV;
        float revs = (float)position / PULSES_PER_REV;
    
        // Print every 100ms
        if (absolute_time_diff_us(last_print, get_absolute_time()) > 100000) {
            printf("Position: %6ld | Angle: %6.1f° | Revs: %5.2f\n", 
                position, angle, revs);
        last_print = get_absolute_time();
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
    
        mqtt_loop(&mqtt_client, message_callback);
    
        // Check if still connected
        if (!mqtt_is_connected(&mqtt_client)) {
            printf("Connection lost! Attempting to reconnect...\n");
            mqtt_connect(&mqtt_client, MQTT_BROKER_IP, MQTT_BROKER_PORT,
                    MQTT_USERNAME, MQTT_PASSWORD);
            sleep_ms(5000);
            continue;
        }
    
        
        uint32_t time_since_last = now - last_publish;
        
        if (time_since_last >= publish_interval) {
            // Read temperature from ADC
            //uint16_t adc_value = adc_read();
            //float voltage = adc_value * conversion_factor;
            //float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;
        
            // Format temperature as string
            char temp_str[32];
            snprintf(temp_str, sizeof(temp_str), "%6ld", position);
        
            // Publish to MQTT broker
            //printf("Publishing sensor data: %s°C\n", temp_str);
            mqtt_publish(&mqtt_client, "sensors/position", 
                     (uint8_t*)temp_str, strlen(temp_str), 0, false);
        
            last_publish = now;
            //printf("Updated last_publish to: %lu\n", last_publish);
        }
    
        sleep_ms(10);
        }
    mqtt_disconnect(&mqtt_client);
    return 0;
}