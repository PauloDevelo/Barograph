/*
  barograph.ino

  This arduino sketch is the development of a barograph for sailor who cares about the atmospheric pressure history
  and its trend.
  This barograph alerts of quick pressure changement that often appears when a ridge or a low is getting closer.
  You can acknowledge an alarm with the unique button.
  The history of the pressure can be visualized at different time scales. You can change it with the unique button.

  You can always find the latest version of the program on our website at
  http://www.ecogium.fr/arbutus/barograph/

  If you make any modifications or improvements to the code, I would
  appreciate that you share the code with me so that I might include
  it in the next release. I can be contacted through
  http://www.ecogium.fr/arbutus/contact.php.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GPL 3.0 license.
  Please see the included documents for further information.

  The license applies to barograph.ino and the documentation.
*/

#include <Adafruit_BMP085.h>


#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <TFT.h>// Arduino LCD library

//Taille d'un enregistrement en octet
#define SIZEOFRECORD 4


 // pin definition for the Uno
#define tft_cs 10
#define sd_cs 4
#define dc   9
#define rst  8  
#define BUZ 7

#define FREQBUZ 2500

#define PREF_PRESSURE_MIN 98000
#define PREF_PRESSURE_MAX 102500
#define NORM_PRESSURE 101325

//Nbre de pixel de l'écran
#define W_SCR 160
#define H_SCR 128

//Hauteur des caractère en pixel
#define HEIGHT_CHAR 7

const char* HISTOFILENAME = "H";

//Ecran
TFT TFTscreen = TFT(tft_cs, dc, rst);

//Capteur de pression et de temperature
Adafruit_BMP085 bmp;

unsigned long lastMeasurementTime = 0;
unsigned long lastRefreshDisplay = 0;
byte heightsToDisplay[W_SCR];

//Tendance de la pression sur 1 et 3 heures
int32_t pressure = 0;
int32_t min_pressure = PREF_PRESSURE_MIN;
int32_t max_pressure = PREF_PRESSURE_MAX;
byte norm_pressure_i = 0;

//Liste des échelles en nombre de seconde par pixel horizontal
const char scales[] PROGMEM = {3, 6, 12, 24, 48, 72, 96, 120}; //Correspond à la visualisation de 3H, 6H, 12H, 24H, 48H, 72H}
volatile byte iscale = 0;

unsigned long lastAlarm = 0;
volatile bool isAlarmOn = false;
volatile bool isAlarmAck = false;

volatile unsigned long lastButtonAction = 0;
volatile boolean isScaleChanged = false;

int32_t readPressure(File& historicFile){
  int32_t pressure;
  byte* tabBytePressure = (byte*)(&pressure);
  for(int j = 0; j < 4; j++)
      tabBytePressure[j] = historicFile.read();

    return pressure;
}

void drawHDotLine(byte j, byte r, byte g, byte b){
  TFTscreen.stroke(r, g, b);
  for(int i = 0; i < W_SCR; i++){
    if(i%3 == 0){
      TFTscreen.point(i, j);
    }
  }
}

void displayTemperature(int temperature){
  TFTscreen.stroke(0,0,0);

  for(int k = 130; k < 155; k++){
    TFTscreen.line(k, 120, k, 120 + HEIGHT_CHAR);
  }

  String myString = String(temperature) + String(F("'C"));
  char tempStr[6];
  myString.toCharArray(tempStr, 6);
  TFTscreen.stroke(255,0,0);
  TFTscreen.text(tempStr, 130, 120);
}

void displayPressure(int32_t pressureval, byte res, byte x, byte y, byte r, byte g, byte b){
  TFTscreen.stroke(0,0,0);
  byte lgth;
  if(res == 0 || res == 1){
    lgth = 48;
  }
  else if(res == 2){
    lgth = 42;
  }
  else{
    lgth = 48;
    res = 0;
  }
  
  for(int k = x; k < x + lgth; k++){
    TFTscreen.line(k, y, k, y + HEIGHT_CHAR);
  }
  
  int32_t resAbs = 1;
  for(int i = 0; i < res; i++){
    resAbs = resAbs * 10;
  }
  
  pressureval = (int32_t)(pressureval + resAbs/2)/resAbs;
  String myString = String(pressureval);
  
  
  String unit = String(F("Pa"));;
  if(res == 1){
    unit = String(F("dPa"));
  }
  else if(res == 2){
    unit = String(F("hPa"));
  }
  
  //myString += String(unit);
  myString += unit;
  char pressureStr[10];
  myString.toCharArray(pressureStr, 10);
  TFTscreen.stroke(r,g,b);
  TFTscreen.text(pressureStr, x, y);
}

void displayEmptyGraph(){
  {
    byte norm_pressure_i_temp = (byte)(((long)NORM_PRESSURE - (long)min_pressure) * (long)(H_SCR - 10 - 10) /((long)max_pressure - (long)min_pressure));
    norm_pressure_i_temp = H_SCR - 10 - 10 - norm_pressure_i_temp;
    if(norm_pressure_i_temp != norm_pressure_i){
      drawHDotLine(10 + norm_pressure_i, 0, 0, 0);
      norm_pressure_i = norm_pressure_i_temp;
    }
    drawHDotLine(10 + norm_pressure_i, 0, 0, 255);
  }
  
  for(int i = 0; i < 4; i++){
    drawHDotLine(10 + i * 36, 127, 127, 127);
    displayPressure(max_pressure - i * (max_pressure - min_pressure)/(long)3, 2, 0, 1 + i * 36, 127, 127, 127);
  }
}

void updateDataToDisplay(){
  
  displayEmptyGraph();
  //Nombre de seconde entre 2 mesures à afficher
  byte scale = (byte)pgm_read_word_near(scales + iscale);
  
  File historicFile = SD.open(HISTOFILENAME, FILE_WRITE);
  if(!historicFile){
    return;
  }
  
  uint32_t fileSize = historicFile.size();
  long pos = (long)fileSize - (long)scale * (long)3600 * (long)SIZEOFRECORD;
  long increment = ((long)scale * (long)3600 + (long)W_SCR/(long)2) / (long)W_SCR;
  increment = increment * (long)SIZEOFRECORD;
 
  int i = 0;
  int32_t temp_min_pressure = PREF_PRESSURE_MIN;
  int32_t temp_max_pressure = PREF_PRESSURE_MAX;
  
  while(i < W_SCR && pos < (long)fileSize){ 
    //Si il y a de l'historique
    if(pos >= 0){
      historicFile.seek(pos);
      
      long pressureValue = readPressure(historicFile);

      if(temp_min_pressure > pressureValue){
        temp_min_pressure = pressureValue;
      }
      if(temp_max_pressure < pressureValue){
        temp_max_pressure = pressureValue;
      }
      
      TFTscreen.stroke(0,0,0);
      TFTscreen.point(i, H_SCR-heightsToDisplay[i]);
      
      heightsToDisplay[i] = map(pressureValue, min_pressure, max_pressure, 10, H_SCR - 10);
      
      TFTscreen.stroke(0,255,0);
      TFTscreen.point(i, H_SCR-heightsToDisplay[i]);
      
      i++;
    }
    //Autrement
    else{
      if(heightsToDisplay[i] != 0){
        TFTscreen.stroke(0,0,0);
        TFTscreen.point(i, H_SCR - heightsToDisplay[i]);
      }
      
      heightsToDisplay[i++] = 0;
    }
    pos += increment;
  }
  historicFile.close();
  
  //Les nouvelles valeurs maximum seront prises en compte lors du prochain rafraichissement.
  max_pressure = temp_max_pressure;
  min_pressure = temp_min_pressure;
}



void buttonActionPerformed(){
  //Avant d'executer l'action du bouton, on filtre les rebonds
  if(millis() - lastButtonAction > 300){
    lastButtonAction = millis();
    if(isAlarmOn && !isAlarmAck){
      noTone(BUZ);
      isAlarmOn = false;
      isAlarmAck = true;
    }
    else{
      iscale++;
      if(iscale == 8){
        iscale = 0;
      }
  
      isScaleChanged = true;
    }
  }
}

void displayText(String msg, int pxlg, int i, int j, byte r, byte g, byte b){
  TFTscreen.stroke(0,0,0);
  for(int k = i; k < i + pxlg; k++){
    TFTscreen.line(k, j, k, j + HEIGHT_CHAR);
  }
  
  TFTscreen.stroke(r,g,b);
  
  char txtmsg[11];
  msg.toCharArray(txtmsg, 11);
  
  TFTscreen.text(txtmsg, i, j);
}

void displayDisplayedPeriod(){
  byte period = (byte)pgm_read_word_near(scales + iscale);
  String periodTxt = String(period);
  periodTxt += F("h");
  displayText(periodTxt, 24, 45, 120, 127, 127, 127);
}

void updatePressure(){
  //Mise à jour de la pression
  long pressureTemp = bmp.readSealevelPressure((float)2);
  long var;
  if(pressureTemp > pressure){
    var = pressureTemp - pressure;
  }
  else{
    var = pressure - pressureTemp;
  }
  
  //Cela permet de limiter du bruit observé lors du fonctionnement
  if(var < (long)20){
    pressure = pressureTemp;
  }
  
  displayPressure(pressure, 1, 75, 120, 255, 0, 0);
}

void updateTemperature(){
  int temperature = (int)(bmp.readTemperature() + (float)0.5);
  displayTemperature(temperature);
}

void appendPressureInHistoric(){
  File historicFile = SD.open(HISTOFILENAME, FILE_WRITE);
  if(!historicFile){
    return;
  }
  
  historicFile.write((byte*)(&pressure), 4);
  historicFile.close();
}


void setup () {
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);
  
  //Serial.begin(9600);
  
  if(!SD.begin(sd_cs)){
    //Serial.println("Erreur 1");
    return;
  }

  if(!SD.exists(HISTOFILENAME)){
    File historicFile = SD.open(HISTOFILENAME, FILE_WRITE);
    historicFile.close();
  }

  
  bmp.begin(BMP085_STANDARD);
  // initialize the display
  TFTscreen.begin();
  pinMode(BUZ, OUTPUT);
  
  pressure = bmp.readSealevelPressure((float)2);
  
  // clear the screen with a pretty color
  TFTscreen.background(0,0,0); 
  updateDataToDisplay();
  displayDisplayedPeriod();
  
  //Bouton géré par interruption
  attachInterrupt(0, buttonActionPerformed, RISING);
}

void computePressureTrend(){
  File historicFile = SD.open(HISTOFILENAME, FILE_WRITE);
  if(!historicFile){
    //Serial.println("1");
    return;
  }
  
  long fileSize = historicFile.size();
  long pos = fileSize - (long)10800* (long)SIZEOFRECORD;
  long pressure3HValue = -1;
  if(pos >= 0){
    historicFile.seek(pos);
    pressure3HValue = readPressure(historicFile);
  }
  
  pos = fileSize - (long)3600 * (long)SIZEOFRECORD;
  long pressure1HValue = -1;
  if(pos >= 0){
    historicFile.seek(pos);
    pressure1HValue = readPressure(historicFile);
  }

  historicFile.close();
  
  String oneHTrendStr = F("Na Pa/h");
  int oneHourTrend = 0;
  if(pressure1HValue != -1){
    oneHourTrend = (int)(pressure - pressure1HValue);
    oneHTrendStr = String(oneHourTrend);
    oneHTrendStr += String(F("Pa/h"));
  }
  displayText(oneHTrendStr, 54, 50, 0, 255, 0, 0);
  
  String threeHTrendStr = F("Na Pa/3h");
  if(pressure3HValue != -1){
    int threeHourTrend = (int)(pressure - pressure3HValue);
    threeHTrendStr = String(threeHourTrend);
    threeHTrendStr += String(F("Pa/3h"));
  }
  displayText(threeHTrendStr, 54, 105, 0, 255, 0, 0);
  
  unsigned long gaplastAlarm = millis() - lastAlarm;

  //Si la tendante sur 1 heure est inférieur à -140, et que  l'alarme n'est pas déjà on et qu'une alarme n'a pas déjà été déclanché dans les 3h
  if((oneHourTrend < (int)-140 || oneHourTrend > 140) && !isAlarmOn && gaplastAlarm > (unsigned long)10800000){
    //On déclenche l'alarme
    tone(BUZ, FREQBUZ);
    isAlarmOn = true;
    lastAlarm = millis();

    //On précise que l'alarme n'a pas été acquittée.
    isAlarmAck = false;
  }
}



void loop () {
  
  unsigned long gap = millis() - lastMeasurementTime;
  //Mise à jour de la pression chaque seconde
  if(gap >= 1000){
    lastMeasurementTime += gap;
    
    updatePressure();
    updateTemperature();
    computePressureTrend();
    appendPressureInHistoric();
  }
  
  //Mise à jour du graphe chaque minute
  gap = millis() - lastRefreshDisplay;
  if(gap > 60000){
    lastRefreshDisplay += gap;
    updateDataToDisplay();
  }
  
  //Si un changement d'échelle a eu lieu, on fait le rafraichissement de l'écran
  //Dans le thread principal (si on peut parler de thread ...)
  if(isScaleChanged){
    isScaleChanged = false;
    updateDataToDisplay();
    displayDisplayedPeriod();
  }
}







