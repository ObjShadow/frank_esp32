//
// Created by lsk on 6/27/25.
//

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "network.h"

// Message queues to implement the communication between main task and network task
QueueHandle_t queue_send_to_network;
QueueHandle_t queue_send_to_main;

// Network task handle
TaskHandle_t task_handle;

// Callbacks
void (*on_text_packet)(char* text);
void (*on_audio_packet)(void* data, size_t size);