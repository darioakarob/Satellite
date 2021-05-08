/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
     along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "devnull.h"

extern unsigned long IRAM_ATTR micros(void);

float rg_rnd(float min,float max) {
  return (esp_random()%100 - 0) * (max - min) / (100 - 0) + min;
}

float scale(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int devNullStartup(void) {
    int i;

    for (i=0; i<DEVNULL_DATA_BUFF; i++)    {
        rnd[i] = rg_rnd(-1,1) ;
    }
    temp = 0.0 + rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)];
    humi = 50.0 + rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)];
    if (humi < 0)
      humi *= -1;
    pres = 1000.0 + rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)];
    if (pres < 0)
      pres *= -1;
    return 0;
}

float gettempc() {
  temp = temp + (temp / 100 * rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)]);
  return temp;
}

float gettempf(){
  return gettempc() * 1.8 + 32;
}

float gethumi(){
  humi = humi + (humi / 100 * rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)]);
  return humi;
}

float getpres(){
  pres = pres + (pres / 100 * rnd[(int) rg_rnd(0,DEVNULL_DATA_BUFF-1)]);
  return pres;
}

float * devNullTask( void ) {
    static float data[3];

    devNullStartup();

    data[0] = gettempc();
    data[1] = gethumi();
    data[2] = getpres();

    return data;
}
