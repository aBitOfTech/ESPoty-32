#pragma once

#include "player_store.h"

// Starts the task on core 0 that connects to Wi-Fi, polls Spotify, loads covers and runs the
// commands posted to `store`, so HTTP/TLS and JPEG decoding never stall touch and drawing on core 1.
void startNetworkTask(PlayerStore &store);
