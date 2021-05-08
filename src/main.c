#define LOG_LOCAL_LEVEL ESP_LOG_WARN


#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_crc.h"

#include "esp32/ulp.h"
#include "esp32/rom/ets_sys.h"
#include "esp32/rom/crc.h"

#include "driver/gpio.h"
#include "driver/touch_pad.h"
#include "driver/adc.h"
#include "driver/rtc_io.h"

#include "soc/rtc_cntl_reg.h"
#include "soc/rtc.h"

#include "satellite.h"

#ifdef DHTXX
    #include "dht.h"
    extern float * dhtTask( void );
#endif
#ifdef DEVNULL
    #include "devnull.h"
    extern float * devNullTask( void );
#endif
#ifdef DALLASTEMP
    #include "dallasTemp.h"
    extern float * ds18b20Task( void );
#endif
#ifdef BMP280
    #include "BMP280.h"
    extern float * bmp280Task( void );
#endif

/*
extern float       temp;
extern float       humi;
extern float       pres;
*/
static const char* TAG = "aRGiSAT";
static char* PMK = "pmk1234567890123";
int i=0;


//static void satellite_deinit(satellite_send_packet_t *send_param);
char* mac2str (const uint8_t*, char*);

unsigned long IRAM_ATTR micros()
{
    return (unsigned long) (esp_timer_get_time());
}

int check_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
 
  wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    return 0;
  }
  else {
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
      return 1;
    }
    else {
      return -1;
    }
  }
}

static sat_retcode_t satellite_wifi_init(void)
{
    sat_retcode_t retcode;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t wifiError;

    wifiError = esp_netif_init();
    if (wifiError != ESP_OK) {
        retcode.code = 1;
        strcpy(retcode.desc,"esp_netif_init");
        return retcode;
    }
    wifiError = esp_event_loop_create_default();
    if (wifiError != ESP_OK) {
        retcode.code = 2;
        strcpy(retcode.desc,"esp_event_loop_create_default");
        return retcode;
    }
    wifiError = esp_wifi_init(&cfg);
    if (wifiError != ESP_OK) {
        retcode.code = 3;
        strcpy(retcode.desc,"esp_wifi_init");
        return retcode;
    }
    wifiError = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (wifiError != ESP_OK) {
        retcode.code = 4;
        strcpy(retcode.desc,"esp_wifi_set_storage");
        return retcode;
    }
    wifiError = esp_wifi_set_mode(WIFI_MODE_STA);
    if (wifiError != ESP_OK) {
        retcode.code = 5;
        strcpy(retcode.desc,"esp_wifi_set_mode");
        return retcode;
    }
    wifiError = esp_wifi_start();
    if (wifiError != ESP_OK) {
        retcode.code = 6;
        strcpy(retcode.desc,"esp_wifi_start");
        return retcode;
    }
    wifiError = esp_wifi_set_channel(wifiChannels[SATELLITE_DEFAULT_CHANNEL],0);
    if (wifiError != ESP_OK) {
        retcode.code = 6;
        strcpy(retcode.desc,"esp_wifi_set_channel");
        return retcode;
    }
    #if CONFIG_ESPNOW_ENABLE_LONG_RANGE
        wifiError =  esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR) );
        if (wifiError != ESP_OK) {
            retcode.code = 7;
            strcpy(retcode.desc,"esp_wifi_set_protocol");
            return retcode;
        }
    #endif

    return retcode;
}

esp_err_t addPeer(uint8_t *peerMAC, uint8_t channel) {
    esp_now_peer_info_t *peer;
    esp_err_t retCode;

    if (!esp_now_is_peer_exist(peerMAC)) {
        peer = malloc(sizeof(esp_now_peer_info_t));
        if (peer == NULL)
            return 1;

        memset(peer, 0, sizeof(esp_now_peer_info_t));
        peer->channel = channel;
        peer->ifidx = ESP_IF_WIFI_STA;
        peer->encrypt = false;
        memcpy(peer->peer_addr, peerMAC, ESP_NOW_ETH_ALEN);
        retCode =  esp_now_add_peer(peer);
        free(peer);
        if (retCode != ESP_OK) {
            return 5;
        }
    }
    return ESP_OK;
}

static void satellite_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (!(mac_addr == NULL))
        satellite.send = 1;
}

static void satellite_recv_cb(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
    if (mac_addr == NULL || incomingData == NULL || len <= 0) 
        satellite.receive = -1;
    else {
        memcpy(receive_buffer, incomingData, len); 
        satellite.receive = len;
    }
}

int receivePacket() {
    volatile unsigned long  tmstart;
    volatile unsigned long  tmduration; 

    tmduration = micros();
    tmstart= tmduration;

    while (((tmduration - tmstart) < SATELLITE_RECEIVE_MAXWAIT) && satellite.receive == 0) {
        tmduration = micros();
    }
    return satellite.receive;
}
   

int sendPacket(uint8_t * pkt,size_t pLen, uint8_t * destination) {

    int retCode = 1;

    ESP_LOGW(TAG, "Sending to: %s", mac2str(destination,NULL));

    retCode = esp_now_send(destination,  pkt, pLen);
    if (retCode != ESP_OK) {
        if (satellite.send_errors > 65534)
            satellite.send_errors = 1;
        else
            satellite.send_errors++;
    }
    return retCode;
}

esp_err_t sendMessage(enum PacketTypes_e msgType,uint8_t * packetTo) {

    satellite_packet_head_t packet_head;
    satellite_packet_payload_t packet_payload;
    satellite_packet_payload_data_t data;

    size_t buffSize;
    static uint8_t * buff2send;
    uint16_t crc;
    esp_err_t retCode = 0xff;

    memcpy(packet_head.src_addr, satellite.local_addr, sizeof(satellite.local_addr));
    memcpy(packet_head.dest_addr, satellite.dest_addr, sizeof(satellite.dest_addr));
    packet_head.type = msgType;

    packet_payload.state = satellite.state;
    packet_payload.command = 0;
    packet_payload.uptime = satellite.uptime;

    if (msgType == PT_ADDRREQ_ACK || msgType == PT_TELEMETRY) {
        data.temp = satellite.temp;
        data.humi = satellite.humi;
        data.pres = satellite.pres;
        buffSize = sizeof(packet_head) + sizeof(satellite_packet_payload_t) + sizeof(satellite_packet_payload_data_t) + sizeof(uint16_t);
    }
    else
        buffSize = sizeof(packet_head) + sizeof(satellite_packet_payload_t) + sizeof(uint16_t);

    buff2send = malloc(buffSize);
    if (!(buff2send == NULL)) {
        memset(buff2send, 0, buffSize);
        memcpy(buff2send, &packet_head, sizeof(satellite_packet_head_t));
        memcpy(&buff2send[sizeof(satellite_packet_head_t)], &packet_payload, sizeof(satellite_packet_payload_t));
        if (msgType == PT_ADDRREQ_ACK || msgType == PT_TELEMETRY) {
            memcpy(&buff2send[sizeof(satellite_packet_head_t)+sizeof(satellite_packet_payload_t)], &data, sizeof(satellite_packet_payload_data_t));
            crc = 0x69; //esp_crc16_le(UINT16_MAX, (uint8_t const *)buff2send, buffSize);
            memcpy(&buff2send[sizeof(satellite_packet_head_t)+sizeof(satellite_packet_payload_t) + sizeof(satellite_packet_payload_data_t)], &crc, sizeof(uint16_t));
        }
        else {
            crc = 0x69; //esp_crc16_le(UINT16_MAX, (uint8_t const *)buff2send, buffSize);
            memcpy(&buff2send[sizeof(satellite_packet_head_t)+sizeof(satellite_packet_payload_t)], &crc, sizeof(uint16_t));
        }
        retCode = sendPacket(buff2send, buffSize, packetTo);
        free(buff2send);
    }
    return retCode;
}

esp_err_t satellite_getchannel(void) {
    int result;
    esp_err_t retCode;
    bool done = false;
    uint8_t channel = SATELLITE_DEFAULT_CHANNEL;
    bool channelFound = false;
    bool channelNotFound = false;
    const TickType_t b4SendxDelay = SATELLITE_SEND_DELAY / portTICK_PERIOD_MS;
    
    satellite.send = 0;
    while (!channelFound && !channelNotFound) {
        retCode = sendMessage(PT_ADDRREQ, broadcastAddress);
        if (retCode == ESP_OK) {
            satellite.tmduration = micros();
            satellite.tmstart= satellite.tmduration;
            while (((satellite.tmduration - satellite.tmstart) < SATELLITE_SEND_MAXWAIT) && !done) {
                if (satellite.send == 1)
                    done = true;
                satellite.tmduration = micros();
            }
            if (done) {
                satellite.receive = 0;
                result = receivePacket();
                if (result > 0) {
                    channelFound = true;
                    satellite.channel = channel;
                }
            }
        }
        if (!channelFound) {
            if (++channel < SATELLITE_CHANNELS) {
                esp_wifi_set_channel(wifiChannels[channel],0);
                esp_now_del_peer(broadcastAddress);
                addPeer(broadcastAddress,wifiChannels[channel]);
                vTaskDelay( b4SendxDelay );
            }
            else
                channelNotFound = true;
        }
    }
    if (!channelFound)
        return -1;
    ESP_LOGW(TAG, "Satellite channel: %d" , wifiChannels[channel]);
    return channel;
}

esp_err_t satellite_getaddr(void) {
    int result;
    esp_err_t retCode;
    bool done = false;
    satellite_packet_head_t packet_head;
    const TickType_t b4SendxDelay = SATELLITE_SEND_DELAY / portTICK_PERIOD_MS;

    satellite.send = 0;

    retCode = sendMessage(PT_ADDRREQ, broadcastAddress);
    if (retCode == ESP_OK) {
        satellite.tmduration = micros();
        satellite.tmstart= satellite.tmduration;
        while (((satellite.tmduration - satellite.tmstart) < SATELLITE_SEND_MAXWAIT) && !done) {
            if (satellite.send == 1)
                done = true;
            satellite.tmduration = micros();
        }
        if (done) {
            satellite.receive = 0;
            result = receivePacket();
            if (result > 0) {
                memcpy(&packet_head, receive_buffer, sizeof(satellite_packet_head_t)); 
                if (packet_head.type == PT_ADDRREQ_OFFER) {
                    memcpy(&satellite.dest_addr, packet_head.src_addr, ESP_NOW_ETH_ALEN); 
                    ESP_LOGW(TAG, "Received from: %s", mac2str(satellite.dest_addr,NULL));
                    retCode = addPeer(packet_head.src_addr,satellite.channel);
                    if (retCode == ESP_OK) {
                        satellite.send = 0;
                        done = false;
                        vTaskDelay( b4SendxDelay );
                        retCode = sendMessage(PT_ADDRREQ_REQ, satellite.dest_addr);
                        if (retCode == ESP_OK) {
                            satellite.tmduration = micros();
                            satellite.tmstart = satellite.tmduration;
                            while (((satellite.tmduration - satellite.tmstart) < SATELLITE_SEND_MAXWAIT) && !done) {
                                if (satellite.send == 1)
                                    done = true;
                                satellite.tmduration = micros();
                            }
                            if (done) {
                                satellite.receive = 0;
                                result = receivePacket();
                                if (result > 0) {
                                    memcpy(&packet_head, receive_buffer, sizeof(satellite_packet_head_t)); 
                                    if (packet_head.type == PT_ADDRREQ_ACK) {
                                        satellite.send = 0;
                                        if (esp_now_is_peer_exist(packet_head.src_addr)) {
                                            memcpy(&satellite.dest_addr, packet_head.src_addr, ESP_NOW_ETH_ALEN); 
                                            retCode = ESP_OK;
                                        }
                                        else
                                            retCode = 0x1269;
                                    }
                                    else
                                        retCode = 0x1270;
                                }
                                else
                                    retCode = 0x1271;
                            }
                            else
                                retCode = 0x1272;
                        }
                    }
                    else 
                        retCode = 0x1273;
                }
                else
                    retCode = 0x1274;
            }
            else
                retCode = 0x1275;
        }
        else
            retCode = 0x1273;
    }
    return retCode;
}

esp_err_t satellite_telemetry(void) {
    int result;
    esp_err_t retCode;
    bool done = false;
    satellite_packet_head_t packet_head;

    satellite.send = 0;

    sendMessage(PT_TELEMETRY, satellite.dest_addr);
    satellite.tmduration = micros();
    satellite.tmstart= satellite.tmduration;
    while (((satellite.tmduration - satellite.tmstart) < SATELLITE_SEND_MAXWAIT) && !done) {
        if (satellite.send == 1)
            done = true;
        satellite.tmduration = micros();
    }
    if (done) {
        satellite.receive = 0;
        result = receivePacket();
        if (result > 0) {
            memcpy(&packet_head, receive_buffer, sizeof(satellite_packet_head_t)); 
            if (packet_head.type == PT_TELEMETRY_ACK) {
                satellite.send = 0;
                if (esp_now_is_peer_exist(packet_head.src_addr)) {
                    memcpy(&satellite.dest_addr, packet_head.src_addr, ESP_NOW_ETH_ALEN); 
                    retCode = ESP_OK;
                }
                else
                    retCode = 0x1269;
            }
            else
                retCode = 0x1270;
        }
        else
            retCode = 0x1275;
    }
    else
        retCode = 0x1273;
    return retCode;
}

static sat_retcode_t satellite_espnow_init(void) {
    esp_err_t espnowError;
    sat_retcode_t retcode;

    ESP_LOGW(TAG, "Satellite espnow init");
    /* Initialize ESPNOW and register sending and receiving callback function. */
    espnowError =  esp_now_init();
    if (espnowError != ESP_OK) {
        retcode.code = 1;
        strcpy(retcode.desc,"esp_now_init");
        return retcode;
    }

    /* Set primary master key. */
    espnowError =  esp_now_set_pmk((uint8_t *)PMK);
    if (espnowError != ESP_OK) {
        retcode.code = 3;
        strcpy(retcode.desc,"esp_now_set_pmk");
        return retcode;
    }

    espnowError =  (esp_err_t) esp_now_register_send_cb((esp_now_send_cb_t) satellite_send_cb);
    if (espnowError != ESP_OK) {
        retcode.code = 2;
        strcpy(retcode.desc,"esp_now_register_send_cb");
        return retcode;
    }

    espnowError =  (esp_err_t) esp_now_register_recv_cb((esp_now_recv_cb_t) satellite_recv_cb);
    if (espnowError != ESP_OK) {
        retcode.code = 3;
        strcpy(retcode.desc,"esp_now_register_recv_cb");
        return retcode;
    }

    espnowError =  addPeer(broadcastAddress,wifiChannels[SATELLITE_DEFAULT_CHANNEL]);
    if (espnowError != ESP_OK) {
        retcode.code = 5;
        strcpy(retcode.desc,"esp_now_add_peer");
        return retcode;
    }

    retcode.code = 0;
    strcpy(retcode.desc,"success");
    return retcode;
}

char* mac2str (const uint8_t* mac, char* extBuffer) {
     char* buffer;
     static char staticBuffer[ESP_NOW_ETH_ALEN * 3];
     
     if (!extBuffer){
         buffer = staticBuffer;
     } else {
         buffer = extBuffer;
     }
     
     if (mac && buffer) {
         snprintf (buffer, ESP_NOW_ETH_ALEN * 3, MACSTR, MAC2STR (mac));
         return buffer;
     }
     return NULL;
}

void app_main(void) {
    uint8_t mac[6];
    //bool txrxOK = false;
    time_t localTime;
    uint64_t tm2sleep = 0;
    float * data;
    esp_err_t appError;
    sat_retcode_t appWifiError;
    sat_retcode_t appEspNowError;

    ESP_LOGW(TAG, "Satellite");
    //esp_log_level_set("*", ESP_LOG_ERROR);
    //esp_log_level_set("aRGiSAT", ESP_LOG_INFO);
    //esp_log_level_set("*", ESP_LOG_DEBUG);

    satellite.send = 0;
    satellite.receive = 0;
    receive_buffer = (uint8_t*) malloc( SATELLITE_PACKET_MAXLEN );
    if (receive_buffer == NULL) {
        satellite.err_code = 1;
        strcpy((char *) &satellite.err_text,"Malloc receive buffer fail");
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }
    send_buffer = (uint8_t*) malloc( SATELLITE_PACKET_MAXLEN );
    if (send_buffer == NULL) {
        satellite.err_code = 2;
        strcpy((char *) &satellite.err_text,"Malloc send buffer fail");
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    // Initialize NVS
    appError =  nvs_flash_erase();
    if (!(appError == ESP_OK)) {
        satellite.err_code = 3;
        sprintf((char *) &satellite.err_text,"nvs_flash_erase() %d", appError);
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    appError = nvs_flash_init();
    if (!(appError == ESP_OK)) {
        satellite.err_code = 4;
        sprintf((char *) &satellite.err_text,"nvs_flash_init() %d", appError);
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    appWifiError = satellite_wifi_init();
    if (appWifiError.code != ESP_OK) {
        satellite.err_code = 5;
        sprintf((char *) &satellite.err_text,"satellite_wifi_init() %d %s", appWifiError.code, appWifiError.desc);
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    appEspNowError = satellite_espnow_init();
    if (appEspNowError.code != ESP_OK) {
        satellite.err_code = 6;
        sprintf((char *) &satellite.err_text,"satellite_espnow_init() %d %s", appWifiError.code, appWifiError.desc);
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    esp_now_peer_num_t num;
    appError = esp_now_get_peer_num(&num);
    if (!(appError == ESP_OK)) {
        satellite.err_code = 7;
        sprintf((char *) &satellite.err_text,"esp_err_tesp_now_get_peer_num() %d", appError);
        ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }

    if (hasReboot) {
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        memcpy(satellite.local_addr, &mac, sizeof(mac));
        memcpy(satellite.dest_addr, broadcastAddress, ESP_NOW_ETH_ALEN);
        satellite.state = 0;
        satellite.pktnum = 0;
        satellite.uptime = 0;
        satellite.send_errors = 0;
        satellite.rssi = 0;
        satellite.send_timeouts = 0;
        satellite.receive_timeouts = 0;
        satellite.send_errors = 0;
        satellite.receive_errors = 0;
        satellite.send = 0;
        satellite.receive = 0;
        appError = satellite_getchannel();
        if ((appError < 0)) {
            satellite.err_code = 8;
            sprintf((char *) &satellite.err_text,"satellite satellite_getchannel %x", appError);
            ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
            esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
            esp_deep_sleep_start();
        }
        else {
            appError = satellite_getaddr();
            if (!(appError == ESP_OK)) {
                satellite.channel = appError;
                satellite.err_code = 9;
                sprintf((char *) &satellite.err_text,"satellite get gateway address() %x", appError);
                ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
                esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
                esp_deep_sleep_start();
            }
        } 
        ESP_LOGW(TAG, "Satellite reboot: last message: %s", satellite.err_text);
    }
    else {
        time(&localTime);
        if (satellite.uptime + (localTime / 60 - satellite.uptime) > 65534)
            satellite.uptime = 1;
        else
            satellite.uptime = localTime / 60;
        esp_wifi_set_channel(satellite.channel,0);
        appError = addPeer(satellite.dest_addr,satellite.channel);
        if (appError != ESP_OK) {
            satellite.err_code = 9;
            sprintf((char *) &satellite.err_text,"add gateway peer() %x", appError);
            ESP_LOGW(TAG, "Warning: %s", satellite.err_text);
            esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
            esp_deep_sleep_start();
        }
        ESP_LOGW(TAG, "Satellite restart: last message: %s", satellite.err_text);
    }

    /*
    if (satellite.restart == 1) {
        hasReboot = true;
        satellite.restart = 0;
        esp_restart();
    }
    */

#ifdef DHTXX
    data = dhtTask();
#endif
#ifdef DEVNULL
    data = devNullTask();
#endif
#ifdef DALLASTEMP
    data = ds18b20Task();
#endif
#ifdef BMP280
    data = bmp280Task();
#endif
    satellite.temp = data[0];
    satellite.humi = data[1];
    satellite.pres = data[2];
    
    appError = satellite_telemetry();
    if (!(appError == ESP_OK)) {
        satellite.err_code = 10;
        sprintf((char *) &satellite.err_text,"satellite send telemetry data() %x", appError);
        esp_sleep_enable_timer_wakeup(SATELLITE_SLEEP_TIME_ON_ERROR);
        esp_deep_sleep_start();
    }
    sprintf((char *) &satellite.err_text,"Success");

    tm2sleep = (uint64_t) (SATELLITE_SLEEP_TIME) * uSEC_TO_SEC;
    esp_sleep_enable_timer_wakeup(tm2sleep);

    ESP_LOGW(TAG, "Satellite MAC: %s, uptime %d minutes, Telemetry: %x, T %3.3f, H %3.3f, P %3.3f. Going to sleep from %s for %d seconds", mac2str(satellite.local_addr, NULL), satellite.uptime, appError, data[0], data[1], data[2], (hasReboot ? "reboot" : "wakeup"), ((int) tm2sleep / uSEC_TO_SEC));

    hasReboot = false;

    esp_deep_sleep_start();
    
}
  /*
  if (sensor.isAlarm == HIGH)
    esp_sleep_enable_ext0_wakeup(RGTA6_REED_PIN,LOW);
  else
    esp_sleep_enable_ext0_wakeup(RGTA6_REED_PIN,HIGH);
  */


/*
  gpio_reset_pin(GPIO_NUM_0);
  gpio_reset_pin(GPIO_NUM_2);
  //gpio_reset_pin(GPIO_NUM_12);
  gpio_reset_pin(GPIO_NUM_4);
  //gpio_reset_pin(GPIO_NUM_13);
  gpio_reset_pin(GPIO_NUM_14);
  gpio_reset_pin(GPIO_NUM_15);
  gpio_reset_pin(GPIO_NUM_25);
  gpio_reset_pin(GPIO_NUM_26);
  gpio_reset_pin(GPIO_NUM_27);
  gpio_reset_pin(GPIO_NUM_32);
  gpio_reset_pin(GPIO_NUM_33);
  gpio_reset_pin(GPIO_NUM_34);
  gpio_reset_pin(GPIO_NUM_35);
  gpio_reset_pin(GPIO_NUM_36);
  gpio_reset_pin(GPIO_NUM_37);
  gpio_reset_pin(GPIO_NUM_38);
  gpio_reset_pin(GPIO_NUM_39);

  rtc_gpio_isolate(GPIO_NUM_0);
  rtc_gpio_isolate(GPIO_NUM_4);
  //rtc_gpio_isolate(GPIO_NUM_12);
  rtc_gpio_isolate(GPIO_NUM_14);
  rtc_gpio_isolate(GPIO_NUM_15);
  rtc_gpio_isolate(GPIO_NUM_25);
  rtc_gpio_isolate(GPIO_NUM_26);
  rtc_gpio_isolate(GPIO_NUM_27);
  rtc_gpio_isolate(GPIO_NUM_32);
  rtc_gpio_isolate(GPIO_NUM_33);
  rtc_gpio_isolate(GPIO_NUM_34);
  rtc_gpio_isolate(GPIO_NUM_35);
  rtc_gpio_isolate(GPIO_NUM_36);
  rtc_gpio_isolate(GPIO_NUM_37);
  rtc_gpio_isolate(GPIO_NUM_38);
  rtc_gpio_isolate(GPIO_NUM_39);
  
  gpio_hold_en(RGTA6_RADIO_EN_PIN);
  gpio_deep_sleep_hold_en();
  */
    /*
    ESP_LOGW(TAG, "configTICK_RATE_HZ: %d",configTICK_RATE_HZ);
    ESP_LOGW(TAG, "portTICK_PERIOD_MS: %d",portTICK_PERIOD_MS);
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK( nvs_flash_erase() );
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    satellite_wifi_init();
    satellite_espnow_init();
    */
    //satellite_data_queue = xQueueCreate( 10, sizeof( uint32_t ) );

    //xTaskCreate(
    //    anotherTask, // Task function. 
    //    "another Task", // name of task. 
    //    10000, // Stack size of task 
    //    NULL, // parameter of the task 
    //    1, // priority of the task 
    //    NULL); // Task handle to keep track of created task 
    /*
    xTaskCreate(
        dhtTask, // Task function. 
        "dht Task", // name of task. 
        10000, // Stack size of task 
        NULL, // parameter of the task 
        1, // priority of the task 
        NULL); // Task handle to keep track of created task 
    xTaskCreate(
        ds18b20Task, // Task function. 
        "ds18b20 Task", // name of task. 
        10000, // Stack size of task 
        NULL, // parameter of the task 
        1, // priority of the task 
        NULL); // Task handle to keep track of created task 
*/
/*
int satellite_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic)
{
    satellite_data_t *buf = (satellite_data_t *)data;
    uint16_t crc, crc_cal = 0;

    if (data_len < sizeof(satellite_data_t)) {
        ESP_LOGW(TAG, "Receive ESPNOW data too short, len:%d", data_len);
        return -1;
    }

    *state = buf->state;
    *seq = buf->seq_num;
    *magic = buf->magic;
    crc = buf->crc;
    buf->crc = 0;
    crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

    if (crc_cal == crc) {
        return buf->type;
    }

    return -1;
}
*/
/* Prepare ESPNOW data to be sent. */
/*
void satellite_data_prepare(satellite_send_param_t *send_param)
{
    satellite_data_t *buf = (satellite_data_t *)send_param->buffer;

    assert(send_param->len >= sizeof(satellite_data_t));

    buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? EXAMPLE_ESPNOW_DATA_BROADCAST : EXAMPLE_ESPNOW_DATA_UNICAST;
    buf->state = send_param->state;
    buf->seq_num = i++;
    buf->crc = 0;
    buf->magic = send_param->magic;
    // Fill all remaining bytes after the data with random values 
    esp_fill_random(buf->payload, send_param->len - sizeof(satellite_data_t));
    buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}
*/
//static QueueHandle_t satellite_data_queue;
    /*
  uint8_t from;
  uint8_t destaddr;
  packet_t pkt;
  packet_t pktOut;
  uint8_t len = sizeof(pkt);
  unsigned long timeoutMillis = 0;                 // timeout counter, timeouts after RFM69_RECEIVE_TIMEOUT milliseconds (see defines.h)
  unsigned long timeoutStart = 0;                  // timeout initial values in millis, equals millis() function
  bool received = false;

  timeoutMillis = millis();
  timeoutStart = timeoutMillis;
  while ((!(timeoutMillis > (timeoutStart + RFM69_RECEIVE_TIMEOUT))) && !received) {
    if (rf69_manager.available()) {
      rf69_manager.recvfromAck(recv_buf, &len, &from, &destaddr);
      if (len == sizeof(pkt)) {
        
        DEBUG_STR("Received data: ");
        for(int i=0;i<len;i++) {
          DEBUG_UCH(recv_buf[i],HEX);
          DEBUG_STR(" ");
        }
        DEBUG_STRLN("");
        
        memcpy(&pkt,recv_buf,sizeof(pkt));
        if (macAddressCmp(pkt.zoneid,sensor.zoneid)) {
          if (sensor.pktnum > 65534)
            sensor.pktnum = 1;
          else
            sensor.pktnum++;
          switch (pkt.type) {
            case PT_ADDRREQ_REPLY:
                sensor.src_addr = pkt.src_addr;
                sensor.dest_addr = pkt.dest_addr;
                sensor.rssi = rf69.lastRssi();
                rf69_manager.setThisAddress(pkt.src_addr);
                if (sendMessage(PT_ADDRREQ_ACK, sensor.dest_addr) == 0) 
                  return 1;
                return 0;  
              break;
            case PT_TELEMETRY_ACK:
            case PT_ALARM_REPLY:
              sensor.rssi = rf69.lastRssi();
              sensor.restart = pkt.restart;
              return 1;  
              break;
            default:
              break;
          }
        }
      }
    }
    timeoutMillis = millis();
  }
  if (!received) {
    if (sensor.timeouts > 65534)
      sensor.timeouts = 1;
    else
      sensor.timeouts++;
    return 0;
  }
  */

/*
static sat_retcode_t satellite_old_espnow_init(void)
{
    satellite_send_param_t *send_param;
    satellite_send_packet_t *sat_send_param;

    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) {
        ESP_LOGW(TAG, "Malloc peer information fail");
        vSemaphoreDelete(satellite_data_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcastAddress, ESP_NOW_ETH_ALEN);
    espnowError =  esp_now_add_peer(peer);
    free(peer);

    send_param = malloc(sizeof(satellite_send_param_t));
    sat_send_param = malloc(sizeof(satellite_send_packet_t));
    memset(send_param, 0, sizeof(satellite_send_param_t));
    memset(sat_send_param, 0, sizeof(satellite_send_packet_t));
    if (send_param == NULL) {
        ESP_LOGW(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(satellite_data_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    if (sat_send_param == NULL) {
        ESP_LOGW(TAG, "Malloc send parameter fail");
        vSemaphoreDelete(satellite_data_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    sat_send_param->type = PT_ADDRREQ;
    sat_send_param->temp = 0;
    sat_send_param->humi = 0;
    sat_send_param->pres = 0;
    sat_send_param->uptime = 0;
    sat_send_param->pktnum = 0;
    sat_send_param->reconnections = 0;
    sat_send_param->rssi = 0;
    sat_send_param->timeouts = 0;
    sat_send_param->crc = 0;
    memcpy(sat_send_param->dest_addr, broadcastAddress, ESP_NOW_ETH_ALEN);
    esp_read_mac(sat_send_param->src_addr, ESP_MAC_WIFI_STA);       
    sat_send_param->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)sat_send_param, sizeof(satellite_send_packet_t));

    send_param->unicast = false;
    send_param->broadcast = true;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = CONFIG_ESPNOW_SEND_COUNT;
    send_param->delay = CONFIG_ESPNOW_SEND_DELAY;
    send_param->len = CONFIG_ESPNOW_SEND_LEN;
    send_param->buffer = malloc(CONFIG_ESPNOW_SEND_LEN);
    if (send_param->buffer == NULL) {
        ESP_LOGW(TAG, "Malloc send buffer fail");
        free(send_param);
        vSemaphoreDelete(satellite_data_queue);
        esp_now_deinit();
        return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, broadcastAddress, ESP_NOW_ETH_ALEN);
    satellite_data_prepare(send_param);

    xTaskCreate(satellite_task, "satellite task", 2048, sat_send_param, 4, NULL);

    return ESP_OK;
}
*/
/*
static void satellite_deinit(satellite_send_packet_t *send_param)
{
    free(send_param);
    vSemaphoreDelete(satellite_data_queue);
    esp_now_deinit();
}
*/
/*
void anotherTask( void * parameter ) {
    uint32_t rnd;
    const TickType_t xDelay = 10000 / portTICK_PERIOD_MS;

    for(;;) {
        rnd = esp_random();
        if (rnd%11 == 0) {
            ESP_LOGW(TAG, "info 1 %d", rnd);
            if( satellite_data_queue != 0 )
            {
                // Send an uint32_t.  Wait for 10 ticks for space to become
                // available if necessary.
                if( xQueueSend( satellite_data_queue, ( void * ) &rnd, ( TickType_t ) 10 ) != pdPASS )
                {
                    // Failed to post the message, even after 10 ticks.
                }
            }
            vTaskDelay( xDelay );
        }
    }
}


    const TickType_t xDelay = 10000 / portTICK_PERIOD_MS;
    vTaskDelay( xDelay );

*/

