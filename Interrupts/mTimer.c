
#include "xparameters.h"
#include "xgpiops.h"
#include "xstatus.h"
#include "xplatform_info.h"
#include "xscutimer.h"
#include "Xscugic.h"

#include "mTimer.h"

#define LED    	54

#define GPIO_DEVICE_ADDR      XPAR_XGPIOPS_0_BASEADDR
XGpioPs Gpio;

void Gpio_Init(void){
	XGpioPs_Config *ConfigPtr;

	ConfigPtr = XGpioPs_LookupConfig(GPIO_DEVICE_ADDR);
	XGpioPs_CfgInitialize(&Gpio, ConfigPtr,ConfigPtr->BaseAddr);

	XGpioPs_SetDirectionPin(&Gpio, LED, 1);
	XGpioPs_SetOutputEnablePin(&Gpio, LED, 1);
	XGpioPs_WritePin(&Gpio, LED, 0);
}

#define TIMER_DEVICE_ADDR XPAR_XSCUTIMER_0_BASEADDR
#define INTC_DEVICE_ADDR XPAR_XSCUGIC_0_BASEADDR
#define TIMER_IRPT_INTR XPAR_SCUTIMER_INTR
#define TIMER_LOAD_VALUE 0x514c7 //666*1000*1/2 666mhz
static XScuGic Intc; //GIC
static XScuTimer Timer;//timer

static void SetupInterruptSystem(XScuGic *GicInstancePtr,
XScuTimer *TimerInstancePtr, u16 TimerIntrId);
static void TimerIntrHandler(void *CallBackRef);

void SetupInterruptSystem(XScuGic *GicInstancePtr,XScuTimer *TimerInstancePtr, u16 TimerIntrId){
	XScuGic_Config *IntcConfig; //GIC config
	Xil_ExceptionInit();
	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ADDR);
	XScuGic_CfgInitialize(GicInstancePtr, IntcConfig,IntcConfig->CpuBaseAddress);
	//connect to the hardware
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
		(Xil_ExceptionHandler)XScuGic_InterruptHandler,
		GicInstancePtr);
	//set up the timer interrupt
	XScuGic_Connect(GicInstancePtr, TimerIntrId,
		(Xil_ExceptionHandler)TimerIntrHandler,
		(void *)TimerInstancePtr);
	//enable the interrupt for the Timer at GIC
	XScuGic_Enable(GicInstancePtr, TimerIntrId);
	//enable interrupt on the timer
	XScuTimer_EnableInterrupt(TimerInstancePtr);
	// Enable interrupts in the Processor.
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
}

volatile int ms_count;
unsigned char led_state=0;

static void TimerIntrHandler(void *CallBackRef){
	static int ms_count = 0; //计数
	XScuTimer *TimerInstancePtr = (XScuTimer *) CallBackRef;
	XScuTimer_ClearInterruptStatus(TimerInstancePtr);

    //ADD Your Own IntrHandle Function
	ms_count++;
	if(ms_count>=1000){
		ms_count=0;
		if(led_state==0)led_state=1;
		else led_state=0;
		XGpioPs_WritePin(&Gpio, LED, led_state);
	}
    //END
}

void Timer_Init(void){
	XScuTimer_Config *TMRConfigPtr;
	TMRConfigPtr = XScuTimer_LookupConfig(TIMER_DEVICE_ADDR);
	XScuTimer_CfgInitialize(&Timer, TMRConfigPtr,TMRConfigPtr->BaseAddr);
	//XScuTimer_SelfTest(&Timer);
	XScuTimer_LoadTimer(&Timer, TIMER_LOAD_VALUE);//设置自动重装载值
	XScuTimer_EnableAutoReload(&Timer);//使能自动重装载
	XScuTimer_Start(&Timer);//启动定时器
	SetupInterruptSystem(&Intc,&Timer,TIMER_IRPT_INTR);//将时钟挂载到中断
}