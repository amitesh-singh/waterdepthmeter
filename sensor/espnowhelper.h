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

#ifndef __ESP_NOW__HELPER__H__
#define __ESP_NOW__HELPER__H__

#include <ESP8266WiFi.h>

extern "C"
{
    #include <espnow.h>
}

class espnow
{
    public:
    int init(WiFiMode_t mode = WIFI_STA, esp_now_role role = ESP_NOW_ROLE_CONTROLLER)
    {
        WiFi.mode(mode);
        delay(1000);

        int ret = esp_now_init();
        if (ret != 0)
        {
            //Serial.println("esp now init is failed. restarting..");
            //ESP.restart();
            return ret;
        }

        esp_now_set_self_role(role);

        return ret;
    }

    void addSendCb(esp_now_send_cb_t cb)
    {
        esp_now_register_send_cb(cb);
    }

    void addRecvCb(esp_now_recv_cb_t cb)
    {
        esp_now_register_recv_cb(cb);
    }

    int addPeer(uint8_t *macaddr, esp_now_role role, uint8_t channel, uint8_t *key, uint8_t len)
    {
        return esp_now_add_peer(macaddr, role, channel, key, len);
    }

    int send(uint8_t *macaddr, uint8_t *data, uint8_t len)
    {
        return esp_now_send(macaddr, data, len);
    }
};

#endif