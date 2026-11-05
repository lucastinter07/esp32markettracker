#include "app/input_event.h"
#include <Arduino.h>

// A FreeRTOS queue, so events can later come from other tasks (e.g. a button task) safely.
static QueueHandle_t queue = nullptr;

void inputInit() {
  queue = xQueueCreate(16, sizeof(InputEvent));
}

bool inputPost(const InputEvent& ev) {
  return queue && xQueueSend(queue, &ev, 0) == pdTRUE;
}

bool inputGet(InputEvent& ev) {
  return queue && xQueueReceive(queue, &ev, 0) == pdTRUE;
}
