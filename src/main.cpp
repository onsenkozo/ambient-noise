#include <thread>
#include <mutex>
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

BLEServer* server = nullptr;
BLEAdvertising* advertising = nullptr;
BLEScan *scanner = nullptr;

std::shared_ptr<std::thread> th1;
std::shared_ptr<std::thread> th2;

uint8_t deviceNo = 1;
uint8_t selected_sound = 0;
uint8_t prev_selected_sound = 255;
bool isActive = true;
bool isDetected = false;
bool night = false;
bool continueDetected = false;
bool longPress = false;
bool confirm_flag = false;

const uint8_t run = 0;
const uint8_t sound_select = 1;
const uint8_t device_no_10 = 2;
const uint8_t device_no_1 = 3;
const uint8_t confirm = 4;
uint8_t config_state = run;

uint8_t led_r = 0;
uint8_t led_g = 0;
uint8_t led_b = 0;
uint8_t blink_count = 0;
uint8_t blink_sub_count = 0;
const uint8_t blink_off_count = 2;
const uint8_t blink_on_count = 2;
const uint8_t blink_pre_on_count = 5;
const uint8_t blink_post_off_count = 5;
enum class blinkState : uint8_t {
  NonBlink,
  PreOnState,
  OnState,
  OffState,
  PostOffState,
};
blinkState blinkOnState = blinkState::NonBlink;
std::mutex _mtx;

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
  strData += (char)5;                     // length: 5 octets
  strData += (char)0xff;                  // Manufacturer specific data
  strData += (char)0xff;                  // manufacturer ID low byte
  strData += (char)0xff;                  // manufacturer ID high byte
  strData += (char)deviceNo;              // device id
  strData += (char)(isDetected ? 1 : 0);  // count by hour

  isDetected = false;

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
  server = BLEDevice::createServer();
  advertising = server->getAdvertising();

  th1 = std::make_shared<std::thread>([&]() {
    scanner = BLEDevice::getScan();
    scanner->setActiveScan(false);
    while (true) {
      setAdvertisementData(advertising);
      Serial.print("Starting Advertisement: ");
      advertising->start();

      Serial.println("Begin scan.");
      BLEScanResults result = scanner->start(5);
      int cnt = result.getCount();
      for (int i = 0; i < cnt; i++) {
        GetAdvertisedDevice(result.getDevice(i));
      }
      scanner->clearResults();
      Serial.println("End scan.");
      advertising->stop();
      Serial.println("Stop Advertisement. ");
    }
  });
}

void blinkLed() {
  // Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
  if (blinkOnState == blinkState::NonBlink) {
    if (led_r != 0 or led_g != 0 or led_b != 0) {
      M5.dis.drawpix(0, CRGB(led_r, led_g, led_b));
    }
  } else if (blink_count == 0 and blink_sub_count == 0 and blinkOnState == blinkState::PreOnState) {
     M5.dis.drawpix(0, CRGB(0, 0, 0));
     blink_sub_count = blink_post_off_count;
     blinkOnState == blinkState::PostOffState;
     Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
  } else if (blinkOnState != blinkState::NonBlink) {
    if (blinkOnState == blinkState::OnState) {
      if (blink_sub_count == 0) {
        blink_sub_count = blink_on_count;
        M5.dis.drawpix(0, CRGB(led_r, led_g, led_b));
        Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
      } else {
        blink_sub_count--;
        if (blink_sub_count == 0) {
          blinkOnState = blinkState::OffState;
          Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
        }
      }
    } else {
      if (blink_sub_count == 0) {
        switch (blinkOnState) {
          case blinkState::PreOnState:
            blink_sub_count = blink_pre_on_count;
            Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
            break;
          case blinkState::OffState:
            blink_sub_count = blink_off_count;
            Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
            break;
          case blinkState::PostOffState:
            blink_sub_count = blink_post_off_count;
            Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
            break;
        }
        M5.dis.drawpix(0, CRGB(0, 0, 0));
      } else {
        blink_sub_count--;
        if (blink_sub_count == 0) {
          switch (blinkOnState) {
            case blinkState::PreOnState:
              blinkOnState = blinkState::OnState;
              Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
              break;
            case blinkState::OffState:
              if (blink_count > 0) {
                blink_count--;
                Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
              }
              if (blink_count > 0) {
                blinkOnState = blinkState::OnState;
              } else {
                blinkOnState = blinkState::PostOffState;
                Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
              }
              break;
            case blinkState::PostOffState:
              blinkOnState = blinkState::NonBlink;
              Serial.printf("Blink Count: %d Blink State: %d Blink Sub Count: %d.\n", blink_count, blinkOnState, blink_sub_count);
              break;
          }
        }
      }
    }
  }
}

void setupLed() {
  th2 = std::make_shared<std::thread>([&]() {
    while (true) {
      blinkLed();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}

void ledChange() {
  if (config_state == confirm) {
    if (confirm_flag) {
      led_r = 0;
      led_g = 255;
      led_b = 0;
    } else {
      led_r = 255;
      led_g = 0;
      led_b = 0;
    }
    M5.dis.drawpix(0, CRGB(led_r, led_g, led_b));
  }
}

void changeConfigState() {
  switch (config_state) {
    case run:
      config_state = sound_select;
      led_r = 255;
      led_g = 255;
      led_b = 255;
      Serial.printf("Change state from run to sound select.\n");
      break;
    case sound_select:
      config_state = device_no_10;
      led_r = 0;
      led_g = 255;
      led_b = 255;
      Serial.printf("Change state from sound select to device no 10's order.\n");
      break;
    case device_no_10:
      config_state = device_no_1;
      led_r = 0;
      led_g = 0;
      led_b = 255;
      Serial.printf("Change state from device no 10's order to device no 1's order.\n");
      break;
    case device_no_1:
      config_state = confirm;
      led_r = 255;
      led_g = 0;
      led_b = 0;
      Serial.printf("Change state from device no 1's order to confirm save.\n");
      break;
    case confirm:
      config_state = run;
      led_r = 0;
      led_g = 0;
      led_b = 0;
      Serial.printf("Change state from confirm save to run.");
      break;
  }
}

void setup() {
  M5.begin(true, false, true);
  pinMode(PIR, INPUT); 

  setupBLE();
  setupLed();

  M5.dis.clear();

  Serial.begin(115200);
  delay(1000);
  SPIFFS.begin();
  
  out = new AudioOutputI2S(I2S_NUM_0);
  out->SetOutputModeMono(true);
  out->SetGain(audio_gain / 100.0);
  out->SetPinout(BCLK, LRCK, DataOut);

  mp3 = new AudioGeneratorMP3();
}

void loop() {
  // blinkLed();

  M5.update();  // ボタン状態更新

  // 本体スイッチ処理
  if (!longPress and M5.Btn.pressedFor(3000)) {  // 3秒間ボタンが押されていれば
    changeConfigState();
    ledChange();                  // 液晶画面表示変更
    longPress = true;
  } else if (longPress and M5.Btn.wasReleased()) {
    longPress = false;
  } else if (M5.Btn.wasReleased()) {
    switch (config_state) {
      case sound_select:
        Serial.printf("change from %d", selected_sound);
        selected_sound++;
        selected_sound %= (sizeof(sounds)/sizeof(char*));
        Serial.printf(" to %d.\n", selected_sound);
        ledChange();                  // 液晶画面表示変更
        blink_count = selected_sound + 1;
        blinkOnState = blinkState::PreOnState;
        Serial.printf("Sound Select Blink countt: %d.\n", blink_count);
        break;
      case device_no_10:
        deviceNo = (deviceNo + 10) % 100;
        Serial.printf("Device NO: %d\n", deviceNo);
        ledChange();                  // 液晶画面表示変更
        blink_count = deviceNo / 10;
        blinkOnState = blinkState::PreOnState;
        Serial.printf("Device No 10: %d.\n", blink_count);
        break;
      case device_no_1:
        deviceNo = (deviceNo / 10) * 10 + ((deviceNo % 10) + 1) % 10;
        if (deviceNo == 0) {
          deviceNo = 1;
        }
        Serial.printf("Device NO: %d\n", deviceNo);
        ledChange();                  // 液晶画面表示変更
        blink_count = deviceNo % 10;
        blinkOnState = blinkState::PreOnState;
        Serial.printf("Device No 1: %d.\n", blink_count);
        break;
      case confirm:
        Serial.printf("SAVE? %d", confirm_flag);
        confirm_flag = !confirm_flag; // flag状態反転
        Serial.printf(" to %d.\n", confirm_flag);
        ledChange();                  // 液晶画面表示変更
        break;
    }
  }

  bool detected = false;

  if (digitalRead(PIR) == 1) {  // If pin 36 reads a value of 1.
    // detected
    isDetected = true;
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
    if (prev_selected_sound != selected_sound) {
      prev_selected_sound = selected_sound;
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