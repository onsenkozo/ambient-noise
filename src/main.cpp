#include <M5Atom.h>
#include <driver/i2s.h>
#include "SPIFFS.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

AudioGeneratorMP3 *mp3;
AudioFileSourceSPIFFS *file;
AudioOutputI2S *out;

#define audio_gain 12
#define BCLK 19
#define LRCK 33
#define DataOut 22
#define PIR 32

const char* sounds[] = {
  "/mitsukado.mp3",
  "/haraokame.mp3",
};

int selected_sound = 0;

void setup() {
    // put your setup code here, to run once:

  M5.begin(true, false, true);
  pinMode(PIR, INPUT); 

  Serial.begin(115200);
  delay(1000);
  SPIFFS.begin();
  
  audioLogger = &Serial;

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
      if (not detected && mp3->isRunning()) {
        if (!mp3->loop()) {
          Serial.println("stop.");
          mp3->stop();
        }
      } else if (detected && mp3->isRunning()) {
          Serial.println("stop.");
          mp3->stop();
      } else if (not detected && not mp3->isRunning()) {
        Serial.println("repeat.");
        file = new AudioFileSourceSPIFFS(sounds[selected_sound]);
        mp3 = new AudioGeneratorMP3();
        mp3->begin(file, out);
      }
    }
  }
}