#include "coreiot.h"
#include <Adafruit_NeoPixel.h>
#include "led_blinky.h"

// ----------- CONFIGURE THESE! -----------
const char* coreIOT_Server = "app.coreiot.io";  
const char* coreIOT_Token = "t1hr1cgg3upha69llywm";   // Device Access Token
const char* coreIOT_User = "device_1";   // Device Access Token
const char* coreIOT_Pass = "Ngoc";   // Device Access Token
const int   mqttPort = 1883;
// ----------------------------------------

// NeoPixel setup
#define NEO_PIN 45
#define LED_COUNT 1
Adafruit_NeoPixel strip(LED_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

WiFiClient espClient;
PubSubClient client(espClient);


void reconnect() {
  // Loop until we're reconnected
  int retry_count = 0;
  while (!client.connected()) {
    retry_count++;
    Serial.print("Attempting MQTT connection #");
    Serial.print(retry_count);
    Serial.print(" to ");
    Serial.println(CORE_IOT_SERVER);
    
    // Attempt to connect using Device Token
    // CoreIOT expects: username=device_token, password=empty
    if (client.connect("esp32_device", coreIOT_Token, "")) {
        Serial.println("connected to CoreIOT Server!");
        client.subscribe("v1/devices/me/rpc/request/+");
        Serial.println("Subscribed to v1/devices/me/rpc/request/+");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      
      // Check WiFi connection
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("ERROR: WiFi disconnected! Check your WiFi connection.");
      }
      
      delay(5000);
    }
  }
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");

  // Allocate a temporary buffer for the message
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.print("Payload: ");
  Serial.println(message);

  // Parse JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* method = doc["method"];
  if (strcmp(method, "setValueNEO") == 0) {
    // Check params type (could be boolean, int, or string according to your RPC)
    bool neoState = false;
    
    // Handle different param types
    if (doc["params"].is<bool>()) {
      // If params is boolean
      neoState = doc["params"].as<bool>();
      Serial.print("Params (boolean): ");
      Serial.println(neoState ? "true" : "false");
    } else if (doc["params"].is<const char*>()) {
      // If params is string
      const char* params = doc["params"];
      if (strcmp(params, "ON") == 0 || strcmp(params, "on") == 0 || strcmp(params, "true") == 0) {
        neoState = true;
      }
      Serial.print("Params (string): ");
      Serial.println(params);
    } else if (doc["params"].is<int>()) {
      // If params is number
      neoState = (doc["params"].as<int>() != 0);
      Serial.print("Params (number): ");
      Serial.println(doc["params"].as<int>());
    }

    if (neoState) {
      Serial.println("Device turned ON.");
      isNeoBlinkEnabled = true;
      xSemaphoreGive(xBinarySemaphoreNeoBlink);
      Serial.println("Neo blink semaphore set to true.");
    } else {
      Serial.println("Device turned OFF.");
      isNeoBlinkEnabled = false;
      xSemaphoreTake(xBinarySemaphoreNeoBlink, 0);
      Serial.println("Neo blink semaphore set to false.");
    }
  } else if (strcmp(method, "setValueLED") == 0) {
    bool ledState = false;

    if (doc["params"].is<bool>()) {
      ledState = doc["params"].as<bool>();
      Serial.print("LED params (boolean): ");
      Serial.println(ledState ? "true" : "false");
    } else if (doc["params"].is<const char*>()) {
      const char* params = doc["params"];
      if (strcmp(params, "ON") == 0 || strcmp(params, "on") == 0 || strcmp(params, "true") == 0) {
        ledState = true;
      }
      Serial.print("LED params (string): ");
      Serial.println(params);
    } else if (doc["params"].is<int>()) {
      ledState = (doc["params"].as<int>() != 0);
      Serial.print("LED params (number): ");
      Serial.println(doc["params"].as<int>());
    }

    if (ledState) {
      Serial.println("LED blink turned ON.");
      isLedBlinkEnabled = true;
      xSemaphoreGive(xBinarySemaphoreLedBlink);
      Serial.println("LED blink semaphore set to true.");
    } else {
      Serial.println("LED blink turned OFF.");
      isLedBlinkEnabled = false;
      xSemaphoreTake(xBinarySemaphoreLedBlink, 0);
      digitalWrite(LED_GPIO, LOW);
      Serial.println("LED blink semaphore set to false.");
    }
  } else {
    Serial.print("Unknown method: ");
    Serial.println(method);
  }
}


void setup_coreiot(){

  // Initialize NeoPixel
  strip.begin();
  strip.clear();
  strip.show();
  Serial.println("NeoPixel initialized");

  //Serial.print("Connecting to WiFi...");
  //WiFi.begin(wifi_ssid, wifi_password);
  //while (WiFi.status() != WL_CONNECTED) {
  
  // while (isWifiConnected == false) {
  //   delay(500);
  //   Serial.print(".");
  // }

  while(1){
    if (xSemaphoreTake(xBinarySemaphoreInternet, portMAX_DELAY)) {
      break;
    }
    delay(500);
    Serial.print(".");
  }

  // Add delay to ensure DNS is ready after WiFi connection
  delay(2000);
  Serial.println("setup_coreiot Connected!");

  // Initialize MQTT server address and port
  CORE_IOT_SERVER = coreIOT_Server;
  CORE_IOT_PORT = "1883";
  
  client.setServer(CORE_IOT_SERVER.c_str(), CORE_IOT_PORT.toInt());
  client.setCallback(callback);

}

void coreiot_task(void *pvParameters){

    setup_coreiot();

    while(1){

        if (!client.connected()) {
            reconnect();
        }
        client.loop();

        // Sample payload, publish to 'v1/devices/me/telemetry'
        String payload = "{\"temperature\":" + String(glob_temperature) +  ",\"humidity\":" + String(glob_humidity) + "}";
        
        client.publish("v1/devices/me/telemetry", payload.c_str());


        
        Serial.println("Published payload: " + payload);
        vTaskDelay(10000);  // Publish every 10 seconds
    }
}