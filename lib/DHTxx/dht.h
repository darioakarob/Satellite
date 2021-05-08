#ifndef DHT_H_  
#define DHT_H_

#define DHT_DATA_HBITS 41

enum DHTMachineStates_e {
  DHT_START    =  0,
  DHT_RELEASE      = 10,
  DHT_RELEASE_END  = 20,
  DHT_ACK      = 30,
  DHT_RECV_BEG = 40,
  DHT_RECV_END = 50,
  DHT_IDLE     = 60,
  DHT_TIMEOUT  = 97,
  DHT_STOP     = 98,
  DHT_UNAVAIL  = 99,
};

enum DHTMachineErrors_e {
  DHT_ERR_OK  =  0,
  DHT_ERR_PIN =  1,
  DHT_ERR_ACK =  2,
  DHT_ERR_CKS =  3,
  DHT_ERR_TMO =  4,
};

/*
void dhtISR (void*);
int dhtStartup(gpio_num_t dhtpin);
enum DHTMachineStates_e getstatus();
int geterror();
float gettempc();
float gettempf();
float gethumi();
int   getBuffLen();
enum DHTMachineStates_e setstatus(enum DHTMachineStates_e command);
int settimeout(int val);
int cleartimeout();
void dhtmachineLoop();
float * dhtTask( void );
*/
    
//static void dhtPin13(void);
esp_err_t attachISR(void);
esp_err_t detachISR(void);
int decodeDHT(void);
enum DHTMachineStates_e state;
volatile unsigned long  tmstart;
volatile unsigned long  tmelapsed; 
volatile unsigned long  tmduration; 
volatile unsigned int   databuflen; 
volatile unsigned long  databuf[DHT_DATA_HBITS]; 
gpio_num_t              dhtDataPin;
int                     datapin; 
int                     errCode;
int                     timeout;
float                   temp;
float                   humi;
float                   pres;
 
#endif
