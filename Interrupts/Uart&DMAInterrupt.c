#include <stdio.h>
#include "platform.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xaxidma.h"
#include "xuartps.h"
#include "xscugic.h"
#include "sleep.h"

#define GIC_DEVICE_ID	XPAR_PS7_SCUGIC_0_DEVICE_ID
#define UART_DEV_ID 	XPAR_PS7_UART_1_DEVICE_ID
#define DMA_DEVICE_ID 	XPAR_AXI_DMA_0_DEVICE_ID

#define UART_INTR_ID 	XPAR_PS7_UART_1_INTR
#define MM2S_INTR_ID 	XPAR_FABRIC_AXIDMA_0_MM2S_INTROUT_VEC_ID
#define S2MM_INTR_ID 	XPAR_FABRIC_AXIDMA_0_S2MM_INTROUT_VEC_ID

XUartPs UartInst;
XScuGic GicInst;
XAxiDma DmaInst;

volatile int TxDone;
volatile int RxDone;
volatile int Error;

u32 UartRxLen = 0;
int UartRxFlag = 0;
u8 * UartRxBuf = (u8 *)(0x2000000);
u8 * UartTxBuf = (u8 *)(0x4000000);
/***********************************
 * Uart Init function
 **********************************/
int Uart_Init();
/********************************
 * DMA initialize function
 ********************************/
int Dma_Init();

int Setup_Interrupt_System();

void Uart_Intr_Handler(void *CallBackRef, u32 Event, unsigned int EventData);

void Mm2s_Intr_Handler();
void S2mm_Intr_Handler();

int main()
{
    init_platform();
    Uart_Init();
    Dma_Init();
    Setup_Interrupt_System();
    while(1)
    {
    	/*****************************************************************************
		* Uart has receive one frame
		*****************************************************************************/
    	if (UartRxFlag == 1) {
    		// clear the flag
			UartRxFlag = 0;

			/*****************************************************************************
			* Transfer data from axidma to device
			*****************************************************************************/
			Xil_DCacheFlushRange((INTPTR)UartRxBuf, UartRxLen);//flush data into ddr
			usleep(2);
			// transfer data from axi dma to device
			XAxiDma_SimpleTransfer(&DmaInst, (UINTPTR)UartRxBuf, UartRxLen, XAXIDMA_DMA_TO_DEVICE);
			while(!TxDone);
			TxDone=0;//reset txdone flag; complete  txtransfer

			/*****************************************************************************
			* Transfer data from device to dma
			*****************************************************************************/
			Xil_DCacheInvalidateRange((INTPTR)UartTxBuf, UartRxLen);
			usleep(2);
			XAxiDma_SimpleTransfer(&DmaInst, (UINTPTR)UartTxBuf, UartRxLen, XAXIDMA_DEVICE_TO_DMA);
			while(!RxDone);
			RxDone = 0;
			XUartPs_Send(&UartInst, UartTxBuf, UartRxLen);

			XUartPs_Recv(&UartInst, UartRxBuf, 4096);//reset conter and start recv from uart
		}
    }

    cleanup_platform();
    return 0;
}

/*****************************************************************************
 * @ function : init uart and set the callback fuction
*****************************************************************************/
int Uart_Init()
{
	int Status;
	u32 IntrMask;

	XUartPs_Config *UartCfgPtr;
	UartCfgPtr = XUartPs_LookupConfig(UART_DEV_ID);
	Status = XUartPs_CfgInitialize(&UartInst, UartCfgPtr, UartCfgPtr->BaseAddress);
	if(Status != XST_SUCCESS)
	{
		printf("initialize UART failed\n");
		return XST_FAILURE;
	}
	/****************************************
	 * Set uart interrput mask
	 ****************************************/
	IntrMask =
		XUARTPS_IXR_TOUT | XUARTPS_IXR_PARITY | XUARTPS_IXR_FRAMING |
		XUARTPS_IXR_OVER | XUARTPS_IXR_TXEMPTY | XUARTPS_IXR_RXFULL |
		XUARTPS_IXR_RXOVR;
	XUartPs_SetInterruptMask(&UartInst, IntrMask);

	/*****************************************************************************
	* Set Uart interrput callback function
	*****************************************************************************/
	XUartPs_SetHandler(&UartInst, (XUartPs_Handler)Uart_Intr_Handler, &UartInst);

	/*****************************************************************************
	* Set Uart baud rate
	*****************************************************************************/
	XUartPs_SetBaudRate(&UartInst, 115200);

	/*****************************************************************************
	* Set Uart opertion mode
	*****************************************************************************/
	XUartPs_SetOperMode(&UartInst, XUARTPS_OPER_MODE_NORMAL);

	/*****************************************************************************
	* Set Uart Receive timeout
	*****************************************************************************/
	XUartPs_SetRecvTimeout(&UartInst, 8);

	/*****************************************************************************
	* Start to listen
	*****************************************************************************/
	XUartPs_Recv(&UartInst, UartRxBuf, 4096);
	return Status;
}

void Uart_Intr_Handler(void *CallBackRef, u32 Event, unsigned int EventData)
{
	if (Event == XUARTPS_EVENT_RECV_TOUT) {
		if(EventData == 0)
		{
			XUartPs_Recv(&UartInst, UartRxBuf, 4096);
		}
		else if(EventData > 0) {
			UartRxLen = EventData;
			UartRxFlag = 1;
		}
	}
}

/*****************************************************************************
 * @ function : init Axi DMA
*****************************************************************************/
int Dma_Init()
{
	int Status;
	XAxiDma_Config * DmaCfgPtr;
	DmaCfgPtr = XAxiDma_LookupConfig(DMA_DEVICE_ID);
	Status = XAxiDma_CfgInitialize(&DmaInst, DmaCfgPtr);
	if(Status != XST_SUCCESS)
	{
		printf("initialize AXI DMA failed\n");
		return XST_FAILURE;
	}
	/*****************************************************************************
	* Disable all the interrupt before setup
	*****************************************************************************/
	XAxiDma_IntrDisable(&DmaInst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrDisable(&DmaInst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	/*****************************************************************************
	*Enable all the interrput
	*****************************************************************************/
	XAxiDma_IntrEnable(&DmaInst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
	XAxiDma_IntrEnable(&DmaInst, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);

	return Status;
}
/*****************************************************************************/
/*
*
* This is the DMA TX Interrupt handler function.
*
* It gets the interrupt status from the hardware, acknowledges it, and if any
* error happens, it resets the hardware. Otherwise, if a completion interrupt
* is present, then sets the TxDone.flag
*
* @param	Callback is a pointer to TX channel of the DMA engine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void Mm2s_Intr_Handler(void *Callback)
{

	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	/* Read pending interrupts */
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DMA_TO_DEVICE);

	/* Acknowledge pending interrupts */


	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DMA_TO_DEVICE);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {

		return;
	}

	/*
	 * If error interrupt is asserted, raise error flag, reset the
	 * hardware to recover from the error, and return with no further
	 * processing.
	 */
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error = 1;

		/*
		 * Reset should never fail for transmit channel
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = 10000;

		while (TimeOut) {
			if (XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	/*
	 * If Completion interrupt is asserted, then set the TxDone flag
	 */
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		TxDone = 1;
	}
}

/*****************************************************************************/
/*
*
* This is the DMA RX interrupt handler function
*
* It gets the interrupt status from the hardware, acknowledges it, and if any
* error happens, it resets the hardware. Otherwise, if a completion interrupt
* is present, then it sets the RxDone flag.
*
* @param	Callback is a pointer to RX channel of the DMA engine.
*
* @return	None.
*
* @note		None.
*
******************************************************************************/
void S2mm_Intr_Handler(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;

	/* Read pending interrupts */
	IrqStatus = XAxiDma_IntrGetIrq(AxiDmaInst, XAXIDMA_DEVICE_TO_DMA);

	/* Acknowledge pending interrupts */
	XAxiDma_IntrAckIrq(AxiDmaInst, IrqStatus, XAXIDMA_DEVICE_TO_DMA);

	/*
	 * If no interrupt is asserted, we do not do anything
	 */
	if (!(IrqStatus & XAXIDMA_IRQ_ALL_MASK)) {
		return;
	}

	/*
	 * If error interrupt is asserted, raise error flag, reset the
	 * hardware to recover from the error, and return with no further
	 * processing.
	 */
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {

		Error = 1;

		/* Reset could fail and hang
		 * NEED a way to handle this or do not call it??
		 */
		XAxiDma_Reset(AxiDmaInst);

		TimeOut = 10000;

		while (TimeOut) {
			if(XAxiDma_ResetIsDone(AxiDmaInst)) {
				break;
			}

			TimeOut -= 1;
		}

		return;
	}

	/*
	 * If completion interrupt is asserted, then set RxDone flag
	 */
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {

		RxDone = 1;
	}
}


/*****************************************************************************
 * @ function : Set up the interrupt system
*****************************************************************************/
int Setup_Interrupt_System()
{
	int Status;
	XScuGic_Config * GicCfgPtr;
	GicCfgPtr = XScuGic_LookupConfig(GIC_DEVICE_ID);
	Status = XScuGic_CfgInitialize(&GicInst, GicCfgPtr, GicCfgPtr->CpuBaseAddress);
	if(Status != XST_SUCCESS)
	{
		printf("initialize GIC failed\n");
		return XST_FAILURE;
	}
	/*****************************************************************************
	* initialize exception system
	*****************************************************************************/
	Xil_ExceptionInit();

	/*****************************************************************************
	* register interrput type exception
	*****************************************************************************/
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,(Xil_ExceptionHandler)XScuGic_InterruptHandler, &GicInst);

	/*****************************************************************************
	* connect interrput to scugic controller
	*****************************************************************************/
	Status = XScuGic_Connect(&GicInst, UART_INTR_ID, (Xil_ExceptionHandler) XUartPs_InterruptHandler, &UartInst);
	if(Status != XST_SUCCESS)
	{
		printf("Connect Uart interrput to GIC failed\n");
		return XST_FAILURE;
	}
	Status = XScuGic_Connect(&GicInst, MM2S_INTR_ID, (Xil_ExceptionHandler) Mm2s_Intr_Handler, &DmaInst);
	if(Status != XST_SUCCESS)
	{
		printf("Connect DMA tx interrput to GIC failed\n");
		return XST_FAILURE;
	}

	Status = XScuGic_Connect(&GicInst, S2MM_INTR_ID, (Xil_ExceptionHandler) S2mm_Intr_Handler, &DmaInst);
	if(Status != XST_SUCCESS)
	{
		printf("Connect DMA tx interrput to GIC failed\n");
		return XST_FAILURE;
	}

	/*****************************************************************************
	* Enable the interrput
	*****************************************************************************/
	XScuGic_Enable(&GicInst, UART_INTR_ID);
	XScuGic_Enable(&GicInst, S2MM_INTR_ID);
	XScuGic_Enable(&GicInst, MM2S_INTR_ID);

	/*****************************************************************************
	* Enable the exception system
	*****************************************************************************/
	Xil_ExceptionEnable();
	return Status;
}
