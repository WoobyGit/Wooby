#include "Filters/IIRFilter.hpp"
#include <RunningAverage.h>
#include <EasyButton.h>
#include "HX711.h"
#include "mapping.h"
#include "version.h"
#include "tare.h"
#include "main.h"
#include "mpu6050.h"
#include "weight.h"
#include "Debugging.h"

const int nMeasuresTare = 7;

float TEMPREF = 26.0;

/* EasyButton */
EasyButton tareButton(PIN_PUSH_BUTTON, 50, true, true); // tareButton(BTN_PIN, debounce, pullup, invert

void myTare()
{
  DPRINTLN("TARE starting... ");
  unsigned long bTare = millis();
  scale.tare(nMeasuresTare);
  DPRINT("TARE time: "); DPRINT(float((millis()-bTare)/1000)); DPRINTLN(" s");

  // Reinitializing the filters
  weightMovAvg.fillValue(0, N_WINDOW_MOV_AVG);
  filterWeight.reset(0);

  // Reading reference temperature
  readMPU();
  TEMPREF = myTmp;
  DPRINT("Reference Temp: "); DPRINT(TEMPREF); DPRINTLN(" C");
}

void setDebugMode()
{
    if (B_DEBUG_MODE)
    {
        Serial.printf("\n\nOut of debug! \n\n");
        B_DEBUG_MODE = false;

        B_LIMITED_ANGLES = true;
        B_DISPLAY_ANGLES = false;
        B_DISPLAY_ACCEL = false;
        B_INHIB_NEG_VALS = true;
        B_SERIALTELNET = false;
        B_WIFI = false;
        B_OTA = false;
        B_BLE = false;

    }
    else
    {
        Serial.printf("\n\nDebug time! \n\n");
        B_DEBUG_MODE = true;

        B_LIMITED_ANGLES = false;
        B_DISPLAY_ANGLES = true;
        B_DISPLAY_ACCEL = true;
        B_INHIB_NEG_VALS = false;
        B_SERIALTELNET = true;
        B_WIFI = true;
        B_OTA = true;
        B_BLE = true;

    }
}

void newTare()
{
    Serial.printf("\nNew tare! \n");
    myTare();
}

void initTareButton()
{
  pinMode(PIN_PUSH_BUTTON, INPUT);

  //*         Easy Button      *//
  tareButton.begin();
  // onSequence(number_of_presses, sequence_timeout, onSequenceMatchedCallback);
  tareButton.onSequence(1,  2000,  newTare);            // For tare
  tareButton.onSequence(10, 5000, setDebugMode);        // For debug mode
  tareButton.onPressedFor(3000, setDebugMode);          // For BLE coupling
}

void newTareButtonAction()
{
  tareButton.read();
}