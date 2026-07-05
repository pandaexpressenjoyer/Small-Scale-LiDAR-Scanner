// Aditya Ganguli - 400572384 - gangua5
// Bus Speed - 22 MHz
// Meausurement LED - PF4
// UART Tx LED - PF0
// Additional Status LED - PN0
// FINAL VERSION
#include <stdint.h>
#include <stdio.h>
#include "PLL.h"
#include "SysTick.h"
#include "uart.h"
#include "onboardLEDs.h"
#include "tm4c1294ncpdt.h"
#include "VL53L1X_api.h"

#define I2C_MCS_ACK             0x00000008  // Data Acknowledge Enable
#define I2C_MCS_DATACK          0x00000008  // Acknowledge Data
#define I2C_MCS_ADRACK          0x00000004  // Acknowledge Address
#define I2C_MCS_STOP            0x00000004  // Generate STOP
#define I2C_MCS_START           0x00000002  // Generate START
#define I2C_MCS_ERROR           0x00000002  // Error
#define I2C_MCS_RUN             0x00000001  // I2C Master Enable
#define I2C_MCS_BUSY            0x00000001  // I2C Busy
#define I2C_MCR_MFE             0x00000010  // I2C Master Function Enable

#define MAXRETRIES              5           // number of receive attempts before giving up

uint16_t dev = 0x29;
int status = 0;
int steps = 8;

struct measuredData{
	// data from the ToF measurement
	uint16_t dist;
	float angle;
};

void detangle();
void home(int j);
void getMeasurement(int j, struct measuredData *dataArray, int i, int steps);

/////////////////////////////////////////////////////////////////

void Ports_Init(void){
	
	// Motor Setup - using Port H
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R7; // activate clock for Port H
  while ((SYSCTL_PRGPIO_R & SYSCTL_PRGPIO_R7) == 0) {} // allow time for clock to stabilize
  GPIO_PORTH_DIR_R |= 0xF; // Set bits 0-3
  GPIO_PORTH_DEN_R |= 0xF; // enable digital I/O
		
	// Bus Speed Output Test - using Port M
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R11;         // activate clock for Port M
  while((SYSCTL_PRGPIO_R & SYSCTL_RCGCGPIO_R11) == 0){}; // allow time for clock to stabilize
  GPIO_PORTM_DIR_R |= 0x01;                         // Set PM0 as output
  //GPIO_PORTM_AFSEL_R &= ~0x01;                      // disable alt funct on PM0
  GPIO_PORTM_DEN_R |= 0x01;                         // enable digital I/O on PM0
  //GPIO_PORTM_AMSEL_R &= ~0x01;                      // disable analog functionality on PM0
	
	// Start/Stop setup - using Port J - keypad needs 6 pins for a 2x4 setup
	SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R8;
  while ((SYSCTL_PRGPIO_R & SYSCTL_PRGPIO_R8) == 0) {}
  GPIO_PORTJ_DIR_R &= ~0x03;
  GPIO_PORTJ_DEN_R |= 0x03;
  GPIO_PORTJ_PUR_R |= 0x03;
	
}

void I2C_Init(void){
  SYSCTL_RCGCI2C_R |= SYSCTL_RCGCI2C_R0;           													// activate I2C0
  SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R1;          												// activate port B
  while((SYSCTL_PRGPIO_R&0x0002) == 0){};																		// ready?

    GPIO_PORTB_AFSEL_R |= 0x0C;           																	// 3) enable alt funct on PB2,3       0b00001100
    GPIO_PORTB_ODR_R |= 0x08;             																	// 4) enable open drain on PB3 only

    GPIO_PORTB_DEN_R |= 0x0C;             																	// 5) enable digital I/O on PB2,3
//    GPIO_PORTB_AMSEL_R &= ~0x0C;          																// 7) disable analog functionality on PB2,3

                                                                            // 6) configure PB2,3 as I2C
//  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R&0xFFFF00FF)+0x00003300;
  GPIO_PORTB_PCTL_R = (GPIO_PORTB_PCTL_R&0xFFFF00FF)+0x00002200;    //TED
    I2C0_MCR_R = I2C_MCR_MFE;                      													// 9) master function enable
    I2C0_MTPR_R = 0b0000000000000101000000000001010;                      	// 8) configure for 100 kbps clock (added 8 clocks of glitch suppression ~50ns)
//    I2C0_MTPR_R = 0x3B;                                        						// 8) configure for 100 kbps clock
        // 0b0000000000000101000000000001010
		// 0b0000000000000101000000000111011, 120 MHZ
}

//The VL53L1X needs to be reset using XSHUT.  We will use PG0
void PortG_Init(void){
    //Use PortG0
    SYSCTL_RCGCGPIO_R |= SYSCTL_RCGCGPIO_R6;                // activate clock for Port N
    while((SYSCTL_PRGPIO_R&SYSCTL_PRGPIO_R6) == 0){};    // allow time for clock to stabilize
    GPIO_PORTG_DIR_R &= 0x00;                                        // make PG0 in (HiZ)
  GPIO_PORTG_AFSEL_R &= ~0x01;                                     // disable alt funct on PG0
  GPIO_PORTG_DEN_R |= 0x01;                                        // enable digital I/O on PG0
                                                                                                    // configure PG0 as GPIO
  //GPIO_PORTN_PCTL_R = (GPIO_PORTN_PCTL_R&0xFFFFFF00)+0x00000000;
  GPIO_PORTG_AMSEL_R &= ~0x01;                                     // disable analog functionality on PN0

    return;
}
//XSHUT     This pin is an active-low shutdown input; 
//					the board pulls it up to VDD to enable the sensor by default. 
//					Driving this pin low puts the sensor into hardware standby. This input is not level-shifted.
void VL53L1X_XSHUT(void){
    GPIO_PORTG_DIR_R |= 0x01;                                        // make PG0 out
    GPIO_PORTG_DATA_R &= 0b11111110;                                 //PG0 = 0
    FlashAllLEDs();
    SysTick_Wait10ms(10);
    GPIO_PORTG_DIR_R &= ~0x01;                                            // make PG0 input (HiZ)
    
}
// for detangling wires connected to TOF
void detangle(void){
	
	uint32_t delay = 2;	
	
	for(int i=0; i< 512; i++){
		FlashLED2(2); // additional status LED
		// spin motor logic
		GPIO_PORTH_DATA_R = 0b00001001;
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00001100;			
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00000110;		
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00000011;			
		SysTick_Wait10ms(delay);
	}
}

// reset position of motor from where it is currently
void home(int j){
	
	uint32_t delay = 2;	
	
	for(int i=j; i>=0; i--){
		// spin motor logic
		GPIO_PORTH_DATA_R = 0b00001001;
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00001100;			
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00000110;		
		SysTick_Wait10ms(delay);
		GPIO_PORTH_DATA_R = 0b00000011;			
		SysTick_Wait10ms(delay);
	}
}

void getMeasurement(int j, struct measuredData *dataArray, int i, int steps){
	FlashLED3(2); // flash for measurement
	uint8_t dataReady = 0;
	uint16_t distance;
	uint8_t range;
	status = VL53L1X_StartRanging(dev);
	
	while(dataReady == 0){
			status = VL53L1X_CheckForDataReady(dev, &dataReady);
			VL53L1_WaitMs(dev, 5);
	}
	dataReady = 0;
	
	status = VL53L1X_GetDistance(dev, &distance);	
	status = VL53L1X_GetRangeStatus(dev, &range);	
	SysTick_Wait10ms(1);
	status = VL53L1X_ClearInterrupt(dev);
	if(range == 0){
		dataArray[j].dist = distance;
		dataArray[j].angle = (float)(i)*360/512;
	}
	else{
		status = VL53L1X_GetDistance(dev, &distance);
		dataArray[j].dist = distance;
		dataArray[j].angle = (float)(i)*360/512;
	}
	
	VL53L1X_StopRanging(dev);
}


void spinMotor(int steps){	
	
	uint32_t delay = 2;					
	struct measuredData dataArray[512/steps]; // array of measured data per steps, 16 steps for 11.25 and 64 steps for 45, giving 32 and 8 measurements respectively
	int j = 0; 
	float stepAngle = 0;
	// 16 steps for 11.25 and 64 steps for 45
	for(int i=0; i< 512; i++){
				if((GPIO_PORTJ_DATA_R & 0x01) == 0x00){
					home(i); 
					return; 
				}
				// spin motor logic
				GPIO_PORTH_DATA_R = 0b00000011;
				SysTick_Wait10ms(delay);			
				GPIO_PORTH_DATA_R = 0b00000110;			
				SysTick_Wait10ms(delay);
				GPIO_PORTH_DATA_R = 0b00001100;			
				SysTick_Wait10ms(delay);
				GPIO_PORTH_DATA_R = 0b00001001;			
				SysTick_Wait10ms(delay);
				if(i % (steps) == 0){
					// get data
					SysTick_Wait10ms(1);
					getMeasurement(j, dataArray, i, steps);
					j++;
					SysTick_Wait10ms(1);
				}
		}
	
		// send info to PC with UART
		for(int i = 0; i < (512/steps); ++i){
			sprintf(printf_buffer, "%02f, %u\r\n", dataArray[i].angle, dataArray[i].dist); 
			UART_printf(printf_buffer);
			FlashLED4(3);
		}
		detangle();
		return;
}



// Main Function

int main(void) {
	uint8_t byteData, sensorState=0, myByteArray[10] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF} , i=0;
  uint16_t wordData;
  uint16_t Distance;
  uint16_t SignalRate;
  uint16_t AmbientRate;
  uint16_t SpadNum; 
  uint8_t RangeStatus;
  uint8_t dataReady;
	// Initialize everything
	PLL_Init();	
	SysTick_Init();
	onboardLEDs_Init();
	Ports_Init();
	PortG_Init();
	I2C_Init();
	UART_Init();
	
	// load TOF sensor
	while(sensorState == 0){
		status = VL53L1X_BootState(dev, &sensorState);
		SysTick_Wait10ms(10);
	}
	FlashLED1(1);
	FlashLED2(1);
	FlashLED3(1);
	FlashLED4(1);
	UART_printf("ToF Chip Booted!\r\n Please Wait...\r\n");
	status = VL53L1X_ClearInterrupt(dev); // clear interrupt to enable next interrupt
	// intialize sensor
	status = VL53L1X_SensorInit(dev);
	Status_Check("SensorInit", status);
	
	while(1){
		// checks for button press at PJ1
    if((GPIO_PORTJ_DATA_R & 0x02) == 0){
      spinMotor(steps);
		}
	}
}




