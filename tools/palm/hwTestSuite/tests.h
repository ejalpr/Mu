#ifndef TESTS_H
#define TESTS_H

#include "testSuite.h"

var testButtonInput();
var listDataRegisters();
var listRegisterFunctions();
var listRegisterDirections();
var checkSpi2EnableBitDelay();
var tstat1GetSemaphoreLockOrder();
var ads7846Read();
var ads7846ReadOsVersion();
var getClk32Frequency();
var getDeviceInfo();
var getCpuInfo();
var getInterruptInfo();
var getIcrInversion();
var doesIsrClearChangePinValue();
var toggleBacklight();
var toggleMotor();
var toggleAlarmLed();
var watchPenIrq();
var getPenPosition();
var playConstantTone();
var unaligned32bitAccess();

#endif
