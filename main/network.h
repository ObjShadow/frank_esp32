//
// Created by lsk on 6/27/25.
//

#ifndef NETWORK_H
#define NETWORK_H

#include "proto/packet.pb-c.h"

struct network_provider {
	int id;
  	int (*connect_to_server)(void);
    int (*send)(void *data, size_t size);
	int (*receive)(void *data, size_t size);
};

static void (*on_text_packet)(char* text);
static void (*on_audio_packet)(void* data, size_t size);

void start_network_task(struct network_provider *provider);
void send_voice_to_server(void *data, size_t size);
void send_command_to_server(Commands command);
void set_one

#endif //NETWORK_H
