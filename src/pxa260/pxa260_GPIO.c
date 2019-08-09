#include "pxa260_GPIO.h"
#include "pxa260_mem.h"


static void pxa260gpioPrvRecalcValues(Pxa260gpio* gpio, UInt32 which){

   UInt8 i;
   UInt32 val, bit, newVal, oldVal = gpio->levels[which];

   val = gpio->dirs[which];
   newVal = (gpio->latches[which] & val) | (gpio->inputs[which] & (~val));

   val = 3;
   bit = 1;
   for(i = 0 ; i < 16; i++, val <<= 2, bit <<= 1) if(gpio->AFRs[(which << 1) + 0] & val) newVal &=~ bit;	//all AFRs read as zero to CPU
   for(i = 16; i < 32; i++, val <<= 2, bit <<= 1) if(gpio->AFRs[(which << 1) + 1] & val) newVal &=~ bit;	//all AFRs read as zero to CPU

   gpio->levels[which] = newVal;

   if (newVal != oldVal) {
      UInt32 wentHi = newVal &~ oldVal;
      UInt32 wentLo = oldVal &~ newVal;

      gpio->detStatus[which] |= (wentHi & gpio->riseDet[which]) | (wentLo & gpio->fallDet[which]);
   }
}

static void pxa260gpioPrvRecalcIntrs(Pxa260gpio* gpio){

   pxa260icInt(gpio->ic, PXA260_I_GPIO_all, gpio->detStatus[1] || gpio->detStatus[2] || (gpio->detStatus[0] &~ 3));
   pxa260icInt(gpio->ic, PXA260_I_GPIO_1, (gpio->detStatus[0] & 2) != 0);
   pxa260icInt(gpio->ic, PXA260_I_GPIO_0, (gpio->detStatus[0] & 1) != 0);
}

Boolean pxa260gpioPrvMemAccessF(void* userData, UInt32 pa, UInt8 size, Boolean write, void* buf){

   Pxa260gpio* gpio = userData;
	UInt32 val = 0;
	
	if(size != 4) {
		err_str(__FILE__ ": Unexpected ");
	//	err_str(write ? "write" : "read");
	//	err_str(" of ");
	//	err_dec(size);
	//	err_str(" bytes to 0x");
	//	err_hex(pa);
	//	err_str("\r\n");
		return true;		//we do not support non-word accesses
	}
	
	pa = (pa - PXA260_GPIO_BASE) >> 2;
	
	if(write){
		val = *(UInt32*)buf;
		
		switch(pa){
			case 0:
			case 1:
			case 2:
				
				break;
			
			case 3:
			case 4:
			case 5:
				pa -= 3;
				gpio->dirs[pa] = val;
				goto recalc;
			
			case 6:
			case 7:
			case 8:
				pa -= 6;
            gpio->latches[pa] |= val;
				goto recalc;
			
			case 9:
			case 10:
			case 11:
				pa -= 9;
            gpio->latches[pa] &=~ val;
				goto recalc;
			
			case 12:
			case 13:
			case 14:
				gpio->riseDet[pa - 12] = val;
				break;
			
			case 15:
			case 16:
			case 17:
				gpio->fallDet[pa - 15] = val;
				break;
			
			case 18:
			case 19:
			case 20:
				gpio->detStatus[pa - 18] &=~ val;
				goto trigger_intrs;
			
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
			case 26:
            gpio->AFRs[pa - 21] = val;
            //pa = (pa - 21) / 2;
				goto recalc;
		}
		
		goto done;
		
recalc:
      pxa260gpioPrvRecalcValues(gpio, pa);
		
trigger_intrs:
      pxa260gpioPrvRecalcIntrs(gpio);
	}
	else{
		switch(pa){
			case 0:
			case 1:
			case 2:
				val = gpio->levels[pa - 0];
				break;
			
			case 3:
			case 4:
			case 5:
				val = gpio->dirs[pa - 3];
				break;
			
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
				val = 0;
				break;
			
			case 12:
			case 13:
			case 14:
				val = gpio->riseDet[pa - 12];
				break;
			
			case 15:
			case 16:
			case 17:
				val = gpio->fallDet[pa - 15];
				break;
			
			case 18:
			case 19:
			case 20:
				val = gpio->detStatus[pa - 18];
				break;
			
			case 21:
			case 22:
			case 23:
			case 24:
			case 25:
			case 26:
				val = gpio->AFRs[pa - 21];
				break;
			
		}
		*(UInt32*)buf = val;
	}
	
done:
	return true;
}


void pxa260gpioInit(Pxa260gpio* gpio, Pxa260ic* ic){
   __mem_zero(gpio, sizeof(Pxa260gpio));
	gpio->ic = ic;
}

void pxa260gpioSetState(Pxa260gpio* gpio, UInt8 gpioNum, Boolean on){
	
	UInt32 set = gpioNum >> 5;
	UInt32 v = 1UL << (gpioNum & 0x1F);
	UInt32* p;
	
	if(gpioNum >= 85) return;
	
	p =  gpio->inputs + set;
	if(on) *p |= v;
	else *p &=~ v;
	
   pxa260gpioPrvRecalcValues(gpio, set);
   pxa260gpioPrvRecalcIntrs(gpio);
}

UInt8 pxa260gpioGetState(Pxa260gpio* gpio, UInt8 gpioNum){
	
	UInt32 sSet = gpioNum >> 5;
	UInt32 bSet = gpioNum >> 4;
	UInt32 sV = 1UL << (gpioNum & 0x1F);
	UInt32 bV = 3UL << (gpioNum & 0x0F);
	
	
	if(gpioNum >= 85) return PXA260_GPIO_NOT_PRESENT;
   if(gpio->AFRs[bSet] & bV) return ((gpio->AFRs[bSet] & bV) >> (gpioNum & 0x0F)) - 1 + PXA260_GPIO_AFR1;
	if(gpio->dirs[sSet] & sV) return (gpio->latches[sSet] & sV) ? PXA260_GPIO_HIGH : PXA260_GPIO_LOW;
	return PXA260_GPIO_HiZ;
}
