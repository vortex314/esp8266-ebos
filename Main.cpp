#include "esp/uart.h"
#include "espressif/esp_common.h"

#include <string.h>

#include <FreeRTOS.h>
#include <ssid_config.h>
#include <task.h>

#include <espressif/esp_sta.h>
#include <espressif/esp_wifi.h>

#include <paho_mqtt_c/MQTTClient.h>
#include <paho_mqtt_c/MQTTESP8266.h>

#include <semphr.h>

#include "esp8266.h"

#include <EventBus.h>
#include <Log.h>
#include <esp/gpio.h>

#include <ssid_config.h>

Log logger(128);
EventBus eb(1024, 256);
Uid uid(100);

extern "C" void __cxa_pure_virtual()
{
  ERROR(" called a pure virtual function ");
  vTaskDelay(1000);
  while (1)
    ;
}

class LedBlinker : public Actor
{
  bool _isOn;
  uint32_t _gpio = 2;
  uint32_t _gpio2 = 16;
  uint32_t _interval;

public:
  LedBlinker(const char *name) : Actor(name)
  {
    _isOn = false;
    _gpio = 2;
    _gpio2 = 16;
    _interval = 100;
  };
  void setup()
  {
    gpio_enable(_gpio, GPIO_OUTPUT);
    gpio_enable(_gpio2, GPIO_OUTPUT);
    eb.onDst(id()).call(this);
    eb.onSrc(H("wifi")).call(this, (MethodHandler)&LedBlinker::changeInterval);
    timeout(100);
  };
  void onEvent(Cbor &msg)
  {
    if (_isOn)
    {
      gpio_write(_gpio, 0);
      gpio_write(_gpio2, 1);
      _isOn = false;
    }
    else
    {
      gpio_write(_gpio, 1);
      gpio_write(_gpio2, 0);
      _isOn = true;
    }
    timeout(_interval);
  }
  void changeInterval(Cbor &msg)
  {
    uid_t event;
    if (msg.getKeyValue(EB_EVENT, event))
    {
      if (event == H("connected"))
        _interval = 1000;
      if (event == H("disconnected"))
        _interval = 100;
    }
  }
};

// #define ZERO(x) memset(&x, 0, sizeof(x))

class Wifi : public Actor
{
  uint8_t status = 0;
  struct sdk_station_config config;

public:
  Wifi(const char *name) : Actor(name){};

  void setup()
  {
    ZERO(config);
    strcpy((char *)config.ssid, WIFI_SSID);
    strcpy((char *)config.password, WIFI_PASS);
    printf("WiFi: connecting to WiFi\n\r");
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
    timeout(1000);
    state(uid.add("disconnected"));
    uid.add("connected");
  };

  void onEvent(Cbor &msg)
  {
    bool stateChanged = false;
    status = sdk_wifi_station_get_connect_status();
    // INFO("%s: status = %d", __func__, status);
    if (status == STATION_GOT_IP)
    {
      stateChanged = state(H("connected"));
      timeout(1000);
    }
    else
    {
      stateChanged = state(H("disconnected"));
      if (status == STATION_WRONG_PASSWORD)
      {
        printf("WiFi: wrong password\n\r");
      }
      else if (status == STATION_NO_AP_FOUND)
      {
        printf("WiFi: AP not found\n\r");
      }
      else if (status == STATION_CONNECT_FAIL)
      {
        printf("WiFi: connection failed\r\n");
      }
      if (stateChanged)
      {
        sdk_wifi_station_disconnect();
      }
      timeout(1000);
    };
  }
};

class Monitor : public Actor
{
public:
  Monitor(const char *name) : Actor(name) {}
  void setup() { timeout(1000); }
  void onEvent(Cbor &evt)
  {
    //TickType_t xLastWakeTime = xTaskGetTickCount();
    //    INFO(" freertos config configTICK_RATE_HZ : %d", configTICK_RATE_HZ);
    //    INFO("millis : %lu ", Sys::millis());
    INFO("heap : %d bytes , stack not used : %d bytes ", Sys::getFreeHeap(), uxTaskGetStackHighWaterMark(NULL) * 4);
    //    INFO("micros : %llu ", Sys::micros());
    //    INFO("lastWakeTime : %d / %d ", xLastWakeTime, xTaskGetTickCount());
    timeout(5000);
  }
};

Wifi wifi("wifi");
Monitor monitor("monitor");
LedBlinker led("led");
Str strLog(128);

static void eventLoop_task(void *pvParameters)
{
  //
  Sys::init();
  logger.level(Log::LOG_INFO);
  wifi.setup();
  monitor.setup();
  led.setup();
  eb.onAny().call([](Cbor &msg) { // Log all events -> first handler
    eb.log(strLog, msg);
    INFO("%s", strLog.c_str());
  });

  while (1)
  {
    eb.eventLoop();
    Cbor cbor(10);
    taskYIELD();
    // printf(" hello \n");
    // monitor.onEvent(cbor); ////
  }
}

extern "C" void user_init(void)
{
  uart_set_baud(0, 115200);
  printf("SDK version:%s\n", sdk_system_get_sdk_version());
  xTaskCreate(&eventLoop_task, "eventLoop_task", 1024, NULL, 1, NULL);
}
