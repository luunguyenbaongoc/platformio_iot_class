#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12

#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>

// Wifi 2.4gb only
constexpr char WIFI_SSID[] = "abcd";
constexpr char WIFI_PASSWORD[] = "123456789";

// device access token from app.coreiot.io
constexpr char TOKEN[] = "7s5pokn2se622pzn1jxu";

constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

constexpr char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr char LED_MODE_ATTR[] = "ledMode";
constexpr char LED_STATE_ATTR[] = "ledState";

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

constexpr int16_t telemetrySendInterval = 10000U;
constexpr int16_t attributeSendInterval = 10000U;

constexpr std::array<const char *, 2U> SHARED_ATTRIBUTES_LIST = {
  LED_STATE_ATTR,
  BLINKING_INTERVAL_ATTR
};

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

DHT20 dht20;

TaskHandle_t wifiTaskHandle = NULL;
TaskHandle_t thingsboardTaskHandle = NULL;
TaskHandle_t telemetryTaskHandle = NULL;
TaskHandle_t attributeTaskHandle = NULL;

SemaphoreHandle_t tbMutex;
volatile bool wifiConnected = false;
volatile bool tbConnected = false;

RPC_Response setLedSwitchState(const RPC_Data &data) {
    Serial.println("Received Switch state");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    digitalWrite(LED_PIN, newState);
    attributesChanged = true;
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setLedSwitchValue", setLedSwitchState }
};

void processSharedAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0) {
      const uint16_t new_interval = it->value().as<uint16_t>();
      if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX) {
        blinkingInterval = new_interval;
        Serial.print("Blinking interval is set to: ");
        Serial.println(new_interval);
      }
    } else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0) {
      ledState = it->value().as<bool>();
      digitalWrite(LED_PIN, ledState);
      Serial.print("LED state is set to: ");
      Serial.println(ledState);
    }
  }
  attributesChanged = true;
}

const Shared_Attribute_Callback attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback attribute_shared_request_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());

void wifiTask(void *pvParameters) {
  Serial.println("[WiFi Task] Starting...");
  
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      Serial.println("[WiFi Task] Connecting to AP...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n[WiFi Task] Connected to AP");
        Serial.print("[WiFi Task] IP Address: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("\n[WiFi Task] Failed to connect, retrying...");
      }
    } else {
      wifiConnected = true;
    }
    
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void thingsboardTask(void *pvParameters) {
  Serial.println("[ThingsBoard Task] Starting...");
  
  while (true) {
    // Wait for WiFi connection
    if (!wifiConnected) {
      tbConnected = false;
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    
    if (xSemaphoreTake(tbMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      if (!tb.connected()) {
        tbConnected = false;
        Serial.print("[ThingsBoard Task] Connecting to: ");
        Serial.print(THINGSBOARD_SERVER);
        Serial.print(" with token ");
        Serial.println(TOKEN);
        
        if (tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
          Serial.println("[ThingsBoard Task] Connected!");
          
          tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
          
          Serial.println("[ThingsBoard Task] Subscribing for RPC...");
          if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
            Serial.println("[ThingsBoard Task] Failed to subscribe for RPC");
          }
          
          if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
            Serial.println("[ThingsBoard Task] Failed to subscribe for shared attribute updates");
          }
          
          Serial.println("[ThingsBoard Task] Subscribe done");
          
          if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
            Serial.println("[ThingsBoard Task] Failed to request for shared attributes");
          }
          
          tbConnected = true;
        } else {
          Serial.println("[ThingsBoard Task] Failed to connect");
        }
      } else {
        tbConnected = true;
        tb.loop();
      }
      xSemaphoreGive(tbMutex);
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void telemetryTask(void *pvParameters) {
  Serial.println("[Telemetry Task] Starting...");
  
  while (true) {
    if (tbConnected) {
      if (xSemaphoreTake(tbMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        dht20.read();
        
        float temperature = dht20.getTemperature();
        float humidity = dht20.getHumidity();
        
        if (isnan(temperature) || isnan(humidity)) {
          Serial.println("[Telemetry Task] Failed to read from DHT20 sensor!");
        } else {
          Serial.print("[Telemetry Task] Temperature: ");
          Serial.print(temperature);
          Serial.print(" °C, Humidity: ");
          Serial.print(humidity);
          Serial.println(" %");
          
          tb.sendTelemetryData("temperature", temperature);
          tb.sendTelemetryData("humidity", humidity);
        }
        xSemaphoreGive(tbMutex);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(telemetrySendInterval));
  }
}

void attributeTask(void *pvParameters) {
  Serial.println("[Attribute Task] Starting...");
  
  while (true) {
    if (tbConnected) {
      if (xSemaphoreTake(tbMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (attributesChanged) {
          attributesChanged = false;
          tb.sendAttributeData(LED_STATE_ATTR, digitalRead(LED_PIN));
          Serial.println("[Attribute Task] LED state attribute sent");
        }

        tb.sendAttributeData("rssi", WiFi.RSSI());
        tb.sendAttributeData("channel", WiFi.channel());
        tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
        tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
        tb.sendAttributeData("ssid", WiFi.SSID().c_str());
        
        Serial.println("[Attribute Task] WiFi attributes sent");
        xSemaphoreGive(tbMutex);
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(attributeSendInterval));
  }
}

void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  pinMode(LED_PIN, OUTPUT);
  delay(1000);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  dht20.begin();

  tbMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    wifiTask,
    "WiFi Task", 
    4096,
    NULL,
    1, 
    &wifiTaskHandle,
    0 
  );
  
  xTaskCreatePinnedToCore(
    thingsboardTask,
    "ThingsBoard Task",
    8192,
    NULL,
    2,
    &thingsboardTaskHandle,
    1
  );
  
  xTaskCreatePinnedToCore(
    telemetryTask,
    "Telemetry Task",
    4096,
    NULL,
    1,
    &telemetryTaskHandle,
    1
  );
  
  xTaskCreatePinnedToCore(
    attributeTask,
    "Attribute Task",
    4096,
    NULL,
    1,
    &attributeTaskHandle,
    1
  );
}

void loop() {
}
