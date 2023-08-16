#include <thread>
#include <M5Atom.h>
#include <driver/i2s.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include "SPIFFS.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

const std::string time_service = "M5Time";
const std::string sensor_adv = "M5Sense";
AudioGeneratorMP3 *mp3;
AudioFileSourceSPIFFS *file;
AudioOutputI2S *out;
BLEScan *scanner;

BLEServer* server = nullptr;
BLEAdvertising* advertising = nullptr;

std::shared_ptr<std::thread> th1;
std::shared_ptr<std::thread> th2;
bool night = false;

#define audio_gain 12
#define BCLK 19
#define LRCK 33
#define DataOut 22
#define PIR 32

const char* sounds[] = {
  "/mitsukado.mp3",
  "/haraokame.mp3",
};

void GetAdvertisedDevice(BLEAdvertisedDevice advertisedDevice) {
  if (advertisedDevice.haveName() && advertisedDevice.getName() == time_service) {
    Serial.print("get advertise: ");
    Serial.println(advertisedDevice.toString().c_str());
    std::string data = advertisedDevice.getManufacturerData();
    char chr;
    int idx=0;
    uint8_t len = data.size();
    Serial.printf("len: %d\n", len);
    if (len != 12) return;

    uint16_t manu_id = data[idx++] + (data[idx++] << 8);
    Serial.printf("manu_id: %04x\n", manu_id);
    if (manu_id != 0xffff) return;

    tm time;
    time.tm_year = (data[idx++] + (data[idx++] << 8)) - 1900;
    time.tm_mon = data[idx++] - 1;
    time.tm_mday = data[idx++];
    time.tm_hour = data[idx++];
    time.tm_min = data[idx++];
    time.tm_sec = data[idx++];

    uint8_t tzh  = data[idx++];
    uint8_t tzm = data[idx++];
    uint8_t wday = data[idx++];

    Serial.printf("time: %04d-%02d-%02dT%02d:%02d:%02d+%02d:%02d\n",
      time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec, tzh, tzm);

    night = (time.tm_hour < 6 || 18 <= time.tm_hour);
    Serial.printf("night flag: %s\n", night ? "true" : "false");
  }
}

void setAdvertisementData(BLEAdvertising *pAdvertising)
{
  // string領域に送信情報を連結する
  std::string strData = "";
  strData += (char)5;                 // length: 5 octets
  strData += (char)0xff;              // Manufacturer specific data
  strData += (char)0xff;              // manufacturer ID low byte
  strData += (char)0xff;              // manufacturer ID high byte
  strData += (char)0;                 // device id
  strData += (char)0;                 // count by hour

  // デバイス名とフラグをセットし、送信情報を組み込んでアドバタイズオブジェクトに設定する
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  oAdvertisementData.setName(sensor_adv);
  oAdvertisementData.setFlags(0x06); // LE General Discoverable Mode | BR_EDR_NOT_SUPPORTED
  oAdvertisementData.addData(strData);
  pAdvertising->setAdvertisementData(oAdvertisementData);
  pAdvertising->setAdvertisementType(esp_ble_adv_type_t::ADV_TYPE_NONCONN_IND);
}

void setupBLE() {
  Serial.println("Starting BLE");
  BLEDevice::init(sensor_adv);
  th1 = std::make_shared<std::thread>([&]() {
    scanner = BLEDevice::getScan();
    scanner->setActiveScan(false);
    while (true) {
      Serial.println("Begin scan.");
      BLEScanResults result = scanner->start(5);
      int cnt = result.getCount();
      for (int i = 0; i < cnt; i++) {
        GetAdvertisedDevice(result.getDevice(i));
      }
      scanner->clearResults();
      Serial.println("End scan.");
    }
  });

  server = BLEDevice::createServer();
  advertising = server->getAdvertising();

  th2 = std::make_shared<std::thread>([&]() {
    while (true) {
      setAdvertisementData(advertising);
      Serial.print("Starting Advertisement: ");
      advertising->start();
      std::this_thread::sleep_for(std::chrono::seconds(4));
      advertising->stop();
      Serial.println("Stop Advertisement. ");
    }
  });
}

int selected_sound = 0;
bool isActive = true;

void setup() {
  M5.begin(true, false, true);
  pinMode(PIR, INPUT); 

  setupBLE();

  Serial.begin(115200);
  delay(1000);
  SPIFFS.begin();
  
  out = new AudioOutputI2S(I2S_NUM_0);
  out->SetOutputModeMono(true);
  out->SetGain(audio_gain / 100.0);
  out->SetPinout(BCLK, LRCK, DataOut);

  mp3 = new AudioGeneratorMP3();
}

bool continueDetected = false;
bool pressBtn = false;
bool continuePressBtn = false;

void loop() {
  // put your main code here, to run repeatedly:
  M5.update();

  bool detected = false;

  if (pressBtn == false && M5.Btn.isPressed()) {
    Serial.println("pressed.");
    pressBtn = true;
    continuePressBtn = true;
    Serial.print("change from ");
    Serial.print(selected_sound);
    selected_sound++;
    selected_sound %= (sizeof(sounds)/sizeof(char*));
    Serial.print(" to ");
    Serial.print(selected_sound);
    Serial.println(".");
  } else if (pressBtn && M5.Btn.isReleased()) {
    Serial.println("released.");
    pressBtn = false;
  }

  if (digitalRead(PIR) == 1) {  // If pin 36 reads a value of 1.
    // detected
    detected = true;
    if (not continueDetected) {
      continueDetected = true;
      Serial.println("detected.");
    }
  } else {
    if (continueDetected) {
      continueDetected = false;
      Serial.println("undetected.");
    }
  }

  if( mp3 != NULL ){
    if (continuePressBtn) {
      continuePressBtn = false;
      if (mp3->isRunning()) {
        mp3->stop();
      }
      file = new AudioFileSourceSPIFFS(sounds[selected_sound]);
      mp3 = new AudioGeneratorMP3();
      mp3->begin(file, out);
      Serial.println("change.");
    } else {
      if ((not detected or not night) && mp3->isRunning()) {
        if (!mp3->loop()) {
          Serial.println("stop.");
          mp3->stop();
        }
      } else if ((detected or not night) && mp3->isRunning()) {
          Serial.println("stop.");
          mp3->stop();
      } else if (not detected && night && not mp3->isRunning()) {
        Serial.println("repeat.");
        file = new AudioFileSourceSPIFFS(sounds[selected_sound]);
        mp3 = new AudioGeneratorMP3();
        mp3->begin(file, out);
      }
    }
  }
}