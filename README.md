# Planet

An esp32 master, collects data from sensor and send them to a planet

at the moment the following sensors are supported:\
  dht22   use the "esp32dht" environment in platformio.ini default_env\
  ds18b20 use the "esp32ds18b20" environment in platformio.ini default_env\
  bmp280  use the "esp32bmp280" environment in platformio.ini default_env\
  devnull use the "esp32devnull" environment in platformio.ini default_env\

note: devnull is a random number generator for people without sensors\
  
prerequisites:\
  vs-code\
  platformio\
  esp32 rtos sdk\

clone this repository, use platformio.ini.sample as a base for you platformio.ini\

adjust platformio.ini\
uncomment the environment you want to compile for\
[platformio]\
default_envs = esp32devnull\
;default_envs = esp32devnull, esp32dht, esp32ds18b20, esp32bmp280\

[env]\
platform = espressif32\
board = esp32dev\
framework = espidf\
lib_ldf_mode = chain+\
monitor_speed = 115200\
monitor_filters =\ 
	direct\
	esp32_exception_decoder\
monitor_flags = --raw\

[env:esp32devnull]\
upload_port = COM19\
build_flags = -D DEVNULL\

[env:esp32dht]\
upload_port = COM23\
build_flags = -D DHTXX\

[env:esp32ds18b20]\
upload_port = COM17\
build_flags = -D DALLASTEMP\

[env:esp32bmp280]\
upload_port = COM27\
build_flags = -D BMP280\

compile and upload\

Created by aRGi (info@argi.mooo.com).
