#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <SparkFun_SCD4x_Arduino_Library.h>

//========================================
// OLED
//========================================
U8G2_SSD1306_72X40_ER_F_HW_I2C u8g2(
  U8G2_R2,
  U8X8_PIN_NONE,
  6,
  5
);

//========================================
// SCD40
//========================================
SCD4x scd40;

//========================================
// Pines humedad
//========================================
#define SOIL_PIN_1 4
#define SOIL_PIN_2 3

//========================================
// ESP-NOW STRUCT
//========================================
typedef struct soil_message {
  float soil1;
  float soil2;
  float co2;
} soil_message;

soil_message datos;

//========================================
// Variables
//========================================
int humedad1 = 0;
int humedad2 = 0;
int co2 = 400;

//========================================
// Timers
//========================================
unsigned long lastSoil = 0;
unsigned long lastDisplay = 0;
unsigned long lastSend = 0;

//========================================
// MACs receptores
//========================================
uint8_t receiver1[] = {0x00, 0x4B, 0x12, 0x3D, 0x19, 0xFC};

uint8_t receiver2[] = {0x94, 0xA9, 0x90, 0x37, 0x7A, 0xEC};

esp_now_peer_info_t peerInfo;

//========================================
// Callback ESP-NOW
//========================================
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {

  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("ESP-NOW OK");
  } else {
    Serial.println("ESP-NOW ERROR");
  }
}

void setup() {

  Serial.begin(115200);
  setCpuFrequencyMhz(80);

  //====================================
  // I2C
  //====================================
  Wire.begin(5, 6);

  //====================================
  // OLED
  //====================================
  u8g2.begin();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 20, "Iniciando...");
  u8g2.sendBuffer();

  //====================================
  // ADC
  //====================================
  analogReadResolution(12);

  //====================================
  // SCD40 PRIMERO
  //====================================
  Serial.println("Iniciando SCD40...");

  if (!scd40.begin()) {

    Serial.println("ERROR SCD40");

    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "ERROR SCD40");
    u8g2.sendBuffer();

    while (1);
  }

  Serial.println("SCD40 OK");

  //====================================
  // IMPORTANTE
  //====================================
  delay(1000);

  scd40.startLowPowerPeriodicMeasurement();

  Serial.println("Esperando primera medicion...");

  //====================================
  // ESPERAR PRIMERA MEDICION REAL
  //====================================
  bool firstReading = false;

  while (!firstReading) {

    if (scd40.readMeasurement()) {

      int firstCO2 = scd40.getCO2();

      if (firstCO2 > 0) {

        co2 = firstCO2;

        firstReading = true;

        Serial.print("Primer CO2: ");
        Serial.println(co2);
      }
    }

    delay(100);
  }

  //====================================
  // DESPUES iniciar WiFi y ESP-NOW
  //====================================
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);

  esp_wifi_set_promiscuous(true);

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  esp_wifi_set_promiscuous(false);

  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {

    Serial.println("ESP-NOW ERROR");

    while (1);
  }

  esp_now_register_send_cb(OnDataSent);

  //====================================
  // Peer 1
  //====================================
  memcpy(peerInfo.peer_addr, receiver1, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);

  //====================================
  // Peer 2
  //====================================
  memcpy(peerInfo.peer_addr, receiver2, 6);

  esp_now_add_peer(&peerInfo);

  Serial.println("ESP-NOW OK");
}

void loop() {
  bool didSendEspNow = false;
  bool didUpdateDisplay = false;

  //====================================
  // PRIORIDAD ABSOLUTA AL CO2
  //====================================
  if (scd40.readMeasurement()) {

    int newCO2 = scd40.getCO2();

    if (newCO2 > 0) {

      co2 = newCO2;

      // Sin spam serial constante: CO2 se reporta al enviar ESP-NOW.
    }
  }

  //====================================
  // HUMEDAD MUY LENTA
  //====================================
  if (millis() - lastSoil > 5000) {

    lastSoil = millis();

    int value1 = analogRead(SOIL_PIN_1);
    int value2 = analogRead(SOIL_PIN_2);

    humedad1 = map(value1, 3200, 1400, 0, 100);
    humedad2 = map(value2, 3200, 1400, 0, 100);

    humedad1 = constrain(humedad1, 0, 100);
    humedad2 = constrain(humedad2, 0, 100);

    // Sin spam serial constante: humedad se reporta al enviar ESP-NOW.
  }

  //====================================
  // ESP-NOW MUY LENTO
  //====================================
  if (millis() - lastSend > 20000) {

    lastSend = millis();

    datos.soil1 = humedad1;
    datos.soil2 = humedad2;
    datos.co2 = co2;

    Serial.print("TX SOIL1: ");
    Serial.println(datos.soil1);

    Serial.print("TX SOIL2: ");
    Serial.println(datos.soil2);

    Serial.print("TX CO2: ");
    Serial.println(datos.co2);

    esp_now_send(
      receiver1,
      (uint8_t *) &datos,
      sizeof(datos)
    );

    esp_now_send(
      receiver2,
      (uint8_t *) &datos,
      sizeof(datos)
    );

    Serial.println("ESP-NOW ENVIADO");
    didSendEspNow = true;
  }

  //====================================
  // OLED
  //====================================
  if (millis() - lastDisplay > 3000) {

    lastDisplay = millis();
    u8g2.setPowerSave(0);

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x12_tr);

    char line1[20];
    char line2[20];
    char line3[20];

    sprintf(line1, "CO2:%d", co2);
    sprintf(line2, "S1 :%d%%", humedad1);
    sprintf(line3, "S2 :%d%%", humedad2);

    u8g2.drawStr(0, 12, line1);
    u8g2.drawStr(0, 26, line2);
    u8g2.drawStr(0, 40, line3);

    u8g2.sendBuffer();
    u8g2.setPowerSave(1);
    didUpdateDisplay = true;
  }

  //====================================
  // LIGHT SLEEP ENTRE CICLOS
  //====================================
  if (didSendEspNow || didUpdateDisplay) {
    esp_sleep_enable_timer_wakeup(500000);
    esp_light_sleep_start();
  } else {
    delay(10);
  }
}
