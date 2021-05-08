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
#include "driver/gpio.h"
#include "dht.h"

extern unsigned long IRAM_ATTR micros(void);

int dhtStartup(gpio_num_t dhtPin) {
    esp_err_t retcode;
    dhtDataPin = dhtPin;
    retcode = gpio_set_direction(dhtDataPin,GPIO_MODE_INPUT);
    //ESP_LOGI(TAG, "gpio_set_direction: %d => %d", retcode,dhtDataPin);
    retcode = retcode | gpio_set_intr_type(dhtDataPin, GPIO_INTR_NEGEDGE);
    //ESP_LOGI(TAG, "gpio_set_intr_type: %d", retcode);
    retcode = retcode | gpio_install_isr_service(ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_LOWMED);
    //ESP_LOGI(TAG, "gpio_install_isr_service: %d", retcode);
    state = DHT_IDLE;
    return retcode; //errCode;
}

void dhtISR (void* args) { 
    tmduration = micros();
    databuf[databuflen] = tmduration - tmstart;
    tmstart = tmduration;
    if ((databuflen + 1) == DHT_DATA_HBITS)
      state = DHT_RECV_END;
    databuflen++;
    //return NULL;
}

/*
int DHTStateMachine::getpulse(){
  return databuflen;
}

int DHTStateMachine::getBuffLen(){
  return cbuff;
}
*/
float gettempc() {
    return temp;
}

float gettempf(){
  return gettempc() * 1.8 + 32;
}
/*
float oldgettempc(){
  float tmax=-100.0;
  float tmin=100.0;
  float tavg=0.0;
  int i=0;

  if (cbuff == 0)
    return -100.0;
  if (cbuff == 1 || cbuff == 2)
    return tbuff[cbuff - 1];
  for(i=0; i< cbuff; i++) {
    if (tbuff[i] <= tmin)
      tmin = tbuff[i];
    if (tbuff[i] >= tmax)
      tmax = tbuff[i];
    tavg += tbuff[i];
  }
  tavg = (tavg - (tmax) - (tmin)) / (1.0 * (cbuff - 2));  
  return tavg;
}
*/
float gethumi(){
  return humi;
}
/*
float oldgethumi(){
  float hmax=0.0;
  float hmin=100.0;
  float havg=0.0;
  int i=0;

  if (cbuff == 0)
    return 0.0;
  if (cbuff == 1 || cbuff == 2)
    return hbuff[cbuff - 1];
  for(i=0; i< cbuff; i++) {
    if (hbuff[i] <= hmin)
      hmin = hbuff[i];
    if (hbuff[i] >= hmax)
      hmax = hbuff[i];
    havg += hbuff[i];
  }
  havg = (havg - (hmax) - (hmin)) / (1.0 * (cbuff - 2));  
  return havg;
}
*/
enum DHTMachineStates_e setstatus(enum DHTMachineStates_e newstatus){
  state = newstatus;
  return state;
}

enum DHTMachineStates_e getstatus() {
  return state;
}
/*
int DHTStateMachine::geterror() {
  return errCode;
}

int DHTStateMachine::settimeout(int timepassed) {
  timeout = timeout - timepassed;
  return timeout;
}

int DHTStateMachine::cleartimeout(void) {
  timeout = -1;
  return timeout;
}
*/
esp_err_t detachISR() {
  esp_err_t retcode;
  retcode = gpio_isr_handler_remove(dhtDataPin);
  return retcode;      
}


esp_err_t attachISR() {
  esp_err_t retcode;
  retcode = gpio_isr_handler_add(dhtDataPin, dhtISR, NULL);
  //ESP_LOGI(TAG, "gpio_isr_handler_add: %d", retcode);
  return retcode;      
}

int decodeDHT(void) {
uint8_t byte1=0;  
uint8_t byte2=0;  
uint8_t byte3=0;  
uint8_t byte4=0;  
uint8_t byte5=0;  

uint8_t tempsign = 0;

if (databuf[0] < 50) {
    return DHT_ERR_ACK;
}
else {
    for (int i=1;i<DHT_DATA_HBITS;i++) {
        if (i>=1 && i<=8) {
            byte1 |= ((databuf[i] > 60 && databuf[i] < 100) ? 0 << (7-((i-1)%8)) : 1 << (7 - ((i-1)%8)));
        }
        if (i>=9 && i<=16) {
            byte2 |= ((databuf[i] > 60 && databuf[i] < 100) ? 0 << (7-((i-1)%8)) : 1 << (7 - ((i-1)%8)));
        }
        if (i>=17 && i<=24) {
            byte3 |= ((databuf[i] > 60 && databuf[i] < 100) ? 0 << (7-((i-1)%8)) : 1 << (7 - ((i-1)%8)));
        }
        if (i>=25 && i<=32) {
            byte4 |= ((databuf[i] > 60 && databuf[i] < 100) ? 0 << (7-((i-1)%8)) : 1 << (7 - ((i-1)%8)));
        }
        if (i>=33 && i<=40) {
            byte5 |= ((databuf[i] > 60 && databuf[i] < 100) ? 0 << (7-((i-1)%8)) : 1 << (7 - ((i-1)%8)));
        }
    }
}

if (byte5 != ((byte1 + byte2 + byte3 + byte4) & 0xFF))
    return DHT_ERR_CKS;

if ((byte3 & 0x8000) == 0x8000) {
    tempsign = 1;
    byte3 &= 0x7fff;
}

temp = (tempsign == 1) ? -0.1 * ((byte3 << 8) + byte4) : 0.1 * ((byte3 << 8) + byte4);
humi = 0.1 * ((byte1 << 8) + byte2);
pres = temp /humi;

return DHT_ERR_OK;
}

   
void dhtmachineLoop() {
  switch (state) {
    case DHT_START:
      for (int i=0;i<DHT_DATA_HBITS;i++)
        databuf[i] = 0;
        
      gpio_set_direction(dhtDataPin,GPIO_MODE_OUTPUT);
      gpio_set_level(dhtDataPin, 0);
      //GPOC = 1<<datapin;
      //errStr = "DHT_START: " + (String) digitalRead(datapin) + ", ";
      databuflen = 0;
      state = DHT_RELEASE;
      tmduration = micros();
      tmstart= tmduration;
      //ESP_LOGI(TAG, "DHT_START %lu",tmstart);
      break;
    case DHT_RELEASE:
      //ESP_LOGI(TAG, "DHT_RELEASE");
      tmduration = micros();
      if (tmduration - tmstart >= 500) {
        //ESP_LOGI(TAG, "DHT_RELEASE %lu",tmduration);
        //errStr += "DHT_RELEASE: " + (String) digitalRead(datapin) + ", ";
        tmduration = micros();
        tmstart= tmduration;
        state = DHT_RECV_BEG;
        tmelapsed = tmduration;
        gpio_set_level(dhtDataPin, 1);
        //GPOS= 1<<datapin;
        gpio_set_direction(dhtDataPin,GPIO_MODE_INPUT);
        //noInterrupts();
        attachISR();
      }
      break;
    case DHT_RECV_BEG:
      //ESP_LOGI(TAG, "DHT_RECVBEG");
      if ((micros() - tmelapsed) > 5000) {
        //errStr += " DHT_TIMEOUT b4 DHT_RECV_END: " + (String) databuflen + ",";
        detachISR();
        //interrupts();
        state = DHT_TIMEOUT;
      }
      break;
    case DHT_RELEASE_END:
      //ESP_LOGI(TAG, "DHT_RELEASE_END %d",databuflen);
      break;
    case DHT_ACK:
      //ESP_LOGI(TAG, "DHT_ACK %d",databuflen);
      break;
    case DHT_RECV_END:
      //ESP_LOGI(TAG, "DHT_RECV_END %d",databuflen);
      detachISR();
      //interrupts();
      //errStr += " DHT_RECV_END, ";
      errCode = decodeDHT();
      //errStr += " DHT_RECV_DECODE: " + (String) errCode + ",";
      state = DHT_IDLE;
      //errStr += " DHT_IDLE.";
      //logBuf.append(errStr);
      gpio_set_direction(dhtDataPin,GPIO_MODE_INPUT);
      break;
    case DHT_UNAVAIL:
    case DHT_IDLE:
      break;
    case DHT_TIMEOUT:
      //errStr += " DHT_TIMEOUT.";
      errCode = DHT_ERR_TMO;
      //logBuf.append(errStr);
      state = DHT_IDLE;
      gpio_set_direction(dhtDataPin,GPIO_MODE_INPUT);
      //ESP_LOGI(TAG, "DHT_TIMEOUT %d",databuflen);
      break;
    case DHT_STOP:
      //errStr += " DHT_STOP " + (String) errCode + ".";
      //logBuf.append(errStr);
      state = DHT_UNAVAIL;
      gpio_set_direction(dhtDataPin,GPIO_MODE_INPUT);
      break;
  }

}


/*
void ICACHE_RAM_ATTR DHTStateMachine::dhtPin15 () {
  if (instances[15] != NULL)
    instances[15]->dhtISR();
}
*/

float * dhtTask( void ) {
    static float data[3];

    data[0] = 0;
    data[1] = 0;
    data[2] = 0;

    dhtStartup(GPIO_NUM_27);
    state = DHT_START;
    while (!(state == DHT_IDLE || state == DHT_TIMEOUT || state == DHT_STOP || state == DHT_UNAVAIL)) {
       dhtmachineLoop(); 
    }

    if (state == DHT_IDLE) {
        data[0] = gettempc();
        data[1] = gethumi();
        data[2] = 0.0;
    }
    return data;
}
