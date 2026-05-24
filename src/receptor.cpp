#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define PIN_CENTRALA 9

RF24 radio(A0, A1);
const byte adresa[6] = "TERMO"; 

byte intensitateIncalzire = 0; 

int main(void) {
  init();

  Serial.begin(9600);
  
  pinMode(PIN_CENTRALA, OUTPUT);
  analogWrite(PIN_CENTRALA, 0); 

  if (!radio.begin()) {
    Serial.println("EROARE CRITICA: Modulul NRF24 RX nu raspunde!");
    while (1); 
  }

  radio.openReadingPipe(0, adresa);
  radio.setPALevel(RF24_PA_MIN);
  radio.startListening();
  
  Serial.println("Receptor HEATER (PWM) pornit cu succes.");
  Serial.println("Astept nivel de putere (0-255)");

  byte ultimaValoare = 255;
  while (1) {
    if (radio.available()) {
      radio.read(&intensitateIncalzire, sizeof(intensitateIncalzire));
      analogWrite(PIN_CENTRALA, intensitateIncalzire);

      // Printeaza doar cand valoarea se schimba 
      if (intensitateIncalzire != ultimaValoare) {
        int procent = map(intensitateIncalzire, 0, 255, 0, 100);
        Serial.print("Putere PWM: ");
        Serial.print(intensitateIncalzire);
        Serial.print(" / 255  (");
        Serial.print(procent);
        Serial.println("%)");
        ultimaValoare = intensitateIncalzire;
      }
    }
  }

  return 0; 
}