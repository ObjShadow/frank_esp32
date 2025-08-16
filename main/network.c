//
// Created by lsk on 6/27/25.
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "network.h"
#include <string.h>
#include <malloc.h>

// Message queues to implement the communication between main task and network task
QueueHandle_t queue_send_to_network;
QueueHandle_t queue_send_to_main;

// Network task handle
TaskHandle_t network_task_handle;

// Provider
struct network_provider *current_provider;

// Callbacks
on_text_packet_callback_t on_text_packet_callback;
on_audio_packet_callback_t on_audio_packet_callback;

// Tag for logging
static const char *TAG = "network";

// Retry constants
#define MAX_RETRY_ATTEMPTS 3
#define RETRY_DELAY_MS 1000

// Internal function declarations
static void network_task(void *pvParameters);
static void process_incoming_packet(void *data, size_t size);
static void handle_packet_message(Packet *packet);

// Helper function to send packet to network task
static int send_packet_to_network(Packet *packet, TickType_t timeout, const char *packet_type);

// network retry function
static int retry(int (*operation_func)(void *arg1, size_t arg2), 
                         void *arg1, 
                         size_t arg2,
                         const char *operation_name) {
    int result;
    int retry_count = 0;
    
    do {
        result = operation_func ? operation_func(arg1, arg2) : -1;
        if (result < 0) {
            retry_count++;
            if (retry_count < MAX_RETRY_ATTEMPTS) {
                ESP_LOGW(TAG, "Failed to %s (attempt %d/%d): %d. Retrying in %d ms...", 
                         operation_name, retry_count, MAX_RETRY_ATTEMPTS, result, RETRY_DELAY_MS);
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
            } else {
                ESP_LOGE(TAG, "Failed to %s after %d attempts: %d", 
                         operation_name, retry_count, result);
            }
        }
    } while (result < 0 && retry_count < MAX_RETRY_ATTEMPTS);
    
    return result;
}

void start_network_task(struct network_provider *provider) {
    current_provider = provider;
    
    // Create message queues
    queue_send_to_network = xQueueCreate(10, sizeof(Packet*));
    queue_send_to_main = xQueueCreate(10, sizeof(void*));
    
    // Create network task
    xTaskCreate(network_task, "network_task", 4096, NULL, 5, &network_task_handle);
}

static void network_task(void *pvParameters) {
    // Initialize provider and connect to server with retry mechanism
    int result = retry((int (*)(void*, size_t))current_provider->connect_to_server, NULL, 0, "connect to server");
    if (result < 0) {
        ESP_LOGE(TAG, "Failed to connect to server after retries. Network task terminating.");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "Connected to server");
    
    // Set callback for incoming data
    current_provider->set_on_data_callback(process_incoming_packet);
    
    Packet *packet_to_send;
    
    // Main network loop
    while (1) {
        // Check if there's data to send
        if (xQueueReceive(queue_send_to_network, &packet_to_send, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Serialize the packet
            size_t packet_size = packet__get_packed_size(packet_to_send);
            uint8_t *packet_buffer = malloc(packet_size);
            
            if (packet_buffer != NULL) {
                packet__pack(packet_to_send, packet_buffer);
                
                // Send the packet with retry mechanism
                int send_result = retry(current_provider->send, packet_buffer, packet_size, "send packet");
                if (send_result < 0) {
                    ESP_LOGE(TAG, "Failed to send packet after retries");
                }
                
                // Free resources
                free(packet_buffer);
            } else {
                ESP_LOGE(TAG, "Failed to allocate memory for packet buffer");
            }
            
            // Free the packet
            packet__free_unpacked(packet_to_send, NULL);
        }
    }
}

static void process_incoming_packet(void *data, size_t size) {
    // Unpack the packet
    Packet *packet = packet__unpack(NULL, size, (const uint8_t *)data);
    
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to unpack packet");
        return;
    }
    
    // Handle the packet directly instead of creating a new task
    handle_packet_message(packet);
}

static void handle_packet_message(Packet *packet) {
    if (packet->head == NULL) {
        ESP_LOGE(TAG, "Packet has no head");
        packet__free_unpacked(packet, NULL);
        return;
    }
    
    switch (packet->head->type) {
        case MESSAGE_TYPE__MESSAGE_TYPE_TEXT:
            if (packet->body_case == PACKET__BODY_TEXT && on_text_packet_callback && packet->text && packet->text->text) {
                on_text_packet_callback(packet->text->text);
            }
            break;
            
        case MESSAGE_TYPE__MESSAGE_TYPE_AUDIO:
            if (packet->body_case == PACKET__BODY_AUDIO && on_audio_packet_callback && packet->audio) {
                struct audio_metadata metadata = {
                    .length = packet->audio->data.len,
                    .channels = packet->audio->channels,
                    .sample_rate = packet->audio->sample_rate,
                    .bits_per_sample = 16 // Default value, could be configurable
                };
                on_audio_packet_callback((void*)packet->audio->data.data, metadata);
            }
            break;
            
        case MESSAGE_TYPE__MESSAGE_TYPE_COMMAND:
            ESP_LOGI(TAG, "Received command packet, not implemented");
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown packet type: %d", packet->head->type);
            break;
    }
    
    // Free the packet
    packet__free_unpacked(packet, NULL);
}

// Helper function to clean up packet resources on send failure
static void cleanup_packet_resources(Packet *packet) {
    if (packet) {
        if (packet->head) {
            free(packet->head);
        }
        if (packet->audio) {
            if (packet->audio->data.data) {
                free(packet->audio->data.data);
            }
            free(packet->audio);
        }
        if (packet->command) {
            free(packet->command);
        }
        if (packet->text) {
            if (packet->text->text) {
                free(packet->text->text);
            }
            free(packet->text);
        }
        free(packet);
    }
}

// Helper function to send packet to network task
static int send_packet_to_network(Packet *packet, TickType_t timeout, const char *packet_type) {
    if (xQueueSend(queue_send_to_network, &packet, timeout) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send %s packet to network task", packet_type);
        // Clean up on failure
        cleanup_packet_resources(packet);
        return -1;
    }
    return 0;
}

void send_voice_to_server(void *data, struct audio_metadata metadata) {
    // Create packet
    Packet *packet = calloc(1, sizeof(Packet));
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for packet");
        return;
    }
    
    packet__init(packet);
    
    // Create packet head
    packet->head = calloc(1, sizeof(Packet__Head));
    if (packet->head == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for packet head");
        free(packet);
        return;
    }
    
    packet__head__init(packet->head);
    packet->head->version = 1;
    packet->head->type = MESSAGE_TYPE__MESSAGE_TYPE_AUDIO;
    
    // Create audio body
    packet->body_case = PACKET__BODY_AUDIO;
    packet->audio = calloc(1, sizeof(Audio));
    if (packet->audio == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio");
        cleanup_packet_resources(packet);
        return;
    }
    
    audio__init(packet->audio);
    
    // Copy audio data to ensure it remains valid during transmission
    packet->audio->data.data = malloc(metadata.length);
    if (packet->audio->data.data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for audio data");
        cleanup_packet_resources(packet);
        return;
    }
    
    memcpy(packet->audio->data.data, data, metadata.length);
    packet->audio->data.len = metadata.length;
    packet->audio->channels = metadata.channels;
    packet->audio->sample_rate = metadata.sample_rate;
    
    // Send packet to network task
    send_packet_to_network(packet, pdMS_TO_TICKS(3000), "voice");
}

void send_command_to_server(command_t command) {
    // Create packet
    Packet *packet = calloc(1, sizeof(Packet));
    if (packet == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for packet");
        return;
    }
    
    packet__init(packet);
    
    // Create packet head
    packet->head = calloc(1, sizeof(Packet__Head));
    if (packet->head == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for packet head");
        free(packet);
        return;
    }
    
    packet__head__init(packet->head);
    packet->head->version = 1;
    packet->head->type = MESSAGE_TYPE__MESSAGE_TYPE_COMMAND;
    
    // Create command body
    packet->body_case = PACKET__BODY_COMMAND;
    packet->command = calloc(1, sizeof(Command));
    if (packet->command == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for command");
        cleanup_packet_resources(packet);
        return;
    }
    
    command__init(packet->command);
    
    switch (command) {
        case COMMAND_CLEAR_HISTORY:
            packet->command->command = COMMANDS__COMMANDS_CLEAR_HISTORY;
            break;
        default:
            packet->command->command = COMMANDS__COMMANDS_CLEAR_HISTORY;
            break;
    }
    
    // Send packet to network task
    send_packet_to_network(packet, pdMS_TO_TICKS(3000), "command");
}

void set_on_text_packet(on_text_packet_callback_t callback) {
    on_text_packet_callback = callback;
}

void set_on_audio_packet(on_audio_packet_callback_t callback) {
    on_audio_packet_callback = callback;
}