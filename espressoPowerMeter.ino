
#include "INA226.h"
#include "Wire.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>

INA226 INA(0x45);

char txValue = 0;

#include "esp_err.h"
#include "esp_pm.h"
#include "driver/timer.h"

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define ONBOARD_LED 32
#define ONBOARD_SWITCH 26
#define BLETIMEOUT 5*60 //Seconds

BLECharacteristic *pCharacteristic;
BLEServer *pServer;
BLEService *pService;

uint8_t value[1] = {0};
volatile bool deviceConnected = false;
volatile bool buttonPressed = false;

esp_pm_config_esp32_t pm_config = {
  .max_freq_mhz = 80, // e.g. 80, 160, 240
  .min_freq_mhz = 40, // e.g. 40
  .light_sleep_enable = true,
};

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void resetTimer(){
  portENTER_CRITICAL_ISR(&timerMux);
  timerRestart(timer);
  portEXIT_CRITICAL_ISR(&timerMux);
}

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    resetTimer();
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    BLEDevice::startAdvertising();
    resetTimer();
  };
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    Serial.println("OnWrite");
  
    std::string value = pCharacteristic->getValue();
  
    if (value.length() > 0) {
      Serial.println("*********");
      Serial.print("New value: ");
      for (int i = 0; i < value.length(); i++)
        Serial.print(value[i]);
  
      Serial.println();
      Serial.println("*********");
    }
  }
};

void IRAM_ATTR switchPressToggle(){
  if (deviceConnected) {
    if (digitalRead(ONBOARD_SWITCH) == LOW) {
        digitalWrite(ONBOARD_LED, HIGH);
        buttonPressed = true;
        value[0] = 1;
        pCharacteristic->setValue(&value[0], 1);
        pCharacteristic->notify();
    } else {
        digitalWrite(ONBOARD_LED, LOW);
        buttonPressed = false;
        value[0] = 0;
        pCharacteristic->setValue(&value[0], 1);
        pCharacteristic->notify();
    }
  } else {
    digitalWrite(ONBOARD_LED, LOW);
    BLEDevice::startAdvertising();
  }
  resetTimer();
}

void setup() {
  Serial.begin(115200);
  
  Wire.begin();
  if (!INA.begin() )
  {
    Serial.println("could not connect. Fix and Reboot");
  }
  INA.setMaxCurrentShunt(1, 0.002);
  
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_N12); 
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N12);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN ,ESP_PWR_LVL_N12);
  
  esp_pm_configure(&pm_config);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_26, LOW);
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000*BLETIMEOUT, true);
  timerAlarmEnable(timer);
  
  //Initialise button state
  pinMode(ONBOARD_SWITCH, INPUT);
  switchPressToggle();
  attachInterrupt(digitalPinToInterrupt(ONBOARD_SWITCH), switchPressToggle, CHANGE);
  
  BLEDevice::init("espressoPowerMeter Device");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE |
                                         BLECharacteristic::PROPERTY_NOTIFY
                                       );

  pCharacteristic->setValue("");
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  
  BLEAdvertisementData pAdvertisementData = BLEAdvertisementData();
  pAdvertisementData.setManufacturerData((std::string)"espresso Displays");
  pAdvertisementData.setName((std::string)"espressoPowerMeter");
  pAdvertising->setAdvertisementData(pAdvertisementData);
  
  BLEDevice::startAdvertising();

}

void loop() {
  if(deviceConnected){
    //Serial.println("Device Connected");
    //txValue = INA.getBusVoltage(), 3;
    txValue = 'V';
    char txString[8];
    dtostrf(txValue, 1, 2, txString);

    pCharacteristic->setValue(txString);
    pCharacteristic->notify();
    Serial.println("sent value " + String(txString));

    txValue = INA.getBusVoltage();
   // char txString[8];
    dtostrf(txValue, 1, 2, txString);

    pCharacteristic->setValue(txString);
    pCharacteristic->notify();
    Serial.println("sent value VOLTAGE " + String(txString));

    txValue = 'C';
    //char txString[8];
    dtostrf(txValue, 1, 2, txString);

    pCharacteristic->setValue(txString);
    pCharacteristic->notify();
    Serial.println("sent value " + String(txString));

    txValue = INA.getCurrent_mA();
//    char txString[8];
    dtostrf(txValue, 1, 2, txString);

    pCharacteristic->setValue(txString);
    pCharacteristic->notify();
    Serial.println("sent value Current " + String(txString));
    delay(1000);
    
  }
}

void onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  deviceConnected = false;
  timerAlarmDisable(timer);
  esp_light_sleep_start();
  portEXIT_CRITICAL_ISR(&timerMux);
}
