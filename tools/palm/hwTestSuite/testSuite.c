#include <PalmOS.h>
#include <stdint.h>

#include "ugui.h"
#include "testSuiteConfig.h"
#include "testSuite.h"
#include "viewer.h"
#include "debug.h"
#include "tools.h"

/*dont include this anywhere else*/
#include "TstSuiteRsc.h"


/*exported functions, cant be converted into macros*/
var makeVar(uint8_t length, uint8_t type, uint64_t value){
   var newVar;
   newVar.type = length & 0xF0 | type & 0x0F;
   newVar.value = value;
   return newVar;
}

static char floatString[maxStrIToALen * 2 + 3];/*2 ints and "-.\0"*/
char* floatToString(float data){
   if(data == 1.0 / 0.0){
      StrCopy(floatString, "+INF");
   }
   else if(data == -(1.0 / 0.0)){
      StrCopy(floatString, "-INF");
   }
   else if(data == 0.0){
      StrCopy(floatString, "0.0");
   }
   else{
      char convertBuffer[maxStrIToALen];
      Boolean negative;
      
      if(data < 0.0){
         negative = true;
         data = -data;
      }
      else{
         negative = false;
      }
      
      floatString[0] = '\0';
      data = abs(data);
      StrIToA(convertBuffer, (int32_t)data);
      if(negative)
         StrCat(floatString, "-");
      StrCat(floatString, convertBuffer);
      StrCat(floatString, ".");
      data -= (int32_t)data;
      data *= 1000000000;
      StrIToA(convertBuffer, (int32_t)data);
      StrCat(floatString, convertBuffer);
   }
   
   return floatString;
}

/*exports*/
uint16_t palmButtons;
uint16_t palmButtonsLastFrame;
Boolean  isM515;
Boolean  isT3;
Boolean  skipFrameDelay;
uint8_t* sharedDataBuffer;

/*video*/
static UG_GUI      uguiStruct;
static BitmapType* offscreenBitmap;
static uint8_t*    framebuffer;

/*other*/
static activity_t parentSubprograms[MAX_SUBPROGRAMS];
static uint32_t   subprogramIndex;
static activity_t currentSubprogram;
static var        subprogramData[MAX_SUBPROGRAMS];
static var        subprogramArgs;/*optional arguments when one subprogram calls another*/
static var        lastSubprogramReturnValue;
static Boolean    subprogramArgsSet;
static Boolean    applicationRunning;


static var errorSubprogramStackOverflow(void){
   static Boolean wipedScreen = false;
   
   if(!wipedScreen){
      debugSafeScreenClear(C_WHITE);
      UG_PutString(0, 0, "Subprogram stack overflow!\nYou must close the program.");
      wipedScreen = true;
   }
   if(getButtonPressed(buttonBack)){
      /*force kill when back pressed*/
      applicationRunning = false;
   }
   /*do nothing, this is a safe crash*/
}

var memoryAllocationError(void){
   static Boolean wipedScreen = false;
   
   if(!wipedScreen){
      debugSafeScreenClear(C_WHITE);
      UG_PutString(0, 0, "Could not allocate memory!\nYou must close the program.");
      wipedScreen = true;
   }
   if(getButtonPressed(buttonBack)){
      /*force kill when back pressed*/
      applicationRunning = false;
   }
   /*do nothing, this is a safe crash*/
}

static void uguiDrawPixel(UG_S16 x, UG_S16 y, UG_COLOR color){
   /*using 1bit grayscale*/
   int pixel = x + y * SCREEN_WIDTH;
   int byte = pixel / 8;
   int bit = pixel % 8;
   
   /*ugui will call this function even if its over the screen bounds, dont let those writes through*/
   if(pixel > SCREEN_WIDTH * SCREEN_HEIGHT - 1)
      return;
   
   if(!color){
      /*1 is black not white*/
      framebuffer[byte] |= (1 << (7 - bit));
   }
   else{
      framebuffer[byte] &= ~(1 << (7 - bit));
   }
}

/*needed to redraw after every call to setDebugTag(char*)*/
void forceFrameRedraw(void){
   WinDrawBitmap(offscreenBitmap, 0, 0);
}

void callSubprogram(activity_t activity){
   if(subprogramIndex < MAX_SUBPROGRAMS - 1){
      subprogramIndex++;
      parentSubprograms[subprogramIndex] = activity;
      currentSubprogram = activity;
      if(!subprogramArgsSet)
         subprogramArgs = makeVar(LENGTH_0, TYPE_NULL, 0);
      subprogramArgsSet = false;/*clear to prevent next subprogram called from inheriting the args*/
      setDebugTag("Subprogram Called");
   }
   else{
      currentSubprogram = errorSubprogramStackOverflow;
      /*cant recover from this*/
   }
}

void exitSubprogram(void){
   if(subprogramIndex > 0){
      subprogramIndex--;
      currentSubprogram = parentSubprograms[subprogramIndex];
      setDebugTag("Subprogram Exited");
   }
   else{
      /*last subprogram is complete*/
      setDebugTag("Application Exiting");
      applicationRunning = false;
   }
}

void execSubprogram(activity_t activity){
   if(!subprogramArgsSet)
      subprogramArgs = makeVar(LENGTH_0, TYPE_NULL, 0);
   subprogramArgsSet = false;/*clear to prevent next subprogram called from inheriting the args*/
   currentSubprogram = activity;
   setDebugTag("Subprogram Swapped Out");
}

var getSubprogramReturnValue(void){
   return lastSubprogramReturnValue;
}

var getSubprogramArgs(void){
   return subprogramArgs;
}

void setSubprogramArgs(var args){
   subprogramArgs = args;
   subprogramArgsSet = true;
}

var subprogramGetData(void){
   return subprogramData[subprogramIndex];
}

void subprogramSetData(var data){
   subprogramData[subprogramIndex] = data;
}

static Boolean testerInit(void){
   uint32_t osVer;
   uint32_t deviceId;
   Err error;
   
   FtrGet(sysFtrCreator, sysFtrNumROMVersion, &osVer);
   if(osVer < PalmOS35){
      FrmCustomAlert(alt_err, "TestSuite requires at least PalmOS 3.5", 0, 0);
      return false;
   }
   
   sharedDataBuffer = MemPtrNew(SHARED_DATA_BUFFER_SIZE);
   if(!sharedDataBuffer){
      FrmCustomAlert(alt_err, "Cant create memory buffer", 0, 0);
      return false;
   }
   
   offscreenBitmap = BmpCreate(SCREEN_WIDTH, SCREEN_HEIGHT, 1, NULL, &error);
   if(error != errNone){
      FrmCustomAlert(alt_err, "Cant create bitmap", 0, 0);
      return false;
   }
   
   KeySetMask(~(keyBitPageUp | keyBitPageDown | keyBitHard1  | keyBitHard2 | keyBitHard3  | keyBitHard4 ));
   
   framebuffer = BmpGetBits(offscreenBitmap);
   WinSetActiveWindow(WinGetDisplayWindow());
   
   UG_Init(&uguiStruct, uguiDrawPixel, SCREEN_WIDTH, SCREEN_HEIGHT);
   UG_FontSelect(&SELECTED_FONT);
   UG_SetBackcolor(C_WHITE);
   UG_SetForecolor(C_BLACK);
   UG_ConsoleSetBackcolor(C_WHITE);
   UG_ConsoleSetForecolor(C_BLACK);
   UG_FillScreen(C_WHITE);
   
   /*setup subprogram enviroment*/
   palmButtons = 0x0000;
   palmButtonsLastFrame = 0x0000;
   FtrGet(sysFtrCreator, sysFtrNumOEMDeviceID, &deviceId);
   isM515 = deviceId == (uint32_t)'lith';/*"lith" is the Palm m515 device code, likely because it is one of the first with a lithium ion battery*/
   isT3 = deviceId == (uint32_t)'Arz1';
   skipFrameDelay = false;
   subprogramIndex = 0;
   subprogramArgsSet = false;
   lastSubprogramReturnValue = makeVar(LENGTH_0, TYPE_NULL, 0);
   subprogramArgs = makeVar(LENGTH_0, TYPE_NULL, 0);
   currentSubprogram = functionPicker;
   parentSubprograms[0] = functionPicker;
   
   /*make function list, needs to be after setting isM515 and unsafeMode*/
   resetFunctionViewer();
   
   return true;
}

static void testerExit(void){
   MemPtrFree(sharedDataBuffer);
}

static void testerFrameLoop(void){
   static uint32_t lastResetTime = 0;
   EventType event;
   
   palmButtons = KeyCurrentState();
   
   /*allow exiting the app normally and prevent filling up the event loop*/
   do{
      EvtGetEvent(&event, 1);
      SysHandleEvent(&event);
      if(event.eType == appStopEvent){
         applicationRunning = false;
         break;
      }
   }
   while(event.eType != nilEvent);
   
   /*disable auto off timer*/
   if(TimGetSeconds() - lastResetTime > 50){
      EvtResetAutoOffTimer();
      lastResetTime = TimGetSeconds();
   }
   
   lastSubprogramReturnValue = currentSubprogram();
   
   WinDrawBitmap(offscreenBitmap, 0, 0);
   
   palmButtonsLastFrame = palmButtons;
   
   if(!skipFrameDelay)
      SysTaskDelay(4);/*30 fps*/
   skipFrameDelay = false;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdBPB, UInt16 launchFlags){
   if(cmd == sysAppLaunchCmdNormalLaunch){
      Boolean initSuccess = testerInit();
      
      if(!initSuccess)
         return 0;
      
      applicationRunning = true;
      
      while(applicationRunning)
         testerFrameLoop();
      
      testerExit();
   }
   else if(cmd == sysAppLaunchCmdSystemReset){
     /*eventualy boot time tests may go here*/
   }
   
   return 0;
}
