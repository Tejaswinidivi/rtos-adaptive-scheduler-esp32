#define BLYNK_TEMPLATE_ID "TMPL3QHQ3mAQm"
#define BLYNK_TEMPLATE_NAME "RTOS2"
#define BLYNK_AUTH_TOKEN "lBeU5MJjIVbxS-kqtM-uYgsACVul-CsA"

#include <Arduino.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_sleep.h"

char ssid[] = "TezooA55";
char pass[] = "Divi12345";

#define MQ135_PIN      34
#define ACS712_PIN     35
#define VOLTAGE_PIN    32

#define TRIG_PIN       5
#define ECHO_PIN       18

#define RED_LED        25
#define GREEN_LED      26
#define BLUE_LED       27
#define BUZZER_PIN     14
#define SWITCH_PIN     4

#define VPIN_GAS        V0
#define VPIN_CURRENT    V1
#define VPIN_VOLTAGE    V2
#define VPIN_DISTANCE   V3
#define VPIN_SCORE      V4
#define VPIN_HEALTH     V5
#define VPIN_MODE       V6
#define VPIN_ALERT      V7
#define VPIN_CPU        V8
#define VPIN_SLACK      V9

#define GAS_ALERT        (1 << 0)
#define HIGH_CURRENT     (1 << 1)
#define LOW_BATTERY      (1 << 2)
#define OBJECT_DETECTED  (1 << 3)
#define MANUAL_ALERT     (1 << 4)

QueueHandle_t sensorQueue;
SemaphoreHandle_t mutex;
SemaphoreHandle_t binarySem;
EventGroupHandle_t eventGroup;

#define HISTORY_SIZE 5
float execHistory[HISTORY_SIZE] = {50, 50, 50, 50, 50};
int historyIndex = 0;

typedef struct
{
  int gas;
  int current;
  int voltage;
  float distance;
} SensorData;

float readDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0)
    return 999;

  return duration * 0.034 / 2;
}

void SensorTask(void *pvParameters)
{
  SensorData data;

  while (1)
  {
    data.gas = analogRead(MQ135_PIN);
    data.current = analogRead(ACS712_PIN);
    data.voltage = analogRead(VOLTAGE_PIN);
    data.distance = readDistance();

    xQueueSend(sensorQueue, &data, portMAX_DELAY);
    xSemaphoreGive(binarySem);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ProcessingTask(void *pvParameters)
{
  SensorData data;

  while (1)
  {
    if (xSemaphoreTake(binarySem, portMAX_DELAY))
    {
      xQueueReceive(sensorQueue, &data, portMAX_DELAY);

      EventBits_t bits = 0;

      if (data.gas > 550)
        bits |= GAS_ALERT;

      if (data.current > 1500)
        bits |= HIGH_CURRENT;

      if (data.voltage < 150)
        bits |= LOW_BATTERY;

      if (data.distance < 20)
        bits |= OBJECT_DETECTED;

      if (digitalRead(SWITCH_PIN) == LOW)
        bits |= MANUAL_ALERT;

      xEventGroupSetBits(eventGroup, bits);

      Serial.println("\n========== SENSOR DATA ==========");
      Serial.print("Gas: ");
      Serial.print(data.gas);

      Serial.print(" | Current: ");
      Serial.print(data.current);

      Serial.print(" | Voltage: ");
      Serial.print(data.voltage);

      Serial.print(" | Distance: ");
      Serial.println(data.distance);

      Blynk.virtualWrite(VPIN_GAS, data.gas);
      Blynk.virtualWrite(VPIN_CURRENT, data.current);
      Blynk.virtualWrite(VPIN_VOLTAGE, data.voltage);
      Blynk.virtualWrite(VPIN_DISTANCE, data.distance);

      if (bits & GAS_ALERT)
        Serial.println("GAS ALERT DETECTED");

      if (data.current > 2200)
        Serial.println("HEAVY LOAD DETECTED");
      else if (data.current > 1000)
        Serial.println("MODERATE LOAD DETECTED");
      else
        Serial.println("IDLE LOAD");

      if (bits & LOW_BATTERY)
        Serial.println("LOW BATTERY");

      if (bits & OBJECT_DETECTED)
        Serial.println("OBJECT DETECTED");

      if (bits & MANUAL_ALERT)
        Serial.println("MANUAL ALERT");
    }
  }
}

void CommunicationTask(void *pvParameters)
{
  while (1)
  {
    Blynk.run();

    xSemaphoreTake(mutex, portMAX_DELAY);
    Serial.println("Blynk Communication Running");
    xSemaphoreGive(mutex);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void SchedulerTask(void *pvParameters)
{
  while (1)
  {
    unsigned long start = millis();

    delay(50);

    unsigned long execTime = millis() - start;

    execHistory[historyIndex] = execTime;
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;

    float predictedTime = 0;

    for (int i = 0; i < HISTORY_SIZE; i++)
      predictedTime += execHistory[i];

    predictedTime /= HISTORY_SIZE;

    float deadline = 200;
    float slack = deadline - predictedTime;

    EventBits_t bits = xEventGroupGetBits(eventGroup);

    int score = 0;

    if (bits & GAS_ALERT)
      score += 100;

    if (bits & HIGH_CURRENT)
      score += 60;

    if (bits & LOW_BATTERY)
      score += 40;

    if (bits & OBJECT_DETECTED)
      score += 20;

    if (bits & MANUAL_ALERT)
      score += 80;

    if (slack < 20)
      score += 30;

    float cpuUtilization = (execTime / deadline) * 100.0;

    int systemHealth = 100 - score;

    if (systemHealth < 0)
      systemHealth = 0;

    Blynk.virtualWrite(VPIN_SCORE, score);
    Blynk.virtualWrite(VPIN_HEALTH, systemHealth);
    Blynk.virtualWrite(VPIN_CPU, cpuUtilization);
    Blynk.virtualWrite(VPIN_SLACK, slack);

    Serial.println("\n========== SCHEDULER ==========");
    Serial.print("Execution Time: ");
    Serial.println(execTime);

    Serial.print("Predicted Time: ");
    Serial.println(predictedTime);

    Serial.print("Slack Time: ");
    Serial.println(slack);

    Serial.print("Decision Score: ");
    Serial.println(score);

    Serial.print("CPU Utilization: ");
    Serial.print(cpuUtilization);
    Serial.println(" %");

    Serial.print("System Health Index: ");
    Serial.println(systemHealth);

    if (score >= 100)
    {
      Serial.println("EMERGENCY MODE");

      digitalWrite(RED_LED, HIGH);
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED, LOW);

      Blynk.virtualWrite(VPIN_MODE, "EMERGENCY MODE");
      Blynk.virtualWrite(VPIN_ALERT, "Critical Event Detected");

      Blynk.logEvent("emergency_alert", "Emergency condition detected");

      for (int i = 0; i < 2; i++)
      {
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(200));

        digitalWrite(BUZZER_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(200));
      }
    }

    else if (score >= 50)
    {
      Serial.println("NORMAL MODE");

      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(BLUE_LED, LOW);
      digitalWrite(BUZZER_PIN, LOW);

      Blynk.virtualWrite(VPIN_MODE, "NORMAL MODE");
      Blynk.virtualWrite(VPIN_ALERT, "Adaptive Scheduling Active");
    }

    else
    {
      Serial.println("POWER SAVE MODE");

      digitalWrite(RED_LED, LOW);
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(BLUE_LED, HIGH);
      digitalWrite(BUZZER_PIN, LOW);

      Blynk.virtualWrite(VPIN_MODE, "POWER SAVE MODE");
      Blynk.virtualWrite(VPIN_ALERT, "Sleep Optimization Active");

      esp_sleep_enable_timer_wakeup(1000000);
      esp_light_sleep_start();
    }

    xEventGroupClearBits(eventGroup,
                         GAS_ALERT |
                         HIGH_CURRENT |
                         LOW_BATTERY |
                         OBJECT_DETECTED |
                         MANUAL_ALERT);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void timerCallback(TimerHandle_t xTimer)
{
  Serial.println("Periodic System Health Check");
}

void setup()
{
  Serial.begin(115200);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(SWITCH_PIN, INPUT_PULLUP);

  Serial.println("Starting Hardware Test...");

  digitalWrite(RED_LED, HIGH);
  delay(500);
  digitalWrite(RED_LED, LOW);

  digitalWrite(GREEN_LED, HIGH);
  delay(500);
  digitalWrite(GREEN_LED, LOW);

  digitalWrite(BLUE_LED, HIGH);
  delay(500);
  digitalWrite(BLUE_LED, LOW);

  digitalWrite(BUZZER_PIN, HIGH);
  delay(500);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.println("Hardware Test Complete");

  sensorQueue = xQueueCreate(5, sizeof(SensorData));
  mutex = xSemaphoreCreateMutex();
  binarySem = xSemaphoreCreateBinary();
  eventGroup = xEventGroupCreate();

  TimerHandle_t timer = xTimerCreate(
                          "Timer",
                          pdMS_TO_TICKS(5000),
                          pdTRUE,
                          0,
                          timerCallback
                        );

  xTimerStart(timer, 0);

  xTaskCreate(SensorTask, "SensorTask", 4096, NULL, 3, NULL);
  xTaskCreate(ProcessingTask, "ProcessingTask", 4096, NULL, 2, NULL);
  xTaskCreate(CommunicationTask, "CommunicationTask", 4096, NULL, 1, NULL);
  xTaskCreate(SchedulerTask, "SchedulerTask", 4096, NULL, 4, NULL);
}

void loop()
{
  // FreeRTOS handles everything
}