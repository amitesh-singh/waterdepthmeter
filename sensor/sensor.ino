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

#include "espnowhelper.h"

//TODO: comment it out in production code.
#define DEBUG

#define SENSOR_ID 1
//calculate battery life from : https://www.geekstips.com/battery-life-calculator-sleep-mode/
// capacity: 2000mAh
// During sleep: 0.02 mA ~ 22uA
// current consumption during wake up: 80mA
// For 30s, timeout - 120 times 
// wakeup time ~ 1000mS
// ~26 days 
// for 60s, -- 52 days
//in seconds
#define SLEEP_PERIOD 60
#define WIFI_CHANNEL 1

const static u8 triggerPin = 5;  // D1
const static u8 echoPin = 4;  // D2
//GPIO 0
const static u8 dcstepupPin = 0;

//http://www.sintexplastics.com/wp-content/uploads/2017/04/ProductCatalogue2017.pdf
// 1000l - dia (inches): 43.3, height (inches): 48.2, dia menhole: 15.7 inches
// 48.2 inches = 122.4 cm
const static long fullDistance = 123; // in cm

static espnow esp12e;
// below mac is of Lollin board - which does not have pins
//static uint8_t remoteMac[] = {0x18, 0xFE, 0x34, 0xE1, 0xAC, 0x6A};
//below mac is for my esp12e board
static uint8_t remoteMac[] = {0x18, 0xFE, 0x34, 0xD3, 0x36, 0x76};

static volatile bool retry = false;
static uint8_t retransmit = 0;

struct __attribute__((__packed__)) waterinfo
{
    uint8_t sensorid;
    long distance;
    uint8_t percentage;
    uint32_t batteryVoltage;
};

static waterinfo wi;

static void resetHCSR04()
{
    #ifdef DEBUG
    Serial.println("Resetting HCSR04, pin high");
    #endif
    pinMode(dcstepupPin, LOW);
    delay(100);
    pinMode(dcstepupPin, HIGH);
    delay(100);
    if (digitalRead(echoPin))
    {
        #ifdef DEBUG
        Serial.println("Resetting Echo pin."); 
        #endif
        pinMode(echoPin, OUTPUT);
        digitalWrite(echoPin, LOW); // send a low pulse to echo pin
        delayMicroseconds(200);
        pinMode(echoPin, INPUT);
    }
    
   delay(50);
}

//Code for HCSR04 sonar sensor
// need to power HCSR04 from 5v or else it won't work, i was getting 0cm reading earlier
// connect 1k/2k resistor divider between ECHO 
void getDistance()
{
    long duration;
   
    digitalWrite(triggerPin, LOW);
    delayMicroseconds(2);

    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(10);

    digitalWrite(triggerPin, LOW);

    //We wait for echo to come back, with the timeout of 20ms,
    // which corresponds to approximately 3m
    // 400 * 29.4 * 2
    duration = pulseIn(echoPin, HIGH, 23000);

    //sometimes, HCSR04 gets stuck in case of 0 readings.
    // i notice this after i run it for 2-3 days.
    // below is the fix for this:
    if (duration == 0) //if we timed out
    {
        #ifdef DEBUG
            Serial.println("Timed out, Resetting..");    
        #endif
        //reset the HCSR04 power rail
        /*
        digitalWrite(dcstepupPin, LOW);
        delay(50);
        digitalWrite(dcstepupPin, HIGH);
        delay(50);
        */
        if (digitalRead(echoPin))
            resetHCSR04();
    }

    //29 - at room temperature
    // 29.4 at outdoors
    if (duration != 0)
      {
         wi.distance = (duration/2)/29.4; //(duration) * 0.017; //(duration/2)/29.4;
      }
    else
      wi.distance = fullDistance;

    wi.percentage = (wi.distance*100)/fullDistance;
}

/******* ADC Code to read battery voltage *************/

ADC_MODE(ADC_VCC);

/******************************************************/

void setup()
{
    #ifdef DEBUG
    Serial.begin(9600);
    #endif

    //switch on dc setup to power HC-SR04
    pinMode(dcstepupPin, OUTPUT);
    digitalWrite(dcstepupPin, HIGH);
    // let HCSR04 starts properly
    delay(50);
    wi.sensorid = SENSOR_ID;

    pinMode(triggerPin, OUTPUT);
    pinMode(echoPin, INPUT);

#ifdef DEBUG
   Serial.println("\r\n");
#endif

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, HIGH);

    // fucking unreliable shit module!!
    // reset HCSR04 every time. 
    if (digitalRead(echoPin))
        resetHCSR04();

    //init esp-now
    if (esp12e.init(WIFI_STA, ESP_NOW_ROLE_COMBO))
    {
        //failed to initialize espinit
    #ifdef DEBUG
        Serial.println("Failed to initialize esp-now");
    #endif

        ESP.restart();
        delay(1000);
    }
    #ifdef DEBUG
    else
        Serial.println("Started ESP-NOW init.");
    #endif

    //Add peer
    esp12e.addPeer(remoteMac, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, nullptr, 0);

    esp12e.addSendCb([](uint8_t *macaddr, uint8_t status)
    {
    #ifdef DEBUG
        Serial.println("send_cb");
    #endif
    }
    );

    retry = false;
    retransmit = 0;
    esp12e.addRecvCb([](u8 *mac_addr, u8 *data, u8 len)
    {
    #ifdef DEBUG
        Serial.println("recv cb");
    #endif

        retry = true;
    }
    );

    wi.batteryVoltage = ESP.getVcc();


#ifdef DEBUG
    Serial.print("Mac Addr: "); Serial.println(WiFi.macAddress());
    Serial.print("System voltage (mV): "); Serial.println(wi.batteryVoltage);
#endif
}

void loop()
{
     if (!retry)
    {
        getDistance();
#ifdef DEBUG
        Serial.print("Centimeter: ");
        Serial.print(wi.distance);
        Serial.print("; percentage: ");
        Serial.print(wi.percentage);
        Serial.println("");
#endif
        //was it crashing because of NULL?,
        // indeed, it was crashing because of null..
        // nope, it was crashing because of noise at power rails.
        //esp12e.send(NULL, (uint8_t *)&wi, sizeof(wi));
        esp12e.send(remoteMac, (uint8_t *)&wi, sizeof(wi));

        delay(100);
        ++retransmit;
    }

    if (retry || retransmit >= 5)
    {
        if (digitalRead(echoPin))
            resetHCSR04();
        //then deep sleep
    #ifdef DEBUG
        Serial.println("going to deep sleep...");
    #endif
        //switch off the dc steup module.
        pinMode(dcstepupPin, LOW);
        //
        // Deep Sleep
         ESP.deepSleep(SLEEP_PERIOD*1000000, WAKE_RF_DEFAULT);
    }
}
