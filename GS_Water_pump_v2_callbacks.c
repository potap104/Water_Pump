#include "app/framework/include/af.h"
#include "hal/hal.h"
#include EMBER_AF_API_NETWORK_STEERING
#include "app/framework/include/af.h"
#include "app/util/ezsp/ezsp-enum.h"
#include "em_timer.h"
#include "em_device.h"
#include "em_cmu.h"
#include "em_chip.h"
#include "em_common.h"
#include "em_adc.h"
#include "em_gpio.h"

#define level_Clus 0x0008
#define flow_Clus 0x0404
#define adcData_attr 0x0000
#define pump_timer_attr 0x0011
#define mode_attr 0x0000


//События
EmberEventControl DelayEventData;
EmberEventControl TimerModeventData;
EmberEventControl ADC_eventData;
EmberEventControl LedBlink_eventData;

// Режимы работы
enum mode{
DO_NOTHING,
TIMER_MODE,
ADC_MODE,
RESET,
};
static uint8_t mode_flag = 0;

#define DELAY_IN_MS 1000
static int PumpDelay = 3;               // Время работы насоса
static uint8_t TimerPumpDelay = 60;     // Таймер включения
static uint8_t MaxTimerPumpDelay = 255;
static uint8_t MinTimerPumpDelay = 60;
static int ADC_Delay = 100;             // Таймер датчика
static int Porog = 40;                  // Пороговое значение датчика


// Инициализация АЦП
uint32_t adcData = 0;
ADC_InitSingle_TypeDef initSingle = ADC_INITSINGLE_DEFAULT;

// Запуск АЦП
void Get_ADC_Data(){
  uint8_t attr_data = 0;
  CMU_ClockEnable( cmuClock_ADC0, true );
  ADC_InitSingle(ADC0, &initSingle);
  ADC_Start(ADC0, adcStartSingle);
  while((ADC_IntGet(ADC0) & ADC_IF_SINGLE) != ADC_IF_SINGLE);
  adcData = ADC_DataSingleGet(ADC0);
  adcData = 100-(((adcData-2048)/10)*1.5);
  emberAfCorePrintln("Humidity: %d%% \r\n",adcData);
  attr_data = adcData;
  emberAfWriteServerAttribute(1,level_Clus,adcData_attr,&attr_data,1);
  CMU_ClockEnable( cmuClock_ADC0, false);
}

void Mode_changing(){
  // Запись режима в атрибут
  mode_flag++;
  emberAfWriteServerAttribute(1,flow_Clus,mode_attr,&mode_flag,1);
  switch (mode_flag)
    {
    // Ручной режим
    case DO_NOTHING:
      break;

    // Полив по таймеру
    case TIMER_MODE:
      emberEventControlSetDelayMS(TimerModeventData, TimerPumpDelay*DELAY_IN_MS);
          break;

    // Полив по датчику
    case ADC_MODE:
      emberEventControlSetInactive(TimerModeventData);
      emberEventControlSetInactive(DelayEventData);
      GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,0);
      emberEventControlSetDelayMS(ADC_eventData, ADC_Delay*DELAY_IN_MS);
          break;

    // Возврат к ручному режиму
    case RESET:
      emberEventControlSetInactive(TimerModeventData);
      emberEventControlSetInactive(DelayEventData);
      emberEventControlSetInactive(ADC_eventData);
      GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,0);
      mode_flag = 0;
          break;
    }
}

int i = 0;
void LedBlink_eventHandler(void)
{
  if (i<6){
  emberEventControlSetInactive(LedBlink_eventData);
  GPIO_PinOutToggle(gpioPortC,10);
  emberEventControlSetDelayMS(LedBlink_eventData,500);
  i++;
  }
  else{
  i = 0;
  GPIO_PinModeSet(gpioPortC,10,gpioModePushPull,0);
  emberEventControlSetInactive(LedBlink_eventData);
  }
}

// Событие отключения полива
void DelayEventHandler(void)
{
  emberEventControlSetInactive(DelayEventData);
  GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,0);
}

// Событие полива по таймеру
void TimerModeventHandler(void)
{
  emberEventControlSetInactive(TimerModeventData);
  emberEventControlSetDelayMS(DelayEventData, PumpDelay*DELAY_IN_MS);
  GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,1);
  emberEventControlSetDelayMS(TimerModeventData, TimerPumpDelay*DELAY_IN_MS);
}

// Событие полива по датчику
void ADC_eventHandler(void)
{
  emberEventControlSetInactive(ADC_eventData);
  Get_ADC_Data();
  // Проверка уровня влажности
  if(adcData<Porog){
      emberEventControlSetDelayMS(DelayEventData, PumpDelay*DELAY_IN_MS);
      GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,1);
  }
  emberEventControlSetDelayMS(ADC_eventData, ADC_Delay*DELAY_IN_MS);
}


void emberAfMainInitCallback(void)
{
  // Проверка кнопки сброса
  int reset_flag = 1;
  reset_flag = GPIO_PinInGet(gpioPortD,14);
  if (reset_flag == 0){
    emberLeaveNetwork();
    emberAfAppPrintln("reset %d", reset_flag);
  }

  GPIO_PinModeSet(gpioPortA,6,gpioModeInput,0);     // Вход датчика
  GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,0);  // насос выкл
  GPIO_PinModeSet(gpioPortC,10,gpioModePushPull,0); // диод выкл

  // Настройка АЦП
  initSingle.reference =  adcRef5V;
  initSingle.acqTime = adcAcqTime4;
  initSingle.posSel = adcPosSelAPORT3XCH14;
  initSingle.negSel = adcNegSelVSS;
}


// При длительном нажатии подключаться к сети
void emberAfPluginButtonInterfaceButton0PressedLongCallback(uint16_t timePressedMs,bool pressedAtReset)
{
  if (emberAfNetworkState() == EMBER_JOINED_NETWORK){
      GPIO_PinModeSet(gpioPortC,10,gpioModePushPull,1);
      emberEventControlSetDelayMS(LedBlink_eventData,500);
  }
  else {
      emberAfPluginNetworkSteeringStart();
      GPIO_PinModeSet(gpioPortC,10,gpioModePushPull,1);
  }
}
// Индикация подключения
void emberAfPluginNetworkSteeringCompleteCallback(EmberStatus status,uint8_t totalBeacons,uint8_t joinAttempts,uint8_t finalState)
{
  if (status != EMBER_SUCCESS){
  GPIO_PinModeSet(gpioPortC,10,gpioModePushPull,0);
  }
  else{
  emberEventControlSetDelayMS(LedBlink_eventData,500);

  }
}


// изменение частоты полива кнопкой
void emberAfPluginButtonInterfaceButton1PressedShortCallback(uint16_t timePressedMs)
{
  TimerPumpDelay = TimerPumpDelay + 60;
  if (TimerPumpDelay > MaxTimerPumpDelay){
      TimerPumpDelay = MinTimerPumpDelay;
  }
  // Запись частоты полива в атрибут
  emberAfWriteServerAttribute(1,level_Clus,pump_timer_attr,&TimerPumpDelay,1);
}

// изменение частоты полива командой
boolean emberAfLevelControlClusterMoveToLevelCallback(int8u level,int16u transitionTime,int8u optionMask,int8u optionOverride)
{
  TimerPumpDelay = level;
  if (TimerPumpDelay > MaxTimerPumpDelay){
      TimerPumpDelay = MaxTimerPumpDelay;
  }
  if (TimerPumpDelay < MinTimerPumpDelay){
      TimerPumpDelay = MinTimerPumpDelay;
  }
  // Запись частоты полива в атрибут
  emberAfWriteServerAttribute(1,level_Clus,pump_timer_attr,&TimerPumpDelay,1);
  return FALSE;
}


// Одиночный полив кнопкой
void emberAfPluginButtonInterfaceButton1PressedLongCallback(uint16_t timePressedMs,bool pressedAtReset)
{
  // Проверка датчика влажности
  Get_ADC_Data();
  emberEventControlSetDelayMS(DelayEventData, PumpDelay*DELAY_IN_MS);
  if(adcData<Porog){
    emberEventControlSetDelayMS(DelayEventData, PumpDelay*DELAY_IN_MS);
    GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,1);
  }
}

// Одиночный полив командой
boolean emberAfOnOffClusterOnCallback(void)
{
  emberEventControlSetDelayMS(DelayEventData, PumpDelay*DELAY_IN_MS);
  GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,1);
  return FALSE;
}

// Отключение полива/проверка датчика
boolean emberAfOnOffClusterOffCallback(void)
{
  Get_ADC_Data();
  GPIO_PinModeSet(gpioPortA,8,gpioModePushPull,0);
  return FALSE;
}


// Смена режимов работы кнопкой
void emberAfPluginButtonInterfaceButton0PressedShortCallback(uint16_t timePressedMs)
{
  Mode_changing();
}

// Смена режимов работы командой
boolean emberAfOnOffClusterToggleCallback(void)
{
  Get_ADC_Data();
  Mode_changing();
  return FALSE;
}

EmberAfStatus emberAfOnOffClusterSetValueCallback(int8u endpoint,int8u command,boolean initiatedByLevelChange)
{
  return EMBER_ZCL_STATUS_UNSUP_COMMAND;
}

