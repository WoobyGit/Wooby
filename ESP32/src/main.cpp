// #include "U8glib.h"
#include <U8g2lib.h>                 // by Oliver
#include "HX711.h"
// #include "Vcc.h"
#include <Wire.h>
#include <math.h>

#include "WoobyImage.h"
// #include "Filter.h"
#include "WoobyWiFi.h"
#include "Debugging.h"

//************************//
//*      VERSION SEL     *//
//************************//

  #define MODEL 2
  #define TYPE 0
  // TYPE = 0 (PROTOTYPE)
  // TYPE = 1 (FINAL DELIVERY)

//************************//
//*      SENSOR CONF     *//
//************************//
  #define DOUT 19     // For Arduino 6
  #define CLK  18     // For Arduino 5
  #define TARE_BUTTON 27

  // For Arduino:
  // HX711 scale(DOUT, CLK);
  // For ESP:
  HX711 scale;

  int nMeasures = 7;
  int nMeasuresTare = 15;

  // Model choice
  #if MODEL == 1
    float calibration_factor=42.00;
  #endif

  #if MODEL == 2
    // OLD BOOT LOADER
    float calibration_factor=42.7461;
  #endif

  #if MODEL == 3
    // OLD BOOT LOADER
    float calibration_factor=61.7977;
  #endif

  #if MODEL == 4
    float calibration_factor= 38.5299;
  #endif

  int gain = 64;  // Reading values with 64 bits (could be 128 too)

  float MAX_GR_VALUE = 11000; // in gr
  float MIN_GR_VALUE = 5;    // in gr

  float correctedValueFiltered = 0;
  float displayFinalValue = 0;
  float displayFinalValue_1 = 0;

  unsigned long tBeforeMeasure = 0;
  unsigned long tAfterMeasure = 0;
  unsigned long tAfterAlgo = 0;

  char arrayMeasure[8];

//************************//
//*   LOAD SENSOR ADJ    *//
//************************//
  float TEMPREF = 26.0;

  float const P3 =   -1.2349e-06;
  float const P2 =    1.3114e-05;
  float const P1 =  -46.122436;
  float const P0 =  0.0;

  bool B_ANGLE_ADJUSTMENT = true;
  float const calib_theta_2 = -0.00014;
  float relativeVal_WU = 0;
  float realValue_WU_AngleAdj = 0;

  float tempCorrectionValue_WU = 0;
  float tempCorrectionValue = 0;

//************************//
//*     TARE BUTTON      *//
//************************//
  const int PIN_PUSH_BUTTON = 27;
  unsigned long countTimeButton;

//************************//
//*  VCC MANAGEMENT CONF *//
//************************//
  const float VCCMIN   = 0.0;           // Minimum expected Vcc level, in Volts.
  const float VCCMAX   = 5.0;           // Maximum expected Vcc level, in Volts.
  const float VCCCORR  = 5.0/5.01;      // Measured Vcc by multimeter divided by reported Vcc

  float myVcc, myVccFiltered, ratioVCCMAX;
  bool B_VCCMNG = false;
  /*Vcc vcc(VCCCORR);
  Filter VccFilter(0.65, 10); // (Sampling time (depending on the loop execution time), tau for filter
  */

//************************//
//*   TARE BUTTON CONF   *//
//************************//

  #define TARE_BUTTON_PIN 27

  int tareButtonStateN   = 0;
  int tareButtonStateN_1 = 0;
  int tareButtonFlank    = 0;

  unsigned long tStartTareButton = 0;
  unsigned long tEndTareButton = 0;


//************************//
//*      DISPLAY CONF    *//
//************************//

// For Arduino:
//  U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0|U8G_I2C_OPT_NO_ACK|U8G_I2C_OPT_FAST); // Fast I2C / TWI
// For ESP32
  U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// For ESP32, replace 'setPrintPos' by 'setCursor'.

  int state = 0;
  char static aux[21] = "01234567890123456789";

  #define DISPLAY_WIDTH 128
  #define DISPLAY_HEIGHT 64

  bool B_LIMITEDANGLES = false;
  bool B_INHIB_NEG_VALS = false;

//************************//
//*    INACTIVITY CONF   *//
//************************//

  const float MAX_INACTIVITY_TIME = 100000; // in milliseconds
  const float INACTIVE_THR  = 5.0;
  bool bInactive = false;
  unsigned long lastTimeActivity = 0;
  const byte interruptPin = 2;

//************************//
//*    GYROSCOPE CONF    *//
//************************//
  const int MPU_ADDR=0x68;
  const float pi = 3.1416;
  int16_t Ax,Ay,Az,Tmp,Gx,Gy,Gz;
  float myAx, myAy, myAz, myTmp, myGyroX, myGyroY, myGyroZ;
  float thetadeg, phideg ;

  const float MAX_THETA_VALUE = 10;
  const float MAX_PHI_VALUE = 10;

//************************//
//*  COMMUNICATION CONF  *//
//************************//
  bool B_HTTPREQ = false;
  bool B_SERIALPORT = true;

  unsigned long countForGoogleSend = 0;

//************************//
//*  FILTERING FUNCTIONS *//
//************************//
  struct filterResult {
    float yk;
    int   bSync;
  };

  filterResult realValueFilterResult;

  float realValue;
  float realValue_1;
  float realValueFiltered;
  float realValueFiltered_1;
  int bSync;
  int bSync_corr = 0;

  float realValue_WU = 0;

  float correctedValue = 0;
  float offset = 0;

//*******************************//
//*      WEIGHTING ALGORITHM    *//
//*******************************//

void myTare(){
  Serial.println("TARE starting... ");
  unsigned long bTare = millis();
  long tareWU = scale.tare(nMeasuresTare);
  Serial.print("TARE time: "); Serial.print(float((millis()-bTare)/1000));Serial.println(" s");
  // float tareGR = tareWU/scale.get_scale(); this does NOT mean anything

  //readTemp();
  TEMPREF = myTmp;

  Serial.print("Reference Temp:"); Serial.println(TEMPREF);
}

float correctionTemp(float beforeCorrectionValue){
  float deltaTemp = myTmp - TEMPREF;
  return ( beforeCorrectionValue / (P3*pow(deltaTemp,3) + P2*pow(deltaTemp,2) + P1*deltaTemp + P0) );
}

float correctionAlgo(float realValue){

  int realValueInt = int(realValue);
  float realValueDecim = (realValue - float(realValueInt)) ;
  float correctedValue = 0;

  // Around zero values deletion //
    if (realValue< MIN_GR_VALUE && realValue>-1*MIN_GR_VALUE){
      return correctedValue = 0.0;
    }

  // Round algorithm //
    if ( realValueDecim < 0.4) { correctedValue = long(realValueInt);}
    if ( realValueDecim >= 0.4 && realValueDecim <= 0.7) { correctedValue = long(realValueInt)+0.5 ; }
    if ( realValueDecim > 0.7) { correctedValue = long(realValueInt)+1; }

  return correctedValue;
}

filterResult filtering(float uk, float uk_1, float yk_1){
  // Definition of the coeffs for the filter
  float a =  0.3915;
  float b =  0.6085;

  filterResult myResult;
  if (abs(uk-uk_1) < MIN_GR_VALUE) {
    // Filtering
    myResult.yk = a*uk_1 + b*yk_1;
    myResult.bSync = 0;
    }
  else{
    // Syncing
    myResult.yk = uk;
    myResult.bSync = 1;
  }

  return myResult;
}

//********************++++****//
//*   TARE BUTTON FUNCTIONS  *//
//*********************++++***//

void initTareButton(){
  DPRINTLN("Initializing the tare button ... ");
  tareButtonStateN   = 0;
  tareButtonStateN_1 = 0;
  tareButtonFlank    = 0;
  tStartTareButton = 0;
  tEndTareButton = 0;
}

int updateTareButton(){
  delay(100);

  // Update
  tareButtonStateN_1 = tareButtonStateN;
  tareButtonStateN = digitalRead(TARE_BUTTON_PIN);
  tareButtonFlank = tareButtonStateN - tareButtonStateN_1;

  //if (tareButtonStateN)
  //  DPRINTLN("Tare button pushed! ");

  switch (tareButtonFlank){
    case   1: DPRINTLN("Up flank!");
              tStartTareButton = millis();
              return -1;
              break;
    case  -1: DPRINTLN("Down flank!");
              tEndTareButton = millis();
              // Serial.print("Time:");
              // Serial.println(tEndTareButton-tStartTareButton);
              return (tEndTareButton-tStartTareButton);
              break;
    default:  return -1;
              break;
  }
}

void tareButtonFunction() // TODO: verify if this is ok (and/or used)
{
  int Push_button_state = digitalRead(PIN_PUSH_BUTTON);
  if ( Push_button_state == HIGH )
  {
    myTare();
  }
}

void tareButtonAction()
{
  int timeButton = updateTareButton();
  if (timeButton>200){
    Serial.print("Time tare button: ");
    Serial.println(timeButton);
    myTare();
  }
}
//************************//
//*   DISPLAY FUNCTIONS  *//
//************************//
void displayImage( const unsigned char * u8g_image_bits){
   u8g.firstPage();
   do {
       u8g.drawXBMP( 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, u8g_image_bits);
   } while( u8g.nextPage() );

   // Let us see that baby face ! :)
   delay(4000);
}

void poweredByDisplay(void){
  Serial.println("");
  Serial.println("Displaying Powered by ...");

 // Set up the display
  u8g.firstPage();
  u8g.setFont(u8g2_font_osb18_tf);

  do {
      u8g.setFont(u8g_font_6x10);
      u8g.drawStr( 30, 15, "Powered by");
      u8g.setFont(u8g2_font_osb18_tf);
      u8g.drawStr( 10, 40, "Humanity");
      u8g.setFont(u8g2_font_osb18_tf);
      u8g.drawStr( 40, 60, "Lab");
  }while(u8g.nextPage());

  Serial.println("Waiting 1 sec...");
  delay(1000);
  Serial.println("");
}

void sponsorsDisplay(void){
  Serial.println("");
  Serial.println("Displaying Sponsors..");

  // Set up the display
    u8g.firstPage();
    u8g.setFont(u8g2_font_osb18_tf);

  // Display loop
   do {
      u8g.setFont(u8g2_font_osb18_tf);
      u8g.drawStr( 15, 25, "AIRBUS");

      u8g.setFont(u8g_font_6x10);
      u8g.drawStr( 60, 35, "&");

      u8g.setFont(u8g2_font_osb18_tf);
      u8g.drawStr( 15, 55, "MEDAIR");
   }while(u8g.nextPage());

  Serial.println("Waiting 1 sec...");
  delay(1000);
  Serial.println("");

}

void clockShoot(int currentTime, int finalTime) {

  int X0 = DISPLAY_WIDTH/2;
  int Y0 = DISPLAY_HEIGHT/2;
  int R = int(DISPLAY_HEIGHT*0.5*0.5);
  int GAP = R*0.25;

  float N = float(currentTime)/float(finalTime);
  int X = X0 + int((R-GAP)*sin(N*2*3.1416)) ;
  int Y = Y0 - int((R-GAP)*cos(N*2*3.1416)) ;

  u8g.drawCircle(X0, Y0, R);
  u8g.drawCircle(X0, Y0, R-2);
  u8g.drawLine(X0, Y0, X, Y);

  u8g.setCursor(15, 55) ;
  u8g.print(int((finalTime-currentTime)/1000)+1, 1);

}


//***************************//
//*  INACTIVITY FUNCTIONS   *//
//***************************//

void inactivityCheck() {

  // Checking activity
    if (abs(displayFinalValue - displayFinalValue_1 ) > INACTIVE_THR ){
          Serial.print("Active: ");Serial.print(bInactive);
          lastTimeActivity =  millis();
          if (bInactive){
            // Waking the Arduino up


            // Waking the screen up
            u8g.sleepOff();
            bInactive =  false;
            }
    }

  // Checking inactivity
    if ((millis() - lastTimeActivity) > MAX_INACTIVITY_TIME){
          Serial.println("Inactive");
          bInactive = true;

          // Putting the screen in sleep mode
          u8g.sleepOn();
          // esp_deep_sleep_start();

          // Putting the Arduino in sleep mode
          /*set_sleep_mode(SLEEP_MODE_PWR_DOWN);
          sleep_enable();
          sleep_cpu();*/
    }


}

void wakeUp(void){
  bInactive = false;
  lastTimeActivity =  millis();
  u8g.sleepOff();
}

//************************//
//* GYRO/ACCEL FUNCTIONS *//
//************************//

void setupMPU(){

    Wire.begin();

 // Waking up the chip
    Wire.beginTransmission(MPU_ADDR); // Begins a transmission to the I2C slave (GY-521 board)
    Wire.write(0x6B); // PWR_MGMT_1 register (Power management)
    Wire.write(0b00000000); // set to zero (wakes up the MPU-6050)
    Wire.endTransmission(true);

  // Setting up the FS of the gyroscope (+-200 deg/s)
    Wire.beginTransmission(MPU_ADDR); // Begins a transmission to the I2C slave (GY-521 board)
    Wire.write(0x1B); // PWR_MGMT_1 register (Power management)
    Wire.write(0b00000000); // set to zero (wakes up the MPU-6050)
    Wire.endTransmission(true);

  // Setting up the FS of the accelerometer (+-2 g)
    Wire.beginTransmission(MPU_ADDR); // Begins a transmission to the I2C slave (GY-521 board)
    Wire.write(0x1B); // PWR_MGMT_1 register (Power management)
    Wire.write(0b00000000); // set to zero (wakes up the MPU-6050)
    Wire.endTransmission(true);
}

void readTemp(){
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x41);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU_ADDR,2,true);

    Tmp = Wire.read()<<8 | Wire.read(); // reading registers: 0x41 and 0x42
    myTmp = Tmp/340.00+36.53;
}

void readAccel(){

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR,14,true);

  Ax =  Wire.read()<<8 | Wire.read(); // reading registers: 0x3B and 0x3C
  Ay =  Wire.read()<<8 | Wire.read(); // reading registers: 0x3D and 0x3E
  Az =  Wire.read()<<8 | Wire.read(); // reading registers: 0x3F and 0x40
  Tmp = Wire.read()<<8 | Wire.read(); // reading registers: 0x41 and 0x42
  Gx =  Wire.read()<<8 | Wire.read(); // reading registers: 0x43 and 0x44
  Gy =  Wire.read()<<8 | Wire.read(); // reading registers: 0x45 and 0x46
  Gz =  Wire.read()<<8 | Wire.read(); // reading registers: 0x47 and 0x48

  myAx = float(Ax)/16384;
  myAy = -1*float(Az)/16384;
  myAz = float(Ay)/16384;
  myGyroX = float(Gx)/131;
  myGyroY = float(Gy)/131;
  myGyroZ = float(Gz)/131;

  myTmp = Tmp/340.00+36.53;

}

//************************//
//* GYRO/ACCEL FUNCTIONS *//
//************************//

void angleCalc(){
    readAccel();
    phideg = (180/pi)*atan2(myAy,myAz);
    thetadeg =   (180/pi)*atan2(-1*myAx, sqrt(pow(myAz,2) + pow(myAy,2)));
}

void angleAdjustment(){
  if (B_ANGLE_ADJUSTMENT){
    angleCalc();
    realValue_WU_AngleAdj = relativeVal_WU/(1+calib_theta_2*pow(thetadeg, 2));
  }
  else{
    realValue_WU_AngleAdj = relativeVal_WU;
  }
}


//**************************//
//* HTTP REQUEST FUNCTIONS *//
//**************************//

void hashJSON(char* data, unsigned len) {
  // Hash encryption of the JSON
  sha.resetHMAC(key, sizeof(key));
  sha.update(data, len);
  sha.finalizeHMAC(key, sizeof(key), hmacResult, sizeof(hmacResult));
  Serial.println("DataSize :" + String(len));
  Serial.println(data);
  Serial.println("HashSize :" + String(sha.hashSize( )));
  Serial.println("BlockSize:" + String(sha.blockSize()));
  sha.clear();

}

String buildJson(){

  Serial.println("IP ADDRESS");
  Serial.println(getIp());

  dataItem["tBeforeMeasure"     ]     = tBeforeMeasure;
  dataItem["tAfterMeasure"     ]      = tAfterMeasure;
  dataItem["IPadress" ]               = getIp();
  dataItem["realValueFiltered"     ]  = realValueFiltered;
  dataItem["correctedValueFiltered"]  = correctedValueFiltered;
  dataItem["bSync"     ]              = bSync;
  dataItem["bSync_corr"]              = bSync_corr;
  dataItem["calibration_factor"     ] = calibration_factor;
  dataItem["offset"]                  = offset;
  dataItem["realValue_WU"     ]       = realValue_WU;
  dataItem["bInactive"]               = bInactive;
  dataItem["lastTimeActivity"     ]   = lastTimeActivity;
  dataItem["myAx"]                    = myAx;
  dataItem["myAy"]                    = myAy;
  dataItem["myAz"]                    = myAz;
  dataItem["thetadeg"]                = thetadeg;
  dataItem["phideg"]                  = phideg;
  dataItem["myTmp"]                   = myTmp;
  dataItem["TEMPREF"]                 = TEMPREF;
  dataItem["MODEL"]                   = MODEL;

  char name[] = "Wooby";
  // strcat(ARDUINO_BOARD,name);
  dataItem["ThisBoard"] =  name; //<char*>

  // Put the json in a string
  String jsonStr;
  serializeJson(dataItem, jsonStr);
  // Put the json string in char array
  char chBuf[jsonStr.length()];
  jsonStr.toCharArray(chBuf, jsonStr.length()+1);

  DPRINTLN("jsonStr :" + String(jsonStr));
  DPRINTLN("chBuf   :" + String(chBuf  ));
  DPRINTLN("jsonStr length:" + String(jsonStr.length()));
  DPRINTLN("chBuf   length:" + String(sizeof(chBuf   )));

  // Hashing JSON ??
  hashJSON(chBuf, sizeof(chBuf));
  rbase64.encode((char*)hmacResult);

  String resultHash = String(rbase64.result());
  DPRINTLN("Hash String: ");
  DPRINTLN(resultHash);

  String completeJSON = "{\"HMACRes\":\"" + resultHash + "\",\"Data\":" + jsonStr + "}";
  String payLoad      = "tag=DataESP&value=" + completeJSON;

  DPRINTLN("payload: ");
  DPRINTLN(payLoad);

  return payLoad;

}

//***************************//
//* COMMUNICATION FUNCTIONS *//
//***************************//

bool sendJson()
{


  String payLoad = buildJson();

  // Connexion to the host
  DPRINTLN("Connecting to ");
  DPRINTLN(host);


  if (!clientForGoogle.connect(host, port)){
    ERRORPRINTLN("Connection failed.");
    return false;
  }

  try{
  String postRequest =   "POST "  + String(uri)  + " HTTP/1.1\r\n" +
                         "Host: " + String(host) + "\r\n" +
                         "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n" +
                     //  "Content-Type: application/json; utf-8\r\n" +
                         "Content-Length: " + payLoad.length() + "\r\n" + "\r\n" + payLoad;

  DPRINTLN("Post request: ");
  DPRINTLN(postRequest);

  // Sending the final string
  clientForGoogle.print(postRequest);
  clientForGoogle.stop();
  return true;
}
catch(int e){
  ERRORPRINT("Post request not succcessful");
  ERRORPRINT(e);
  return false;
}

}

bool sendDataToGoogle()
{
  // Verify the activation
  if (!B_HTTPREQ){
    return false;
  }

  if (countForGoogleSend == N_GSHEETS){
    sendJson();
    countForGoogleSend = 0;
  }
  else{
    countForGoogleSend++;
  }
}

void printSerial()
{
  Serial.print("WS");

  Serial.print("tBeforeMeasure");               Serial.print(":");
  Serial.print(tBeforeMeasure);                 Serial.print(",\t");
  Serial.print("tAfterMeasure");                Serial.print(":");
  Serial.print(tAfterMeasure);                  Serial.print(",\t");
  Serial.print("tAfterAlgo");                   Serial.print(":");
  Serial.print(tAfterAlgo);                     Serial.print(",\t");


  Serial.print("realValue_WU");                 Serial.print(":");
  Serial.print(realValue_WU);                   Serial.print(",\t");
  Serial.print("OFFSET");                       Serial.print(":");
  Serial.print(offset);                         Serial.print(",\t");

  Serial.print("relativeVal_WU");               Serial.print(":");
  Serial.print(relativeVal_WU);                 Serial.print(",\t");
  Serial.print("realValue_WU_AngleAdj");        Serial.print(":");
  Serial.print(realValue_WU_AngleAdj);          Serial.print(",\t");
  Serial.print("realValue");                    Serial.print(":");
  Serial.print(realValue, 5);                   Serial.print(",\t");
  Serial.print("realValueFiltered");            Serial.print(":");
  Serial.print(realValueFiltered, 5);           Serial.print(",\t");
  Serial.print("correctedValueFiltered");       Serial.print(":");
  Serial.print(correctedValueFiltered, 5);      Serial.print(",\t");


  Serial.print("thetadeg");                     Serial.print(":");
  Serial.print(thetadeg);                       Serial.print(",\t");
  Serial.print("phideg");                       Serial.print(":");
  Serial.print(phideg);                         Serial.print(",\t");

  Serial.print("myTmp");                        Serial.print(":");
  Serial.print(myTmp);                          Serial.print(",\t");

  /*
  Serial.print("tareButtonFlank");              Serial.print(":");
  Serial.print(tareButtonFlank);                Serial.print(",\t");
  */



  Serial.println("");
}

void printSerialOld()
{

  Serial.print(tAfterAlgo);                     Serial.print(",\t");
  Serial.print(realValue, 4);                   Serial.print(",\t");
  Serial.print(correctedValue, 4);              Serial.print(",\t");
  Serial.print(tBeforeMeasure);                 Serial.print(",\t");
  Serial.print(tAfterMeasure);                  Serial.print(",\t");
  Serial.print(realValueFiltered, 4);           Serial.print(",\t");
  Serial.print(correctedValueFiltered, 4);      Serial.print(",\t");
  Serial.print(bSync);                          Serial.print(",\t");
  Serial.print(bSync_corr);                     Serial.print(",\t");
  Serial.print(calibration_factor,4);           Serial.print(",\t");
  //Serial.print(valLue_WU/realvalLue,4);Serial.print(",\t");
  Serial.print(offset, 4);                      Serial.print(",\t");
  Serial.print(realValue_WU);                   Serial.print(",\t");
  Serial.print(bInactive);                      Serial.print(",\t");
  Serial.print(lastTimeActivity);               Serial.print(",\t");
  Serial.print(myVcc);                          Serial.print(",\t");
  // Serial.print((float)scale.get_Vcc_offset(), 4);Serial.print(",\t");

  Serial.print(myAx);                           Serial.print(",\t");
  Serial.print(myAy);                           Serial.print(",\t");
  Serial.print(myAz);                           Serial.print(",\t");
  Serial.print(myGyroX);                        Serial.print(",\t");
  Serial.print(myGyroY);                        Serial.print(",\t");
  Serial.print(myGyroZ);                        Serial.print(",\t");

  Serial.print(thetadeg);                       Serial.print(",\t");
  Serial.print(phideg);                         Serial.print(",\t");

  Serial.print(myTmp);                          Serial.print(",\t");
  Serial.print(TEMPREF);                        Serial.print(",\t");
  Serial.print(tempCorrectionValue);            Serial.print(",\t");

  Serial.println("");
}



//*************************//
//*    MACRO FUNCTIONS    *//
//*************************//
void getWoobyWeight()
{

    // Raw weighting //
    tBeforeMeasure = millis();
    realValue_WU = scale.read_average(nMeasures);
    tAfterMeasure = millis();

    offset = (float)scale.get_offset();
    relativeVal_WU = realValue_WU-offset;

    // Angles correction //
    angleAdjustment();

    // Conversion to grams //
    realValue = (relativeVal_WU)/scale.get_scale() ;

    correctedValue = correctionAlgo(realValue); // NOT USED!!!!!

    // Filtering  //

      // Filtering for the real value //
      realValueFilterResult = filtering(realValue, realValue_1, realValueFiltered_1);
      bSync = realValueFilterResult.bSync;
      realValueFiltered = realValueFilterResult.yk;

      // Updating for filtering
      realValueFiltered_1 = realValueFiltered;
      realValue_1 = realValue;

    // Final correction  //
    correctedValueFiltered = correctionAlgo(realValueFiltered);

    tAfterAlgo = millis();
}

void mainDisplayWooby()
{
  // Set up the display
  u8g.firstPage();

  if (correctedValueFiltered > MAX_GR_VALUE) { // Verification of the max value in grams
      do {
            u8g.setCursor(15, 15) ;
            u8g.print(" OVER");
            u8g.setCursor(15, 40) ;
            u8g.print("FLOW !");
        } while(u8g.nextPage());

  }
  else if ((correctedValueFiltered < -1*MIN_GR_VALUE)  && (B_INHIB_NEG_VALS)){ // Verification of the negative values (with threshold)
       do {
            u8g.setFont(u8g2_font_osb18_tf);
            u8g.setCursor(17, 25) ; // (Horiz, Vert)
            u8g.print(" TARE !");

            u8g.setFont(u8g_font_6x10);
            u8g.setCursor(23, 45) ; // (Horiz, Vert)
            u8g.print("Negative values");

        } while(u8g.nextPage());
  }
  else if ((abs(thetadeg) > MAX_THETA_VALUE || abs(phideg) > MAX_PHI_VALUE ) && (B_LIMITEDANGLES)){ // Verification of the maximum allowed angles
       do {
            u8g.setFont(u8g2_font_osb18_tf);
            u8g.setCursor(17, 30) ; // (Horiz, Vert)
            u8g.print(" OUPS !");

            u8g.setFont(u8g_font_6x10);
            u8g.setCursor(23, 45) ; // (Horiz, Vert)
            u8g.print("Wooby NOT flat");

        } while(u8g.nextPage());
  }
  else{
    // Everything is ok!!  So let's show the measurement
    do {
        u8g.setFont(u8g2_font_osb18_tf);
        u8g.setFontPosTop();
        itoa(displayFinalValue, arrayMeasure, 10);
        u8g.setCursor(DISPLAY_WIDTH/2-u8g.getStrWidth(arrayMeasure)/2, 10) ;
        u8g.print((displayFinalValue), 0);


        u8g.setFont(u8g2_font_osb18_tf);
        u8g.setFontPosTop();
        u8g.setCursor(30, 25);
        u8g.print("grams");


       if (TYPE==0){
          // Display trust region //
          u8g.setFont(u8g_font_6x10);
          u8g.setFontPosBottom();
          u8g.setCursor(5, DISPLAY_HEIGHT-2);
          u8g.print(int(thetadeg), 10);


          u8g.setFont(u8g_font_6x10);
          u8g.setFontPosBottom();
          u8g.setCursor(55, DISPLAY_HEIGHT-2);

          //sprintf(aux, (PGM_P)F("%d %d"), 6, int(TEMPREF));
          u8g.print(String(int(myTmp)) + "("+ String(int(TEMPREF)) + ")");

          u8g.setFont(u8g_font_6x10);
          u8g.setFontPosBottom();
          u8g.setCursor(100, DISPLAY_HEIGHT-2);
          u8g.print(int(phideg), 10);
        }

        // Display batterie levels //

          u8g.setFont(u8g_font_6x10);
          u8g.setFontPosTop();
          u8g.setCursor(100, 12) ; // (Horiz, Vert)
          u8g.print(int(100*ratioVCCMAX));


          u8g.setFont(u8g_font_6x10);
          u8g.setFontPosTop();
          u8g.setCursor(120, 12);
          u8g.print("%");

          u8g.drawLine(100,   2,    100+22, 2); // (Horiz, Vert)
          u8g.drawLine(100,   2+7,  100+22, 2+7);
          u8g.drawLine(100,   2,    100,    2+7);
          u8g.drawLine(100+22,2,    100+22, 2+7);

          u8g.drawBox(100+22+1,2+2,2, 7-4+1);
          u8g.drawBox(100+2,2+2,int((22-4+1)*ratioVCCMAX),7-4+1); // (Horiz, Vert, Width, Height)

    } while(u8g.nextPage());
  }

  if (TYPE>0){
    inactivityCheck();
    if (bInactive){
    Serial.println("Display set to inactive"); // TODO:Change for DEBUG!
    }
  }
}

//************************//
//*       SET UP        *//
//************************//
void setup(void) {

  Serial.begin(9600);
  Serial.println("Wooby!! ");
  Serial.println("Initializing to measure tons of smiles");


  //*       SET UP  WEIGHT SENSOR       *//

    scale.begin(DOUT, CLK);
    scale.set_gain(gain);
    scale.set_scale(calibration_factor);


  //*         ACCELEROMETER           *//
    setupMPU();
    readAccel();
    //readTemp();

  //*            AUTO TARE             *//

    myTare();
    initTareButton();

  //*            FILTERING            *//

    realValue_1 = 0;
    realValueFiltered = 0;
    realValueFiltered_1 = 0;
    bSync = false;

  //*          SCREEN SETUP          *//
      // Flip screen, if required
      // For Arduino:
        //u8g.setRot180();
      // For ESP32
    try{
        u8g.begin();
        Serial.println("Flipping the screen ");
        u8g.setFlipMode(1);


    // Set up of the font
      u8g.setFont(u8g2_font_6x10_tf);
      // u8g.setFont(u8g_font_9x18);
      // u8g.setFont(u8g2_font_osb18_tf);

   // Other display set-ups
      u8g.setFontRefHeightExtendedText();
      // For Arduino: u8g.setDefaultForegroundColor();
      u8g.setFontPosTop();
    }
    catch(int e){
      Serial.println("Not possible to activate the display");
    }

  //*          INACTIVITY          *//

    // For Arduino: Serial.print("CLOCK DIVISION:"); Serial.println(CLKPR);
    lastTimeActivity =  millis();
    pinMode(interruptPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(interruptPin), wakeUp, CHANGE);

  //*          VCC MANAGEMENT        *//
    // For Arduino:
    // myVcc = vcc.Read_Volts();
    // VccFilter.init(myVcc);

  //*         TARE BUTTON      *//
    pinMode(PIN_PUSH_BUTTON, INPUT);


   if (B_HTTPREQ){
  //*       GOOGLE CONNECTION        *//
      clientForGoogle.setCACert(root_ca);
  //*         WIFI CONNECTION        *//
      setupWiFi();
      // Checking WiFi connection //
      BF_WIFI = !checkWiFiConnection();
    }

}

//************************//
//*        LOOP         *//
//************************//

void loop(void) {

 //  Vcc measurement // TODO!  Create a function !
    /*
    myVcc = vcc.Read_Volts();
    // int deltaOFFSETVcc = correctionVcc(myVcc);
    myVccFiltered = VccFilter.update(myVcc);

    ratioVCCMAX = _min(myVccFiltered/VCCMAX, 1.0);
    // For Arduino: ratioVCCMAX = min(myVccFiltered/VCCMAX, 1.0);
    */

  //*  READING OF SERIAL ENTRIES   *//
    if(Serial.available())
    {
      char temp = Serial.read();
      switch(temp){
      case '+': calibration_factor += 0.01;
                scale.set_scale(calibration_factor);
                break;
      case '-': calibration_factor -= 0.01;
                scale.set_scale(calibration_factor);
                break;
      case 't': myTare();
                break;
      default:
                break;
      }
    }

  //*         MAIN SWITCH          *//

  switch (state) {
    case 0:
            displayImage(logoWooby);
            state++;
    break;
    case 1:
            // sponsorsDisplay();
            /* for (int i=0; i<10; i++){
              clockShoot(i, 10);
              delay(1000);
            }
            */
            state++;
    break;

    case 2:
            // Displaying and weighting is about to begin
            Serial.println("DATA START");
            state++;

    break;
    case 3:
    {
            // Display characteristics
              // u8g.setFont(u8g_font_9x18);
              // u8g.setFont(u8g2_font_osb18_tf);
              // u8g.setFontPosTop();

            // Main display loop

            // Tare button //
            tareButtonAction();

            // Weighting //
            getWoobyWeight();

            // GyroAcc adjustement  //

            // Temperature algorithm // TODO: create a function
            tempCorrectionValue_WU = (P1*(myTmp-TEMPREF)+P0); //
            tempCorrectionValue = (tempCorrectionValue_WU)/scale.get_scale();

            // Serial monitor outputs  //
            printSerial();

            // Google sheet data Sending  //
            sendDataToGoogle();

            // Updating for inactivity check
            displayFinalValue_1 = displayFinalValue;
            displayFinalValue   = correctedValueFiltered;

            //     Displaying     //
            mainDisplayWooby();

      break;
    }
    default:
    {
      Serial.println("State is not valid");
      break;
    }
  }

  }
