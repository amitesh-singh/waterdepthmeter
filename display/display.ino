/*
  * Water Depth Meter
  * Copyright (C) 2018  Amitesh Singh <singh.amitesh@gmail.com>
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

#include <Ticker.h>
#include "espnowhelper.h"
#include "wifi.h"
#include "down.h"
#include "dish.h"

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library
#include <SPI.h>

//Comment out this code in production.
#define DEBUG

#define TFT_CS     4
// Esp8266 RST is connected to TFT's RST
#define TFT_RST    0
#define TFT_DC     5

#define TFT_SCREEN_TIMEOUT 120

#define WIFI_CHANNEL 1
//30s
#define SLAVE_CONNECTION_TIMEOUT_LIMIT 60*1000

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS,  TFT_DC, TFT_RST);

static void testdrawtext(char *text, uint16_t color)
{
  tft.setCursor(0, 0);
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.print(text);
}


static const uint8_t buttonPin = 0; // GPIO0
static const uint8_t lcdPowerPin = 16; // GPIO16
static const uint8_t slaves_count = 1;

static espnow espmaster;

static uint8_t remoteMac[slaves_count][6] = {
// mac address of bare esp12e board.
 {0x18, 0xFE, 0x34, 0xE2, 0x0C, 0xAB},
//Nodemcu mac
//{ 0x18, 0xFE, 0x34, 0xE2, 0x16, 0x64},
/*
{ 0x18, 0xFE, 0x34, 0xE2, 0x16, 0x64},
{ 0x18, 0xFE, 0x34, 0xE2, 0x16, 0x64},
*/

};

struct __attribute__((__packed__)) waterinfo
 {
     uint8_t sensorid;
     long distance;
     uint8_t percentage;
     uint32_t batteryVoltage;
 };

struct __attribute__((__packed__)) ack
{
    uint8_t sensorid;
};

static volatile ack acks[slaves_count];
static volatile bool reply[slaves_count];
static volatile waterinfo wi[slaves_count];
static volatile unsigned long timestamps[slaves_count];

static bool displayStatus = false;
//Seems like the pipe is attached  at 23cm from Bottom,
// No water in Tap at 23cm from bottom
// gonna use 100cm as tankHeight
const static u8 tankHeight = 123; //in cm
const static u8 tankMinWaterHeight = 100; //in cm
//blink led faster whenever water level is higher than this.
const static u8 tankWaterLevelThresholdPercent = 82;

static uint16_t tankColor = ST7735_RED;

static volatile bool displayOn = true;
Ticker timeout;
Ticker redLedBlinkTimer;

const static u8 redLedPin =  2;//13;
static bool redLedBlinking;

static void _red_led_blink_cb()
{
    digitalWrite(redLedPin, !digitalRead(redLedPin));
}

static void displayBanner()
{
    //TODO: setRotation(1) is the correct one.
    // change it 
    tft.setTextSize(2);

    tft.println("\n");
    tft.setTextColor(ST7735_RED);
    tft.println(" Water Depth");
    tft.println("   Meter");
    tft.setTextSize(1);
    tft.setTextColor(ST7735_YELLOW);
    tft.println("         Version 0.1");
    tft.println("");
    tft.setTextColor(ST7735_WHITE);
    tft.println("  (C) Amitesh Singh 2018");

    delay(5000);
}

static void lcdInit()
{
    // We have ST7735 black tab TFT
    tft.initR(INITR_BLACKTAB);
    delay(500);
    tft.setRotation(3);
    tft.fillScreen(ST7735_BLACK);
    tft.setTextWrap(true);
}

static void lcdOn()
{
    digitalWrite(lcdPowerPin, HIGH);

    //not sure abt the delay yet.
    delay(200);

    lcdInit();
    displayOn = true;
}

static void lcdOff()
{
    tft.fillScreen(ST7735_BLACK);
    displayOn = false;
    digitalWrite(lcdPowerPin, LOW);
}

static void _timeout_cb()
{
    lcdOff();
    timeout.detach();

#ifdef DEBUG
    Serial.println("Timeout happened, TFT is off.");
#endif
}

static void drawReadings(volatile struct waterinfo &w)
{
   tft.fillScreen(ST7735_BLACK);

   tft.drawRoundRect(5, 5, 50, 105, 5, ST7735_WHITE);
   if (w.percentage >= 0 && w.percentage <= 25)
     {
        tankColor = ST7735_RED;
     }
   else if (w.percentage > 25 && w.percentage <= 50)
     {
        tankColor = ST7735_YELLOW;
     }
   else
     {
        tankColor = ST7735_GREEN;
     }

   if (w.percentage > 0)
     {
        tft.fillRoundRect(5, 110 - w.percentage, 50, w.percentage, 1, tankColor);
        tft.drawLine(60, 110 - w.percentage, 60, 110, tankColor);
     }

   tft.setCursor(64, 110 - w.percentage/2);
   tft.printf("%dcm", w.distance);

   //show the last reading
   tft.setTextSize(2);
   tft.setTextColor(tankColor);
   tft.setCursor(105, 60);
   tft.printf("%d%%", w.percentage);
   tft.setTextSize(1);
   tft.setCursor(60, 100);
   tft.setTextColor(ST7735_WHITE);

   //draw WiFi icon
   tft.drawXBitmap(140, 2, net_wifi4_bits, net_wifi4_width, net_wifi4_height, ST7735_CYAN);

   //show battery voltage
   tft.setCursor(75, 5);
   tft.drawXBitmap(63, 5, dish_bits, dish_width, dish_height, ST7735_YELLOW);

   tft.setTextColor(ST7735_MAGENTA);
   tft.printf("%.2fV", w.batteryVoltage/1000.0);

   tft.setCursor(0, 0);
   tft.setTextColor(ST7735_WHITE);
}

void setup()
{
#ifdef DEBUG
    Serial.begin(9600);
    Serial.println("................................");
    Serial.println("Master Display device controller");
#endif

    redLedBlinking = false;

    pinMode(redLedPin, OUTPUT);
    digitalWrite(redLedPin, HIGH);

    pinMode(lcdPowerPin, OUTPUT);
    digitalWrite(lcdPowerPin, HIGH);

#ifdef DEBUG
    Serial.println("Started LCD..");
#endif

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, LOW);

    // Display on/off btn
    pinMode(buttonPin, INPUT_PULLUP);

    delay(500);

    lcdInit();

    displayBanner();
    tft.fillScreen(ST7735_BLACK);

    testdrawtext("ESP-NOW initializing..\n", ST7735_WHITE);

    if (espmaster.init(WIFI_STA, ESP_NOW_ROLE_COMBO))
    {
        tft.setTextColor(ST7735_RED);
        tft.println("Failed to init ESP-NOW. Restarting...");
    #ifdef DEBUG
        Serial.println("Failed to init ESP-NOW. Restarting ESP8266");
    #endif

        ESP.restart();
    }
#ifdef DEBUG
    Serial.print("Mac Addr: "); Serial.println(WiFi.macAddress());
#endif

    tft.println("Mac Addr:");
    tft.setTextColor(ST7735_GREEN);
    tft.println(WiFi.macAddress());
    delay(5000);
    tft.setTextColor(ST7735_WHITE);
    tft.println("Adding Peers");
    tft.setTextColor(ST7735_GREEN);
    //Add all clients as peer
    for (uint8_t i = 0; i < slaves_count; ++i)
    {
        espmaster.addPeer(remoteMac[i], ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, nullptr, 0);
        reply[i] = false;
        acks[i].sensorid = i + 1;
        timestamps[i] = millis();
        for (u8 j = 0; j < 6; ++j)
            tft.printf("%X:", remoteMac[i][j]);
        tft.println("");
    }

    espmaster.addRecvCb([](uint8_t *macaddr, uint8_t *data, uint8_t len)
    {
        digitalWrite(BUILTIN_LED, LOW);
        //get the data
        waterinfo *w = (waterinfo *)data;
#ifdef DEBUG
        Serial.println("Recv_Cb");
        Serial.printf("-- d: %d, %d%%\r\n", w->distance, w->percentage);
#endif
        //ignore the timeout reading from HC-SR04 
        // and show the last  time reading which we had.
        if (w->distance == tankHeight)
            {
                reply[0] = true;
                wi[0].distance = tankMinWaterHeight - wi[0].distance;
                //wi[0].percentage = 100 - wi[0].percentage;
                timestamps[0] = millis();
                return;
            }

        if (w->sensorid == 1)
        {
            reply[0] = true;
            memcpy((void *)&wi[0], w, sizeof(waterinfo));
            timestamps[0] = millis();
        }
        else if (w->sensorid == 2)
        {
            reply[1] = true;
            memcpy((void *)&wi[1], w, sizeof(waterinfo));
            timestamps[1] = millis();
        }
        else if (w->sensorid == 3)
        {
            reply[2] = true;
            memcpy((void *)&wi[2], w, sizeof(waterinfo));
            timestamps[2] = millis();
        }
        else
        {
        #ifdef DEBUG
            Serial.println("Invalid packet...");
        #endif
        }
    }
    );
    tft.setTextColor(ST7735_WHITE);
    tft.println("ESP-NOW initialized.");
    tft.println("");
    tft.print("STARTING");
    for (u8 i = 0; i < 10; ++i)
        tft.print("."), delay(500);

    tft.fillScreen(ST7735_BLACK);
    tft.setCursor(0, 0);
    
    testdrawtext("Connecting..", ST7735_WHITE);
    tft.println("\r\n");
    tft.print("Wait for a moment.");
    tft.setCursor(0, 0);
    timeout.attach(TFT_SCREEN_TIMEOUT, _timeout_cb);
}// setup ends here

void loop()
{
    for (u8 i = 0; i < slaves_count; ++i)
    {   if (reply[i])
        {
            espmaster.send(remoteMac[i], (u8 *)&acks[i], sizeof(ack));
            reply[i] = false;

            delay(10);
            if (wi[i].distance > tankMinWaterHeight)
                wi[i].distance = tankMinWaterHeight;

            wi[i].distance = tankMinWaterHeight - wi[i].distance;
            if (wi[i].percentage > 100)
                wi[i].percentage = 100;

            wi[i].percentage = (wi[i].distance*100)/tankMinWaterHeight;
            //wi[i].percentage = 100 - wi[i].percentage;
#ifdef DEBUG
         //TODO: update the readings to serial or i2c/spi screen when you get the update        
            Serial.print("sensor #"); Serial.print(wi[i].sensorid); Serial.print(": ");
            Serial.println(wi[i].distance);
            Serial.print("Percentage: ");
            Serial.println(wi[i].percentage);
            Serial.print("Sensor Battery Voltage(mV): ");
            Serial.println(wi[i].batteryVoltage);
#endif
            digitalWrite(BUILTIN_LED, HIGH);
            if (wi[i].percentage >= 0 && wi[i].percentage <= 25)
              {
                 if (!redLedBlinking)
                   {
                      redLedBlinkTimer.detach();
                      redLedBlinkTimer.attach(1, _red_led_blink_cb);
                      redLedBlinking = true;
                   }
              }
            else if (wi[i].percentage > 25 && wi[i].percentage <= 50)
              {
                 if (redLedBlinking)
                   {
                      redLedBlinkTimer.detach();
                      redLedBlinking = false;
                      digitalWrite(redLedPin, HIGH);
                   }
              }
            else if (wi[i].percentage > tankWaterLevelThresholdPercent)
            {
                if (!redLedBlinking)
                {
                    redLedBlinkTimer.detach();
                    redLedBlinkTimer.attach(0.25, _red_led_blink_cb);
                    redLedBlinking = true;
                }
            }
            else
              {
                 if (redLedBlinking)
                   {
                      redLedBlinkTimer.detach();
                      redLedBlinking = false;
                      digitalWrite(redLedPin, HIGH);
                   }
              }

            if (displayOn)
              drawReadings(wi[i]);
        }

      if (millis() - timestamps[i] >= SLAVE_CONNECTION_TIMEOUT_LIMIT)
        {
           if (displayOn)
             {
                //tft.fillScreen(ST7735_YELLOW);

                tft.setTextSize(2);
                tft.setCursor(0, 0);
                tft.println("");
                tft.setTextColor(ST7735_RED);
                tft.println("     Sensor");
                tft.println("     Offline");
                tft.setTextColor(ST7735_WHITE);
                tft.setTextSize(1);
                //draw wifi icon
                tft.drawXBitmap(140, 2, net_wifi4_bits, net_wifi4_width, net_wifi4_height, ST7735_CYAN);
                //draw down
                tft.drawXBitmap(140, 2, down_bits, down_width, down_height, ST7735_RED);
                tft.drawLine(143, 4, 157, 18, ST7735_RED);
             }
#ifdef DEBUG
           Serial.print("----->>>> sensor #"); Serial.print(wi[i].sensorid); Serial.println(" is offline.");
#endif
        }
    }

    displayStatus = digitalRead(buttonPin);

    if (!displayStatus)
      {
         //software debounce
         delay(200);
         displayStatus = digitalRead(buttonPin);

         if (!displayStatus && displayOn)
           {
              timeout.detach();
              lcdOff();

#ifdef DEBUG
              Serial.println("Switching off TFT since button is pressed again.");
#endif
           }
         else if (!displayStatus)
           {
              //Switch On the display and show the reading.
#ifdef DEBUG

              Serial.println("Button is pressed.");
              Serial.println("Timer started and TFT is on now.");
#endif

              lcdOn();
              timeout.detach();
              timeout.attach(TFT_SCREEN_TIMEOUT, _timeout_cb);

              //show the last reading.
              drawReadings(wi[0]);
           }
      }
}
