#include "player_store.h"

void PlayerStore::begin() {
  mutex_ = xSemaphoreCreateMutex();
  commands_ = xQueueCreate(4, sizeof(Command));
}

void PlayerStore::post(Command command) { xQueueSend(commands_, &command, 0); }

bool PlayerStore::take(Command &command, TickType_t wait) {
  return xQueueReceive(commands_, &command, wait) == pdTRUE;
}
