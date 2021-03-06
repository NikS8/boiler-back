/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                            boiler-reserve.ino 
                                        Copyright © 2018-2019, Zigfred & Nik.S
19.12.2018 v1
03.01.2019 v2 dell <ArduinoJson.h>
10.01.2019 v3 изменен расчет в YF-B5
13.01.2019 v4 createDataString в формате json
15.01.2019 v5 дабавлены данные по температуре коллектора
16.01.2019 v6 обозначены места расположения датчиков температуры
17.01.2019 v7 в именах датчиков температуры последние 2 цифры
19.01.2019 v8 нумерация контуров коллектора слева направо  
03.02.2019 v9 преобразование в формат  F("")
04.02.2019 v10 добавлена функция freeRam()
04.02.2019 v11 добавлены ds18 коллектора
04.02.2019 v12 добавленa "data"
06.02.2019 v13 переменным добавлен префикс "boiler-back-"
06.02.2019 v14 изменение вывода №№ DS18 и префикс заменен на "bb-"
07.02.2019 v15 удалено все по логсервису
22.02.2019 v16 dell requestTime
09.03.2019 v17 новая плата и новые трансформаторы тока (откалиброваны)
10.03.2019 v18 время работы после включения
13.11.2019 v19 переход на статические IP и префикс заменен на "br-"
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*****************************************************************************\
Сервер boilerBack выдает данные: 
  аналоговые: 
    датчики трансформаторы тока  
  цифровые: 
    датчик скорости потока воды YF-B5
    датчики температуры DS18B20
/*****************************************************************************/

#include <Ethernet2.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>

#define DEVICE_ID "boiler-reserve"
#define VERSION 19

#define RESET_UPTIME_TIME 2592000000  //  =30 * 24 * 60 * 60 * 1000 
                                      // reset after 30 days uptime
byte mac[] = {0xCA, 0x74, 0xC0, 0xFF, 0xBE, 0x01};
IPAddress ip(192, 168, 1, 112);
EthernetServer httpServer(40112); // Ethernet server


#define PIN_TRANS_1 A1
#define PIN_TRANS_2 A2
#define PIN_TRANS_3 A3
EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define DS18_CONVERSION_TIME 750 // (1 << (12 - ds18Precision))
#define PIN8_ONE_WIRE_BUS 8           //  коллектор
unsigned short ds18DeviceCount8;
OneWire ds18wireBus8(PIN8_ONE_WIRE_BUS);
DallasTemperature ds18Sensors8(&ds18wireBus8);
#define PIN9_ONE_WIRE_BUS 9           //  бойлер
unsigned short ds18DeviceCount9;
OneWire ds18wireBus9(PIN9_ONE_WIRE_BUS);
DallasTemperature ds18Sensors9(&ds18wireBus9);
uint8_t ds18Precision = 11;

#define PIN_FLOW_SENSOR 2
#define PIN_INTERRUPT_FLOW_SENSOR 0
#define FLOW_SENSOR_CALIBRATION_FACTOR 5
unsigned long flowSensorLastTime = 0;
volatile long flowSensorPulseCount = 0;

// time
unsigned long currentTime;
// timers
RBD::Timer ds18ConversionTimer;

// TODO optimize variables data length
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
              setup
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void setup()
{
  Serial.begin(9600);
  Serial.println("Serial.begin(9600)"); 

  Ethernet.begin(mac,ip);
  
  Serial.println(F("Server is ready."));
  Serial.print(F("Please connect to http://"));
  Serial.println(Ethernet.localIP());
  
  httpServer.begin();

  pinMode(PIN_TRANS_1, INPUT);
  pinMode(PIN_TRANS_2, INPUT);
  pinMode(PIN_TRANS_3, INPUT);

  emon1.current(1, 8.8);
  emon2.current(2, 8.8);
  emon3.current(3, 8.8);
/*
  pinMode(flowSensorPin, INPUT);
 // digitalWrite(flowSensorPin, HIGH);
  attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, RISING);
  sei();
*/
  pinMode(PIN_FLOW_SENSOR, INPUT);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);
  sei();

  ds18Sensors8.begin();
  ds18DeviceCount8 = ds18Sensors8.getDeviceCount();
  ds18Sensors9.begin();
  ds18DeviceCount9 = ds18Sensors9.getDeviceCount();

  getSettings();
}
  // TODO create function for request temp and save last request time

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function getSettings
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void getSettings()
{
  ds18Sensors8.requestTemperatures();
  ds18Sensors9.requestTemperatures();
  ds18ConversionTimer.setTimeout(DS18_CONVERSION_TIME);
  ds18ConversionTimer.restart();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            loop
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void loop()
{
//  currentTime = millis();
  resetWhen30Days();

  realTimeService();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function realTimeService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void realTimeService()  {

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;
  ds18RequestTemperatures();
  currentTime = millis();
  while (reqClient.available()) reqClient.read();

  String data = createDataString();

  reqClient.println(F("HTTP/1.1 200 OK"));
  reqClient.println(F("Content-Type: application/json"));
  reqClient.print(F("Content-Length: "));
  reqClient.println(data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function ds18RequestTemperatures
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void ds18RequestTemperatures()
{
  if (ds18ConversionTimer.onRestart()) {
    ds18Sensors8.requestTemperatures();
    ds18Sensors9.requestTemperatures();
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function flowSensorPulseCounter
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void flowSensorPulseCounter()
{
  // Increment the pulse counter
  flowSensorPulseCount++;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function createDataString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String createDataString()
{
  String resultData;

  resultData.concat(F("{"));
  resultData.concat(F("\n\"deviceId\":"));
//  resultData.concat(String(DEVICE_ID));
  resultData.concat(F("\"boiler-reserve\""));
  resultData.concat(F(","));
  resultData.concat(F("\n\"version\":"));
  resultData.concat(VERSION);

  resultData.concat(F(","));
  resultData.concat(F("\n\"data\": {"));

  resultData.concat(F("\n\t\"br-trans-1\":"));
  resultData.concat(String((float)emon1.calcIrms(1480), 1));
  resultData.concat(F(","));
  resultData.concat(F("\n\t\"br-trans-2\":"));
  resultData.concat(String((float)emon2.calcIrms(1480), 1));
  resultData.concat(F(","));
  resultData.concat(F("\n\t\"br-trans-3\":"));
  resultData.concat(String((float)emon3.calcIrms(1480), 1));
  
  resultData.concat(F(","));
  for (uint8_t index9 = 0; index9 < ds18DeviceCount9; index9++)
  {
    DeviceAddress deviceAddress9;
    ds18Sensors9.getAddress(deviceAddress9, index9);
    resultData.concat(F("\n\t\""));
    for (uint8_t i = 0; i < 8; i++)
    {
      if (deviceAddress9[i] < 16) resultData.concat("0");

      resultData.concat(String(deviceAddress9[i], HEX));
    }
    resultData.concat(F("\":"));
    resultData.concat(ds18Sensors9.getTempC(deviceAddress9));
    resultData.concat(F(","));
  }

  for (uint8_t index8 = 0; index8 < ds18DeviceCount8; index8++)
  {
    DeviceAddress deviceAddress8;
    ds18Sensors8.getAddress(deviceAddress8, index8);
        resultData.concat(F("\n\t\""));
    for (uint8_t i = 0; i < 8; i++)
    {
      if (deviceAddress8[i] < 16) resultData.concat("0");

      resultData.concat(String(deviceAddress8[i], HEX));
    }
    resultData.concat(F("\":"));
    resultData.concat(ds18Sensors8.getTempC(deviceAddress8));
    resultData.concat(F(","));
  }
  
  resultData.concat(F("\n\t\"br-flow\":"));
  resultData.concat(getFlowData());
  
  resultData.concat(F("\n\t }"));
  resultData.concat(F(","));
  resultData.concat(F("\n\"freeRam\":"));
  resultData.concat(freeRam());
  resultData.concat(F(",\n\"upTime\":\""));
  resultData.concat(upTime(millis()));
  resultData.concat(F("\"\n }"));
 // resultData.concat(F("}"));

  return resultData;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function to measurement flow water
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int getFlowData()
{
  //  static int flowSensorPulsesPerSecond;
  unsigned long flowSensorPulsesPerSecond;

  unsigned long deltaTime = millis() - flowSensorLastTime;
  //  if ((millis() - flowSensorLastTime) < 1000) {
  if (deltaTime < 1000)
  {
    return;
  }

  //detachInterrupt(flowSensorInterrupt);
  detachInterrupt(PIN_INTERRUPT_FLOW_SENSOR);
  flowSensorPulsesPerSecond = flowSensorPulseCount;
  flowSensorPulsesPerSecond *= 1000;
  flowSensorPulsesPerSecond /= deltaTime; //  количество за секунду

  flowSensorLastTime = millis();
  flowSensorPulseCount = 0;
  //attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, FALLING);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);

  return flowSensorPulsesPerSecond;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            function resetWhen30Days
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void resetWhen30Days()
{
  if (millis() > (RESET_UPTIME_TIME))
  {
    // do reset
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            Время работы после старта или рестарта
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String upTime(uint32_t lasttime)
{
  lasttime /= 1000;
  String lastStartTime;
  
  if (lasttime > 86400) {
    uint8_t lasthour = lasttime/86400;
    lastStartTime.concat(lasthour);
    lastStartTime.concat(F("d "));
    lasttime = (lasttime-(86400*lasthour));
  }
  if (lasttime > 3600) {
    if (lasttime/3600<10) { lastStartTime.concat(F("0")); }
  lastStartTime.concat(lasttime/3600);
  lastStartTime.concat(F(":"));
  }
  if (lasttime/60%60<10) { lastStartTime.concat(F("0")); }
lastStartTime.concat((lasttime/60)%60);
lastStartTime.concat(F(":"));
  if (lasttime%60<10) { lastStartTime.concat(F("0")); }
lastStartTime.concat(lasttime%60);
//lastStartTime.concat(F("s"));

return lastStartTime;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            Количество свободной памяти
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int freeRam()
{
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            end
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
