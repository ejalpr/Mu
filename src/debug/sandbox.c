#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "../m68k/m68k.h"
#include "../emulator.h"
#include "sandbox.h"


//this wrappers 68k code and allows calling it for tests, this should be useful for determi ing if hardware accesses are correct

#if defined(EMU_DEBUG)
typedef struct{
   void* hostPointer;
   uint32_t emuPointer;
   uint64_t bytes;//may be used for strings or structs in the future
}return_pointer_t;


static bool executionFinished;


#include "sandboxTrapNumToName.c.h"

static char* takeStackDump(uint32_t bytes){
   char* textBytes = malloc(bytes * 2);
   uint32_t textBytesOffset = 0;
   uint32_t stackAddress = m68k_get_reg(NULL, M68K_REG_SP);

   textBytes[0] = '\0';

   for(uint32_t count = 0; count < bytes; count++){
      sprintf(textBytes + textBytesOffset, "%02X", m68k_read_memory_8(stackAddress + count));
      textBytesOffset = strlen(textBytes);
   }

   return textBytes;
}

static bool spammingTrap(uint16_t trap){
   switch(trap){
      case 0xA249://sysTrapHwrDelay
         return true;

      default:
         return false;
   }
   return false;
}

static void printTrapInfo(uint16_t trap){
   debugLog("name:%s, API:0x%04X, location:0x%08X\n", lookupTrap(trap), trap, m68k_read_memory_32(0x000008CC + (trap & 0x0FFF) * 4));
}

//debug
#if 0//defined(EMU_DEBUG) && defined(EMU_OPCODE_LEVEL_DEBUG) //legacy memory space testing code
#define LOGGED_OPCODES 100
static bool invalidBehaviorAbort;
static char disassemblyBuffer[LOGGED_OPCODES][100];//store the opcode and program counter for the last 10 opcodes

static void invalidBehaviorCheck(){
   char opcodeName[100];
   uint32_t programCounter = m68k_get_reg(NULL, M68K_REG_PPC);
   uint16_t instruction = m68k_get_reg(NULL, M68K_REG_IR);
   bool invalidInstruction = !m68k_is_valid_instruction(instruction, M68K_CPU_TYPE_68000);
   bool invalidBank = (bankType[START_BANK(programCounter)] == CHIP_NONE);

   //get current opcode
   if(!invalidBank){
      //must dissasemble as 68020 to prevent address masking, is also more descriptive for invalid opcodes
      m68k_disassemble(opcodeName, programCounter, M68K_CPU_TYPE_68020);
   }
   else{
      strcpy(opcodeName, "Invalid bank, cant read");
   }
   sprintf(opcodeName + strlen(opcodeName), " at PC:0x%08X", programCounter);

   //shift opcode buffer
   for(uint32_t i = 0; i < LOGGED_OPCODES - 1; i++)
      strcpy(disassemblyBuffer[i], disassemblyBuffer[i + 1]);

   //add to opcode buffer
   strcpy(disassemblyBuffer[LOGGED_OPCODES - 1], opcodeName);

   if(invalidInstruction || invalidBank/* || (instruction == 0x0000 && programCounter != 0x00000000)*/){
      //0x0000 is "ori.b #$IMM, D0", effectivly NOP if the post op byte is 0x00 but still a valid opcode
      //usualy never encountered unless executing empty address space, so it still triggers debug abort
      m68k_end_timeslice();
      invalidBehaviorAbort = true;

      for(uint32_t i = 0; i < LOGGED_OPCODES; i++)
         debugLog("%s\n", disassemblyBuffer[i]);
      //currently CPU32 opcodes will be listed as "unknown", I cant change that properly unless I directly edit musashi source, something I want to avoid doing
      debugLog("Instruction:\"%s\", instruction value:0x%04X, bank type:%d\n", invalidInstruction ? "unknown" : opcodeName, instruction, bankType[START_BANK(programCounter)]);
   }

   //custom debug operations
   switch(programCounter){
      /*
      //case 0x10000566:
      case 0x100003F8:
         {
            //failing on executing first trap "HwrPreDebugInit"
            char* data = takeStackDump(32);
            debugLog("Stack dump:%s\n", data);
            free(data);
         }
         break;
      */

      default:
         break;
   }

#if defined(EMU_LOG_APIS)
   if(instruction == 0x4E4F){
      //Trap F/API call
      uint16_t trap = m68k_read_memory_16(programCounter + 2);
      if(!spammingTrap(trap)){
         debugLog("Trap F API:%s, API number:0x%04X, PC:0x%08X\n", lookupTrap(trap), trap, programCounter);
      }

      //custom debug operations
      switch(trap){
         /*
         case 0xA09A://sysTrapSysTimerWrite
            printTrapInfo(trap);
            break;
         */
         case 0xA255://sysTrapHwrIRQ5Handler
            printTrapInfo(trap);
            break;

         default:
            break;
      }
   }
#endif
}
#endif

uint32_t callTrap(bool fallthrough, const char* name, const char* prototype, ...){
   //prototype is a java style function signature describing values passed and returned "v(wllp)"
   //is return void and pass a uint16_t(word), 2 uint32_t(long) and 1 pointer
   //valid types are b(yte), w(ord), l(ong), p(ointer), s(tring) and v(oid), a capital letter means its a return pointer
   //EvtGetPen v(WWB) returns nothing but writes back to tha calling function with 3 pointers,
   //these are allocated in the bootloader area and interpreted to host pointers on return
   va_list args;
   const char* params = prototype + 2;
   uint16_t trap = reverseLookupTrap(name);
   uint32_t stackFrameStart = m68k_get_reg(NULL, M68K_REG_SP);
   uint32_t stackAddr = stackFrameStart;
   uint32_t oldPc = m68k_get_reg(NULL, M68K_REG_PC);
   uint32_t oldA0 = m68k_get_reg(NULL, M68K_REG_A0);
   uint32_t oldD0 = m68k_get_reg(NULL, M68K_REG_D0);
   uint32_t trapReturn = 0x00000000;
   return_pointer_t trapReturnPointers[10];
   uint8_t trapReturnPointerIndex = 0;
   uint32_t callWriteOut = 0xFFFFFFE0;
   uint32_t callStart;

   va_start(args, prototype);
   while(*params != ')'){
      switch(*params){
         case 'v':
         case 'V':
            //do nothing
            break;

         case 'b':
            //bytes are 16 bits long on the stack due to memory alignment restrictions
         case 'w':
            stackAddr -= 2;
            m68k_write_memory_16(stackAddr, va_arg(args, uint32_t));
            break;

         case 'l':
         case 'p':
            stackAddr -= 4;
            m68k_write_memory_32(stackAddr, va_arg(args, uint32_t));
            break;

         //return pointer values
         case 'B':
            trapReturnPointers[trapReturnPointerIndex].hostPointer = va_arg(args, void*);
            trapReturnPointers[trapReturnPointerIndex].emuPointer = callWriteOut;
            trapReturnPointers[trapReturnPointerIndex].bytes = 1;
            stackAddr -= 4;
            m68k_write_memory_32(stackAddr, trapReturnPointers[trapReturnPointerIndex].emuPointer);
            callWriteOut += 4;
            trapReturnPointerIndex++;
            break;

         case 'W':
            trapReturnPointers[trapReturnPointerIndex].hostPointer = va_arg(args, void*);
            trapReturnPointers[trapReturnPointerIndex].emuPointer = callWriteOut;
            trapReturnPointers[trapReturnPointerIndex].bytes = 2;
            stackAddr -= 4;
            m68k_write_memory_32(stackAddr, trapReturnPointers[trapReturnPointerIndex].emuPointer);
            callWriteOut += 4;
            trapReturnPointerIndex++;
            break;

         case 'L':
         case 'P':
            trapReturnPointers[trapReturnPointerIndex].hostPointer = va_arg(args, void*);
            trapReturnPointers[trapReturnPointerIndex].emuPointer = callWriteOut;
            trapReturnPointers[trapReturnPointerIndex].bytes = 4;
            stackAddr -= 4;
            m68k_write_memory_32(stackAddr, trapReturnPointers[trapReturnPointerIndex].emuPointer);
            callWriteOut += 4;
            trapReturnPointerIndex++;
            break;
      }

      params++;
   }

   //write to the bootloader memory, its not important when debugging
   callStart = callWriteOut;
   m68k_write_memory_16(callWriteOut, 0x4E4F);//trap f opcode
   callWriteOut += 2;
   m68k_write_memory_16(callWriteOut, trap);
   callWriteOut += 2;

   //end execution with CMD_EXECUTION_DONE
   m68k_write_memory_16(callWriteOut, 0x23FC);//move.l data imm to address at imm2 opcode
   callWriteOut += 2;
   m68k_write_memory_32(callWriteOut, MAKE_EMU_CMD(CMD_EXECUTION_DONE));
   callWriteOut += 4;
   m68k_write_memory_32(callWriteOut, EMU_REG_ADDR(EMU_CMD));
   callWriteOut += 4;

   executionFinished = false;
   m68k_set_reg(M68K_REG_SP, stackAddr);
   m68k_set_reg(M68K_REG_PC, callStart);

   //only setup the trap then fallthrough to normal execution, may be needed on app switch since the trap may not return
   if(!fallthrough){
      while(!executionFinished)
         m68k_execute(1);//m68k_execute() always runs requested cycles + extra cycles of the final opcode, this executes 1 opcode
      if(prototype[0] == 'p')
         trapReturn = m68k_get_reg(NULL, M68K_REG_A0);
      else if(prototype[0] == 'b' || prototype[0] == 'w' || prototype[0] == 'l')
         trapReturn = m68k_get_reg(NULL, M68K_REG_D0);
      m68k_set_reg(M68K_REG_PC, oldPc);
      m68k_set_reg(M68K_REG_SP, stackFrameStart);
      m68k_set_reg(M68K_REG_A0, oldA0);
      m68k_set_reg(M68K_REG_D0, oldD0);

      //remap all argument pointers
      for(uint8_t count = 0; count < trapReturnPointerIndex; count++){
         switch(trapReturnPointers[count].bytes){
            case 1:
               *(uint8_t*)trapReturnPointers[count].hostPointer = m68k_read_memory_8(trapReturnPointers[count].emuPointer);
               break;

            case 2:
               *(uint16_t*)trapReturnPointers[count].hostPointer = m68k_read_memory_16(trapReturnPointers[count].emuPointer);
               break;

            case 4:
               *(uint32_t*)trapReturnPointers[count].hostPointer = m68k_read_memory_32(trapReturnPointers[count].emuPointer);
               break;
         }
      }
   }

   va_end(args);
   return trapReturn;
}

uint32_t makePalmString(const char* str){
   uint32_t strLength = strlen(str) + 1;
   uint32_t strData = callTrap(false, "MemPtrNew", "p(l)", strLength);

   if(strData != 0)
      for(uint32_t count = 0; count < strLength; count++)
         m68k_write_memory_8(strData + count, str[count]);
   return strData;
}

char* makeNativeString(uint32_t address){
   if(address != 0){
      int16_t strLength = callTrap(false, "StrLen", "w(p)", address) + 1;
      char* nativeStr = malloc(strLength);

      for(uint32_t count = 0; count < strLength; count++)
         nativeStr[count] = m68k_read_memory_8(address + count);
      return nativeStr;
   }
   return NULL;
}

void freePalmString(uint32_t address){
   callTrap(false, "MemChunkFree", "w(p)", address);
}

#if 0
void sendTouchEvents(){
   static input_t lastFrameInput = {0};
   bool locationChanged = lastFrameInput.touchscreenX != palmInput.touchscreenX || lastFrameInput.touchscreenY != palmInput.touchscreenY;
   bool pressedChanged = lastFrameInput.touchscreenTouched != palmInput.touchscreenTouched;

   //proto: Err EvtEnqueuePenPoint(PointType *ptP);
   //PointType is int16, int16
   if(locationChanged || pressedChanged){
      if(palmInput.touchscreenTouched){
         m68k_write_memory_16(0xFFFFFFE0 - 4, palmInput.touchscreenX);
         m68k_write_memory_16(0xFFFFFFE0 - 2, palmInput.touchscreenY);
      }
      else{
         m68k_write_memory_16(0xFFFFFFE0 - 4, (uint16_t)-1);
         m68k_write_memory_16(0xFFFFFFE0 - 2, (uint16_t)-1);
      }
      callTrap(false, "EvtEnqueuePenPoint", "w(p)", 0xFFFFFFE0 - 4);
   }

   lastFrameInput = palmInput;
}
#endif

#if 0
#include "../../postBootProof.png.c"//contains finishedBooting.pixel_data

void sinfulExecution(){
   //jump directly to an application using SysAppLaunch, very evil and inaccurate, will be removed before version 1.0
   static bool alreadyRun = false;

   if(!alreadyRun){
      bool hasBooted = !memcmp(palmFramebuffer, finishedBooting.pixel_data, 160 * 160 * 2);
      if(hasBooted){
         /*proto:Err SysAppLaunch(UInt16 cardNo, LocalID dbID, UInt16 launchFlags,
                     UInt16 cmd, MemPtr cmdPBP, UInt32 *resultP);
         */

         uint32_t palmString = makePalmString("Memo Pad");
         if(palmString != 0){
            uint32_t localId = callTrap(false, "DmFindDatabase", "l(wp)", 0, palmString);
            callTrap(true, "SysAppLaunch", "w(wlwwpp)", 0, localId, 0, 0, 0, 0);
            //dont free palmString yet
            //SysUIAppSwitch
         }

         alreadyRun = true;
      }
   }
}
#endif

void sandboxTest(uint32_t test){
   switch(test){
      case SANDBOX_TEST_OS_VER:{
            debugLog("Sandbox: Testing OS version");
            uint32_t verStrAddr = callTrap(false, "SysGetOSVersionString", "p()");
            char* nativeStr = makeNativeString(verStrAddr);

            debugLog("Sandbox: OS version is:\"%s\"", nativeStr);
            free(nativeStr);
         }
         break;

      case SANDBOX_TEST_HWR_ADC:
         //not done yet
         break;
   }
}

void sandboxReturn(){
   executionFinished = true;
}

#else
void sandboxTest(uint32_t test){}
void sandboxReturn(){}
#endif