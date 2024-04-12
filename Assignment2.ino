#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "driver/timer.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include <esp_task_wdt.h>

// Pin assignments for different tasks
const int digitalSignalPin1 = 27; // Output pin for task 1
// Input pin for frequency measurement in task 2 & 3
const int frequencyPin1 = 25;
const int frequencyPin2 = 26;
// Data structure to hold frequencies measured in tasks 2 & 3
struct Frequencies {
    int task2Frequency;
    int task3Frequency;
};
Frequencies freqData;
SemaphoreHandle_t freqSemaphore; // Semaphore for accessing shared frequency data

// Variables for interrupt handling and frequency calculation
volatile unsigned long lastRiseTime1 = 0;
volatile unsigned long lastRiseTime2 = 0;
volatile int frequency1 = 0;
volatile int frequency2 = 0;
volatile unsigned long lastInterruptTime1 = 0;
volatile unsigned long lastInterruptTime2 = 0;
const unsigned long debounceTime = 100; // Debounce time to prevent false triggering

// Pin assignments for task 4
const int ANALOG_PIN = 12;
const int ERROR_LED_PIN = 21;

// Pin assignments for task 7
const int BUTTON_PIN = 5;
const int LED_PIN = 19;
QueueHandle_t eventQueue; // Queue for button press events

// Interrupt service routines for measuring frequency on rising edge signals
void IRAM_ATTR onRise1() {
    unsigned long currentTime = micros();
    if (currentTime - lastInterruptTime1 > debounceTime) {
        unsigned long period = currentTime - lastRiseTime1;
        lastRiseTime1 = currentTime;
        lastInterruptTime1 = currentTime;
        if (period > 0) {
            frequency1 = 1000000 / period;
        }
    }
}

void IRAM_ATTR onRise2() {
    unsigned long currentTime = micros();
    if (currentTime - lastInterruptTime2 > debounceTime) {
        unsigned long period = currentTime - lastRiseTime2;
        lastRiseTime2 = currentTime;
        lastInterruptTime2 = currentTime;
        if (period > 0) {
            frequency2 = 1000000 / period;
        }
    }
}

// Task 1: Output a Digital Signal
void Task1(void *pvParameters) {
    (void) pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while(1) {
        digitalWrite(digitalSignalPin1, HIGH);
        ets_delay_us(180);

        digitalWrite(digitalSignalPin1, LOW);
        ets_delay_us(40);

        digitalWrite(digitalSignalPin1, HIGH);
        ets_delay_us(530);

        digitalWrite(digitalSignalPin1, LOW);
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(4));
    }
}


// Task 2&3: These tasks measure the frequency of signals on specific pins
void Task2(void* pvParameters) {
    while (1) {
        if (xSemaphoreTake(freqSemaphore, (TickType_t)10) == pdTRUE) {
            int scaledFrequency = map(frequency1, 333, 1000, 0, 99);
            scaledFrequency = constrain(scaledFrequency, 0, 99);
            freqData.task2Frequency = scaledFrequency;
            xSemaphoreGive(freqSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void Task3(void* pvParameters) {
    while (1) {
        if (xSemaphoreTake(freqSemaphore, (TickType_t)10) == pdTRUE) {
            int scaledFrequency = map(frequency2, 500, 1000, 0, 99);
            scaledFrequency = constrain(scaledFrequency, 0, 99);
            freqData.task3Frequency = scaledFrequency;
            xSemaphoreGive(freqSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(8));
    }
}

// Task4: Sample one analogue input and update a running average of the last 10 readings
void Task4(void *pvParameters) {
  int readings[10] = {0};
  int total = 0;
  int readIndex = 0;
  int average = 0;

  while(1) {
    int newReading = analogRead(ANALOG_PIN);
    total = total - readings[readIndex] + newReading;
    readings[readIndex] = newReading;
    readIndex = (readIndex + 1) % 10;

    average = total / 10;
    if (average > 2047) {
      digitalWrite(ERROR_LED_PIN, HIGH);
    } else {
      digitalWrite(ERROR_LED_PIN, LOW);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// Task5: Logs frequency measurements
void Task5(void* pvParameters) {
    while (1) {
        if (xSemaphoreTake(freqSemaphore, (TickType_t)10) == pdTRUE) {
            Serial.printf("%d,%d\n", freqData.task2Frequency, freqData.task3Frequency);
            xSemaphoreGive(freqSemaphore);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Task7: Control a LED with button
void Task7_1(void* pvParameters) {
  static int lastButtonState = HIGH;
  int buttonState;
  
  while (1) {
    int currentButtonState = digitalRead(BUTTON_PIN);
    // Dithering
    if (currentButtonState != lastButtonState) {
      vTaskDelay(pdMS_TO_TICKS(50)); // 50ms
      currentButtonState = digitalRead(BUTTON_PIN);
      
      if (currentButtonState != lastButtonState) {
        buttonState = currentButtonState;
        lastButtonState = currentButtonState;
        // Send status to queue
        xQueueSend(eventQueue, &buttonState, portMAX_DELAY);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
void Task7_2(void* pvParameters) {
  int buttonState;
  int ledState = LOW;
  
  while (1) {
    if (xQueueReceive(eventQueue, &buttonState, portMAX_DELAY) == pdPASS) {
      if (buttonState == LOW) {
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
      }
    }
  }
}

// Task8
void CPU_work(int time) {
  unsigned long startTime = millis();
  unsigned long endTime = startTime + time;
  // Loop until current time reaches or exceeds target end time
  while (millis() < endTime) {
  }
}
void Task8(void* pvParameters) {
  // Initialise the last wake-up time
  TickType_t xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    CPU_work(2);
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(20));
  }
}

void setup(void) {
  Serial.begin(9600);
  pinMode(digitalSignalPin1, OUTPUT);
  pinMode(frequencyPin1, INPUT);
  pinMode(frequencyPin2, INPUT);
  attachInterrupt(digitalPinToInterrupt(frequencyPin1), onRise1, RISING);
  attachInterrupt(digitalPinToInterrupt(frequencyPin2), onRise2, RISING);
  freqSemaphore = xSemaphoreCreateMutex();
  pinMode(ANALOG_PIN, INPUT);
  pinMode(ERROR_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  eventQueue = xQueueCreate(10, sizeof(int));

  // Create tasks for various operations
  xTaskCreate(Task1, "DigitalSignalOutput", 2048, NULL, 3, NULL);
  xTaskCreate(Task2, "FreqMeasure2", 2048, NULL, 2, NULL);
  xTaskCreate(Task3, "FreqMeasure3", 2048, NULL, 2, NULL);
  xTaskCreate(Task4, "AnalogInput", 2048, NULL, 1, NULL);
  xTaskCreate(Task5, "LogFrequency", 2048, NULL, 3, NULL);
  xTaskCreate(Task7_1, "MonitorButton", 2048, NULL, 1, NULL);
  xTaskCreate(Task7_2, "ControlLED", 2048, NULL, 1, NULL);
  xTaskCreate(Task8, "PeriodicCPUWork", 2048, NULL, 2, NULL);
}

void loop() {
}

