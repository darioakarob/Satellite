#define uSEC_TO_SEC        1000000  /* Conversion factor for micro seconds to seconds */

#define SATELLITE_RETRY 3
#define SATELLITE_QUEUE_SIZE 20
#define SATELLITE_SLEEP_TIME 5
#define SATELLITE_SLEEP_TIME_ON_ERROR 1 * uSEC_TO_SEC
#define SATELLITE_SEND_DELAY 128
#define SATELLITE_SEND_MAXWAIT uSEC_TO_SEC
#define SATELLITE_RECEIVE_MAXWAIT uSEC_TO_SEC

#define SATELLITE_PACKET_MAXLEN   250
#define SATELLITE_CHANNELS        15
#define SATELLITE_DEFAULT_CHANNEL 1

//#define SATELLITE_QUEUE_MAXDELAY 1024

static uint8_t wifiChannels[SATELLITE_CHANNELS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};

static uint8_t broadcastAddress[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define IS_BROADCAST_ADDR(addr) (memcmp(addr, broadcastAddress, ESP_NOW_ETH_ALEN) == 0)
#undef MACSTR
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

typedef struct {
    esp_err_t code;
    char      desc[50];
} sat_retcode_t;

enum PacketTypes_e {
  PT_ADDRREQ         =  0b00000100,
  PT_ADDRREQ_OFFER   =  0b00000101,
  PT_ADDRREQ_REQ     =  0b00000110,
  PT_ADDRREQ_ACK     =  0b00000111,
  PT_TELEMETRY       =  0b00001100,
  PT_TELEMETRY_ACK   =  0b00001101,
};

typedef struct {
    uint8_t  state;
    uint8_t  local_addr[ESP_NOW_ETH_ALEN];
    uint8_t  dest_addr[ESP_NOW_ETH_ALEN];
    uint8_t  channel;
    float temp;
    float humi;
    float pres;
    uint16_t uptime;
    uint16_t pktnum;
    int16_t  rssi;
    uint8_t  receive;
    uint16_t receive_timeouts;
    uint16_t receive_errors;
    uint8_t  send;
    uint16_t send_timeouts;
    uint16_t send_errors;
    uint16_t err_code;
    uint8_t  err_text[128];
    volatile unsigned long  tmstart;
    volatile unsigned long  tmelapsed; 
    volatile unsigned long  tmduration; 
} satellite_config_t;

typedef struct {
    uint8_t  type;
    uint8_t  src_addr[ESP_NOW_ETH_ALEN];
    uint8_t  dest_addr[ESP_NOW_ETH_ALEN];
} satellite_packet_head_t;

typedef struct {
    uint8_t state;
    uint8_t command;
    uint16_t uptime;
    uint16_t pktnum;
    int16_t  rssi;
    uint16_t receive_timeouts;
    uint16_t receive_errors;
    uint16_t send_timeouts;
    uint16_t send_errors;
    uint16_t err_code;
    uint8_t  err_text[40];
} satellite_packet_payload_t;

typedef struct {
    float temp;
    float humi;
    float pres;
} satellite_packet_payload_data_t;

#ifndef DESP8266
RTC_DATA_ATTR static bool               hasReboot = true;    // flag to verify is the chip has boot/reset or not
RTC_DATA_ATTR static satellite_config_t satellite;
#else
static bool               hasReboot = true;    // flag to verify is the chip has boot/reset or not
static satellite_config_t satellite;
#endif
uint8_t* receive_buffer;
uint8_t* send_buffer;
