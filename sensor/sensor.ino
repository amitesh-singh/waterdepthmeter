
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
#define SLEEP_PERIOD 30

// On Lollin board
#define TRIGGER 5  // (D1)
#define ECHO  4  // D2
//http://www.sintexplastics.com/wp-content/uploads/2017/04/ProductCatalogue2017.pdf
// 1000l - dia (inches): 43.3, height (inches): 48.2, dia menhole: 15.7 inches
// 48.2 inches = 122.4 cm
const static long fullDistance = 123; // in cm

struct __attribute__((__packed__)) waterinfo
{
    uint8_t sensorid;
    long distance;
    uint8_t percentage;
};

static waterinfo wi;

//Code forCSR04 sonar sensor
// need to power HCSR04 from 5v or else it won't work, i was getting 0cm reading earlier
// connect 1k/2k resistor divider between ECHO 
void getDistance()
{
    long duration;

    digitalWrite(TRIGGER, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIGGER, HIGH);
    delayMicroseconds(10);

    digitalWrite(TRIGGER, LOW);

    duration = pulseIn(ECHO, HIGH);

    wi.distance = (duration/2)/29.1;
    wi.percentage = (wi.distance*100)/fullDistance;
}

#define WIFI_CHANNEL 1
static espnow esp12e;
// below mac is of Lollin board - which does not have pins
//static uint8_t remoteMac[] = {0x18, 0xFE, 0x34, 0xE1, 0xAC, 0x6A};
//below mac is for my esp12e board
static uint8_t remoteMac[] = {0x18, 0xFE, 0x34, 0xD3, 0x36, 0x76};

static volatile bool retry = false;
static uint8_t retransmit = 0;

void setup()
{
    #ifdef DEBUG
    Serial.begin(9600);
    delay(100);
    #endif
    
    wi.sensorid = SENSOR_ID;

    pinMode(TRIGGER, OUTPUT);
    pinMode(ECHO, INPUT);

#ifdef DEBUG
   Serial.println("\r\n");
#endif

    pinMode(BUILTIN_LED, OUTPUT);
    digitalWrite(BUILTIN_LED, HIGH);

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

#ifdef DEBUG
    Serial.print("Mac Addr: "); Serial.println(WiFi.macAddress());
#endif
}

void loop()
{
    getDistance();
#ifdef DEBUG
    Serial.println("Centimeter: ");
    Serial.print(wi.distance);
    Serial.print("; percentage: ");
    Serial.print(wi.percentage);
    Serial.println("");
#endif
    //digitalWrite(BUILTIN_LED, !digitalRead(BUILTIN_LED));

    esp12e.send(NULL, (uint8_t *)&wi, sizeof(wi));
    ++retransmit;

    delay(100);
    if (retry || retransmit >= 5)
    {
        //then deep sleep
    #ifdef DEBUG
        Serial.println("going to deep sleep...");
    #endif
        //
        // sleep for 10s
         ESP.deepSleep(SLEEP_PERIOD*1000000, WAKE_RF_DEFAULT);
        //ESP.restart();
        //delay(100);
    }
}
