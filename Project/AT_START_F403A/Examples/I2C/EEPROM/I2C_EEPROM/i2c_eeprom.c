/**
  ******************************************************************************
  * @file    I2C/EEPROM/I2C_EEPROM/i2c_eeprom.c 
  * @author  Artery Technology 
  * @version V1.1.2
  * @date    2019-01-04
  * @brief   The driver of eeprom with i2c.
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, ARTERYTEK SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2018 ArteryTek</center></h2>
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "at32f4xx.h"
#include "i2c_eeprom.h"
#include "string.h"	
#include "stdbool.h"

/** @addtogroup AT32F403A_StdPeriph_Examples
  * @{
  */

/** @addtogroup I2C_EEPROM
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/* when EEPROM is writing data inside,it won't response the request from the master.check the ack,
if EEPROM response,make clear that EEPROM finished the last data-writing,allow the next operation */
static bool check_begin = FALSE;
static bool OffsetDone = FALSE;

u32  sEETimeout = sEE_LONG_TIMEOUT; 

static u8 MasterDirection = Transmitter;
static u16 SlaveADDR;
static u16 DeviceOffset=0x0;

u16 I2C_NumByteToWrite = 0;
u8 I2C_NumByteWritingNow = 0;
u8* I2C_pBuffer = 0; 
u16 I2C_WriteAddr = 0;

static u8 SendBuf[8]={0};
static u16 BufCount=0;
static u16 Int_NumByteToWrite=0;
static u16 Int_NumByteToRead=0;
/* global state variable i2c_comm_state */
volatile I2C_STATE i2c_comm_state;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Timeout callback used by the I2C EEPROM driver.
  * @param  None
  * @retval None
  */
u8 sEE_TIMEOUT_UserCallback(void)
{
  printf("error!!!\r\n");
  /* Block communication and all processes */
  while (1)
  {   
  }
}

/**
  * @brief  Initializes peripherals used by the I2C EEPROM driver.
  * @param  None
  * @retval None
  */
void  I2C_EE_Init(void)
{
  /** GPIO configuration and clock enable */
  GPIO_InitType  GPIO_InitStructure; 
  I2C_InitType  I2C_InitStructure;
#if PROCESS_MODE==1
  NVIC_InitType NVIC_InitStructure;
#endif
  I2Cx_peripheral_clock();
  I2C_DeInit(I2Cx);

  I2Cx_scl_pin_clock();
  I2Cx_sda_pin_clock(); 
 
#ifdef I2C1_REMAP
  RCC_APB2PeriphClockCmd(RCC_APB2PERIPH_AFIO, ENABLE);
  GPIO_PinsRemapConfig(GPIO_Remap_I2C1, ENABLE); 
#endif
  GPIO_InitStructure.GPIO_Pins =  I2Cx_SCL_PIN | I2Cx_SDA_PIN;
  GPIO_InitStructure.GPIO_MaxSpeed = GPIO_MaxSpeed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
  GPIO_Init(GPIOx, &GPIO_InitStructure);

  /** I2C periphral configuration */
  I2C_DeInit(I2Cx);
  I2C_InitStructure.I2C_Mode = I2C_Mode_I2CDevice; 
  I2C_InitStructure.I2C_FmDutyCycle = I2C_FmDutyCycle_2_1;  
  I2C_InitStructure.I2C_OwnAddr1 = 0xff;  
  I2C_InitStructure.I2C_Ack = I2C_Ack_Enable; 
  I2C_InitStructure.I2C_AddrMode = I2C_AddrMode_7bit;
  I2C_InitStructure.I2C_BitRate = I2C_Speed; 
  I2C_Init(I2Cx, &I2C_InitStructure);
#if PROCESS_MODE==1
  /** I2C NVIC configuration */  
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
  NVIC_InitStructure.NVIC_IRQChannel = I2C1_EV_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
  NVIC_InitStructure.NVIC_IRQChannel = I2C1_ER_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
#elif PROCESS_MODE==2  
  RCC_AHBPeriphClockCmd(RCC_AHBPERIPH_DMA1, ENABLE);
#endif
}

/**
  * @brief  Writes buffer of data to the I2C EEPROM.
  * @param  pBuffer: pointer to the buffer  containing the data to be
  *                  written to the EEPROM.
  * @param  WriteAddr: EEPROM's internal address to write to.
  * @param  NumByteToWrite: number of bytes to write to the EEPROM.
  * @retval None
  */
void I2C_EE_WriteBuffer(u8* pBuffer, u16 WriteAddr, u16 NumByteToWrite)
{
  if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
  {
    return;
  }
  I2C_pBuffer=pBuffer;
  I2C_WriteAddr=WriteAddr;
  I2C_NumByteToWrite = NumByteToWrite;

  while(I2C_NumByteToWrite>0)
  {
    I2C_EE_WriteOnePage(I2C_pBuffer,I2C_WriteAddr,I2C_NumByteToWrite);
  }
}

/**
  * @brief  Writes a page of data to the I2C EEPROM, general called by
  *         I2C_EE_WriteBuffer.
  * @param  pBuffer: pointer to the buffer  containing the data to be
  *                  written to the EEPROM.
  * @param  WriteAddr: EEPROM's internal address to write to.
  * @param  NumByteToWrite: number of bytes to write to the EEPROM.
  * @retval None
  */
void I2C_EE_WriteOnePage(u8* pBuffer, u16 WriteAddr, u16 NumByteToWrite)
{
  u8 NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0;

  Addr = WriteAddr % I2C_PageSize;
  count = I2C_PageSize - Addr;
  NumOfPage =  NumByteToWrite / I2C_PageSize;
  NumOfSingle = NumByteToWrite % I2C_PageSize;
  
  I2C_NumByteWritingNow=0;
  /** If WriteAddr is I2C_PageSize aligned */
  if(Addr == 0) 
  {
     /** If NumByteToWrite < I2C_PageSize */
     if(NumOfPage == 0) 
     {
        I2C_NumByteWritingNow=NumOfSingle;
        I2C_EE_PageWrite(pBuffer, WriteAddr, NumOfSingle);
     }
     /** If NumByteToWrite > I2C_PageSize */
     else  
     {
         I2C_NumByteWritingNow=I2C_PageSize;
         I2C_EE_PageWrite(pBuffer, WriteAddr, I2C_PageSize); 
     }
  }
  /** If WriteAddr is not I2C_PageSize aligned */
  else 
  {
     /* If NumByteToWrite < I2C_PageSize */
     if(NumOfPage== 0) 
     {
        I2C_NumByteWritingNow=NumOfSingle;
        I2C_EE_PageWrite(pBuffer, WriteAddr, NumOfSingle);
     }
     /* If NumByteToWrite > I2C_PageSize */
     else
     {
        if(count != 0)
        {  
           I2C_NumByteWritingNow=count;
           I2C_EE_PageWrite(pBuffer, WriteAddr, count);
        }
     }
  }  
}

/**
  * @brief  Writes more than one byte to the EEPROM with a single WRITE
  *         cycle. The number of byte can't exceed the EEPROM page size.
  * @param  pBuffer: pointer to the buffer containing the data to be
  *                  written to the EEPROM.
  * @param  WriteAddr: EEPROM's internal address to write to (1-16).
  * @param  NumByteToWrite: number of bytes to write to the EEPROM.
  * @retval None
  */
void I2C_EE_PageWrite(u8* pBuffer, u16 WriteAddr, u16 NumByteToWrite)
{
#if PROCESS_MODE==0
  sEETimeout = sEE_LONG_TIMEOUT;
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
	{
	  if((sEETimeout--) == 0) sEE_TIMEOUT_UserCallback();
	}
	 /** Send START condition */
	I2C_GenerateSTART(I2Cx, ENABLE); 
	 /** Test on EV5 and clear it */
  sEETimeout = sEE_LONG_TIMEOUT;
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))	
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	/** Send EEPROM address for write */ 
	I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Transmit); 
	/** Test on EV6 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_ADDRESS))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	/** Send the EEPROM's internal address to write to */ 
	I2C_SendData(I2Cx, WriteAddr); 
	/** Test on EV8 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
	while(! I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_TRANSMITTED))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	/** While there is data to be written */ 
	while(NumByteToWrite--) 
	{ 
   /** Send the current byte */ 
   I2C_SendData(I2Cx, *pBuffer); 
   /** Point to the next byte to be written */ 
   pBuffer++; 
   /** Test on EV8 and clear it */  
   sEETimeout = sEE_LONG_TIMEOUT;
   while (!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_TRANSMITTED))
   {
     if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
   }		   
	} 
	/** Send STOP condition */ 
  I2C_GenerateSTOP(I2Cx, ENABLE); 
  I2C_EE_WaitEepromStandbyState();
  I2C_EE_WriteOnePageCompleted();

#elif PROCESS_MODE==1
	/** initialize static parameter */
	MasterDirection = Transmitter;
	i2c_comm_state = COMM_PRE;
	/** initialize static parameter according to input parameter */ 
	SlaveADDR = EEPROM_ADDRESS; /// this byte shoule be send by F/W (in loop or ISR way)
	DeviceOffset = WriteAddr; /// this byte can be send by both F/W and DMA
	OffsetDone = FALSE;  
	memcpy(SendBuf,pBuffer,NumByteToWrite);	
	BufCount=0;
	Int_NumByteToWrite=NumByteToWrite;
	I2C_AcknowledgeConfig(I2C1, ENABLE);
	I2C_INTConfig(I2C1, I2C_INT_EVT | I2C_INT_BUF | I2C_INT_ERR, ENABLE);

	/** Send START condition */
  sEETimeout = sEE_LONG_TIMEOUT;
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	I2C_GenerateSTART(I2C1, ENABLE);
  I2C_EE_WaitOperationIsCompleted();
  I2C_EE_WriteOnePageCompleted();
 
#elif PROCESS_MODE==2
  DMA_InitType  DMA_InitStructure;
  NVIC_InitType NVIC_InitStructure;
	/** DMA initialization */
	DMA_Reset(DMA1_Channel6);
	DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&I2Cx->DT;/// (u32)I2C1_DR_Address;
	DMA_InitStructure.DMA_MemoryBaseAddr = (u32)pBuffer; /// from function input parameter
	DMA_InitStructure.DMA_Direction = DMA_DIR_PERIPHERALDST; /// fixed for send function
	DMA_InitStructure.DMA_BufferSize = NumByteToWrite; /// from function input parameter
	DMA_InitStructure.DMA_PeripheralInc = DMA_PERIPHERALINC_DISABLE; // fixed
	DMA_InitStructure.DMA_MemoryInc = DMA_MEMORYINC_ENABLE; /// fixed
	DMA_InitStructure.DMA_PeripheralDataWidth = DMA_MEMORYDATAWIDTH_BYTE; ///fixed
	DMA_InitStructure.DMA_MemoryDataWidth = DMA_MEMORYDATAWIDTH_BYTE; ///fixed
	DMA_InitStructure.DMA_Mode = DMA_MODE_NORMAL; /// fixed
	DMA_InitStructure.DMA_Priority = DMA_PRIORITY_VERYHIGH;  /// up to user
	DMA_InitStructure.DMA_MTOM = DMA_MEMTOMEM_DISABLE; /// fixed
	
	DMA_Init(DMA1_Channel6, &DMA_InitStructure);
	DMA_INTConfig(DMA1_Channel6, DMA_INT_TC, ENABLE);
	
	NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel6_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
		    
  i2c_comm_state = COMM_PRE;
  sEETimeout = sEE_LONG_TIMEOUT;
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	 /** Send START condition */
	I2C_GenerateSTART(I2Cx, ENABLE); 
	 /** Test on EV5 and clear it */
  sEETimeout = sEE_LONG_TIMEOUT;
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))	
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	/** Send EEPROM address for write */ 
	I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Transmit); 
	/** Test on EV6 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_ADDRESS))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}
	/** Send the EEPROM's internal address to write to */ 
	I2C_SendData(I2Cx, WriteAddr); 
	/** Test on EV8 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
	while(! I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_TRANSMITTED))
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}		
 
  I2C_DMACmd(I2C1, ENABLE);
  DMA_ChannelEnable(DMA1_Channel6, ENABLE);
  sEETimeout = sEE_LONG_TIMEOUT;
	while(i2c_comm_state!=COMM_IN_PROCESS)
	{
	  if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
	}		 
  I2C_EE_WaitEepromStandbyState();
  I2C_EE_WriteOnePageCompleted();
#endif 
}

/**
  * @brief  Process Write one page completed.
  * @param  None
  * @retval None
  */
void I2C_EE_WriteOnePageCompleted(void)
{
	I2C_pBuffer += I2C_NumByteWritingNow;
	I2C_WriteAddr += I2C_NumByteWritingNow;
	I2C_NumByteToWrite -= I2C_NumByteWritingNow;
}

/**
  * @brief  Reads a block of data from the EEPROM.
  * @param  pBuffer: pointer to the buffer that receives the data read
  *                  from the EEPROM.
  * @param  ReadAddr: EEPROM's internal address to read from.
  * @param  NumByteToRead: number of bytes to read from the EEPROM.
  * @retval None
  */
void I2C_EE_ReadBuffer(u8* pBuffer, u16 ReadAddr, u16 NumByteToRead)
{
#if PROCESS_MODE==0	
  sEETimeout = sEE_LONG_TIMEOUT;
  while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  I2Cx->CTRL1 &= ~0x0800; //clear POSEN
  I2C_AcknowledgeConfig(I2Cx, ENABLE); 
  /** Send START condition */ 
  I2C_GenerateSTART(I2Cx, ENABLE); 
	
	/** Test on EV5 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }  
	/** Send EEPROM address for write */ 
	I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Transmit); 
	/** Test on EV6 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT; 
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_ADDRESS))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  /** Clear EV6 by setting again the PE bit */ 
  I2C_Cmd(I2Cx, ENABLE); 
  /** Send the EEPROM's internal address to write to */ 
  I2C_SendData(I2Cx, ReadAddr); 
  /** Test on EV8 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_TRANSMITTED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }    
	/** Send STRAT condition a second time */ 
  I2C_GenerateSTART(I2Cx, ENABLE); 
  /** Test on EV5 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }    
	/** Send EEPROM address for read */ 
  I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Receive); 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDRF))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  } 

 /** While there is data to be read */ 
  if(NumByteToRead==1)
  {
    /** Disable Acknowledgement */ 
    I2C_AcknowledgeConfig(I2Cx, DISABLE); 
    (void)(I2Cx->STS1); /// clear ADDR
    (void)(I2Cx->STS2);
    I2C_GenerateSTOP(I2Cx, ENABLE);
  }
  else if(NumByteToRead==2)
  {
    I2Cx->CTRL1 |= 0x0800; /// set POSEN
    (void)(I2Cx->STS1);
    (void)(I2Cx->STS2);
    I2C_AcknowledgeConfig(I2Cx, DISABLE); 
  }
  else
  {
    I2C_AcknowledgeConfig(I2Cx, ENABLE); 
    (void)(I2Cx->STS1);
    (void)(I2Cx->STS2);
  }
  while(NumByteToRead) 
  { 
    if(NumByteToRead <= 3) 
    {
      /** One byte */
      if(NumByteToRead==1)
      {
        /** Wait until RXNE flag is set */
        sEETimeout = sEE_LONG_TIMEOUT;
        while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_RECEIVED))
        {
          if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
        } 
        /** Read data from DR */
        /** Read a byte from the EEPROM */ 
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 
      }
      /** Two bytes */
      else if(NumByteToRead == 2)
      {
        /** Wait until BTF flag is set */
        sEETimeout = sEE_LONG_TIMEOUT;
        while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTFF))
        {
          if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
        } 
        /** Send STOP Condition */ 
        I2C_GenerateSTOP(I2Cx, ENABLE);

        /** Read data from DR */
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 
        /** Read data from DR */
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 
      }
      /** 3 Last bytes */
      else
      {
        sEETimeout = sEE_LONG_TIMEOUT;
        while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTFF))
        {
          if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
        } 
        /** Disable Acknowledgement */ 
        I2C_AcknowledgeConfig(I2Cx, DISABLE); 
        /** Read data from DR */
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 

        /** Wait until BTF flag is set */
        sEETimeout = sEE_LONG_TIMEOUT;
        while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTFF))
        {
          if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
        } 
        /** Send STOP Condition */ 
        I2C_GenerateSTOP(I2Cx, ENABLE);

        /** Read data from DR */
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 

        /** Read data from DR */
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 
      }
   }
   else
   {
      /** Test on EV7 and clear it */
      sEETimeout = sEE_LONG_TIMEOUT;
      while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_RECEIVED))
      {
        if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
      } 
      /** Read a byte from the EEPROM */ 
      *pBuffer = I2C_ReceiveData(I2Cx); 
      /** Point to the next location where the byte read will be saved */ 
      pBuffer++; 
      /** Decrement the read bytes counter */ 
      NumByteToRead--; 
      if(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTFF))
      {
        /** Read a byte from the EEPROM */ 
        *pBuffer = I2C_ReceiveData(I2Cx); 
        /** Point to the next location where the byte read will be saved */ 
        pBuffer++; 
        /** Decrement the read bytes counter */ 
        NumByteToRead--; 
      }
    } 
  } 
 
#elif PROCESS_MODE==1
  I2C_pBuffer = pBuffer;
  MasterDirection = Receiver;

  /** initialize static parameter according to input parameter*/ 
  SlaveADDR = EEPROM_ADDRESS;
  DeviceOffset = ReadAddr;
  OffsetDone = FALSE;
  i2c_comm_state = COMM_PRE;
  I2C_AcknowledgeConfig(I2C1, ENABLE);
  I2C_INTConfig(I2C1, I2C_INT_EVT | I2C_INT_BUF | I2C_INT_ERR, ENABLE);
  Int_NumByteToRead = NumByteToRead;

  sEETimeout = sEE_LONG_TIMEOUT;
  while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
	I2C_GenerateSTART(I2C1, ENABLE); 
  I2C_EE_WaitOperationIsCompleted();
 
#elif PROCESS_MODE==2
  DMA_InitType  DMA_InitStructure;
  NVIC_InitType NVIC_InitStructure;
  /** DMA initialization */
  DMA_Reset(DMA1_Channel7);
  DMA_InitStructure.DMA_PeripheralBaseAddr = (u32)&I2C1->DT;/// (u32)I2C1_DR_Address;

  DMA_InitStructure.DMA_MemoryBaseAddr = (u32)pBuffer; /// from function input parameter
  DMA_InitStructure.DMA_Direction = DMA_DIR_PERIPHERALSRC; /// fixed for receive function
  DMA_InitStructure.DMA_BufferSize = NumByteToRead; /// from function input parameter
  DMA_InitStructure.DMA_PeripheralInc = DMA_PERIPHERALINC_DISABLE; // fixed
  DMA_InitStructure.DMA_MemoryInc = DMA_MEMORYINC_ENABLE; /// fixed
  DMA_InitStructure.DMA_PeripheralDataWidth = DMA_MEMORYDATAWIDTH_BYTE; ///fixed
  DMA_InitStructure.DMA_MemoryDataWidth = DMA_MEMORYDATAWIDTH_BYTE; ///fixed
  DMA_InitStructure.DMA_Mode = DMA_MODE_NORMAL; /// fixed
  DMA_InitStructure.DMA_Priority = DMA_PRIORITY_VERYHIGH;  /// up to user
  DMA_InitStructure.DMA_MTOM = DMA_MEMTOMEM_DISABLE; /// fixed

  DMA_Init(DMA1_Channel7, &DMA_InitStructure);
  DMA_INTConfig(DMA1_Channel7, DMA_INT_TC, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel7_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  /** global state variable i2c_comm_state */
  i2c_comm_state = COMM_PRE;

  sEETimeout = sEE_LONG_TIMEOUT;
  while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  I2Cx->CTRL1 &= ~0x0800; /// clear POSEN
  I2C_AcknowledgeConfig(I2Cx, ENABLE); 
  /** Send START condition */ 
  I2C_GenerateSTART(I2Cx, ENABLE); 

  /** Test on EV5 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }  
	/** Send EEPROM address for write */ 
	I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Transmit); 
  /** Test on EV6 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT; 
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_ADDRESS))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
  /** Clear EV6 by setting again the PE bit */ 
  I2C_Cmd(I2Cx, ENABLE); 
  /** Send the EEPROM's internal address to write to */ 
  I2C_SendData(I2Cx, ReadAddr); 
  /* Test on EV8 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_DATA_TRANSMITTED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }    
	/** Send STRAT condition a second time */ 
  I2C_GenerateSTART(I2Cx, ENABLE); 
  /** Test on EV5 and clear it */ 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }    
  /** Send EEPROM address for read */ 
  I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Receive); 
  sEETimeout = sEE_LONG_TIMEOUT;
  while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_ADDRF))
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  } 
  I2C_DMALastTransferCmd(I2C1, ENABLE);
  I2C_DMACmd(I2C1, ENABLE);
  DMA_ChannelEnable(DMA1_Channel7, ENABLE);  
  (void)(I2Cx->STS1);
  (void)(I2Cx->STS2);
  I2C_EE_WaitOperationIsCompleted();
#endif 
}

/**
  * @brief  wait operation is completed.
  * @param  None
  * @retval None
  */
void I2C_EE_WaitOperationIsCompleted(void)
{
  sEETimeout = sEE_LONG_TIMEOUT;
  while(i2c_comm_state!=COMM_DONE)
  {
    if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
  }
}	

/**
  * @brief  I2c1 event interrupt Service Routines.
  * @param  None
  * @retval None
  */
void i2c1_evt_handle(void)
{  
  uint32_t lastevent=  I2C_GetLastEvent(I2C1);
  switch (lastevent)
  {
    /** Master Invoke */
    case I2C_EVENT_MASTER_START_GENERATED:    /// EV5
      if(!check_begin)
      {
        i2c_comm_state = COMM_IN_PROCESS;
      }
      if (MasterDirection == Receiver)
      {
        if (!OffsetDone)
        {
          I2C_Send7bitAddress(I2C1,SlaveADDR,I2C_Direction_Transmit);
        }   
        else
        {
          /** Send slave Address for read */
          I2C_Send7bitAddress(I2C1,SlaveADDR,I2C_Direction_Receive);      
          OffsetDone = FALSE;
        }
      }
      else
      {
        /** Send slave Address for write */
        I2C_Send7bitAddress(I2C1,SlaveADDR,I2C_Direction_Transmit);
      }
    break;
    /** Master Receiver events */
    case I2C_EVENT_MASTER_ADDRESS_WITH_RECEIVER:  /// EV6
    break;
    case I2C_EVENT_MASTER_DATA_RECEIVED:    /// EV7
      *I2C_pBuffer = I2C_ReceiveData(I2C1);
      I2C_pBuffer++;
      Int_NumByteToRead--;
      if(Int_NumByteToRead==1) 
      {
        /** Disable Acknowledgement */ 
        I2C_AcknowledgeConfig(I2C1, DISABLE); 
        I2C_GenerateSTOP(I2C1, ENABLE);
      }
      if(Int_NumByteToRead==0) 
      {
        I2C_INTConfig(I2C1, I2C_INT_EVT | I2C_INT_BUF |I2C_INT_ERR, DISABLE);
        i2c_comm_state = COMM_DONE;
      }
    break;
    /** Master Transmitter events */
    case I2C_EVENT_MASTER_ADDRESS|I2C_EVENT_MASTER_TRANSMITTER:  /// EV8 just after EV6
      if (check_begin)
      {
        check_begin = FALSE;
        I2C_INTConfig(I2C1, I2C_INT_EVT | I2C_INT_BUF |I2C_INT_ERR, DISABLE);
        I2C_GenerateSTOP(I2C1, ENABLE);
        i2c_comm_state = COMM_DONE;
        break;
      }
      I2C_SendData(I2C1, DeviceOffset);
      OffsetDone = TRUE;
    break;
    case I2C_EVENT_MASTER_DATA_TRANSMITTING:   ///  EV8 I2C_EVENT_MASTER_DATA_TRANSMITTING
      if (MasterDirection == Receiver)
      {
        I2C_GenerateSTART(I2C1, ENABLE);
      }
    break;
    case I2C_EVENT_MASTER_DATA_TRANSMITTED:    /// EV8-2 
      if (MasterDirection == Transmitter)
      {
        if(Int_NumByteToWrite==0)
        {    
          I2C_GenerateSTOP(I2C1, ENABLE); 
          sEETimeout = sEE_LONG_TIMEOUT;
          while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
          {
            if((sEETimeout--) == 0) sEE_TIMEOUT_UserCallback();     
          }
          check_begin = TRUE;
          I2C_GenerateSTART(I2C1, ENABLE);
        }else
        {
          I2C_SendData(I2C1, SendBuf[BufCount++]);
          Int_NumByteToWrite--;    
        }
      }
    break;
  }
}

/**
  * @brief  I2c1 error interrupt Service Routines.
  * @param  None
  * @retval None
  */
void i2c1_err_handle(void)
{
  if (I2C_GetFlagStatus(I2C1, I2C_FLAG_ACKFAIL))
  {
    if (check_begin) /// EEPROM write busy
    {
      I2C_GenerateSTART(I2C1, ENABLE);
    }
    else if (I2C1->STS2 &0x01) /// real fail
    {	
      I2C_GenerateSTOP(I2C1, ENABLE);
      i2c_comm_state = COMM_EXIT;
    }
    I2C_ClearFlag(I2C1, I2C_FLAG_ACKFAIL);
  }

  if (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSERR))
  {
    if (I2C1->STS2 &0x01)
    {
      I2C_GenerateSTOP(I2C1, ENABLE);
      i2c_comm_state = COMM_EXIT;
    }
    I2C_ClearFlag(I2C1, I2C_FLAG_BUSERR);
  }
}

/**
  * @brief  I2c1 dma send interrupt Service Routines.
  * @param  None
  * @retval None
  */
void i2c1_send_dma_handle()
{
  if (DMA_GetFlagStatus(DMA1_FLAG_TC6))
  {
    if (I2Cx->STS2 & 0x01) /// master send DMA finish, check process later
    {
      /** DMA1-6 (I2Cx Tx DMA)transfer complete ISR */
      I2C_DMACmd(I2Cx, DISABLE);
      DMA_ChannelEnable(DMA1_Channel6, DISABLE);
      /** wait until BTF */
      while(!I2C_GetFlagStatus(I2Cx, I2C_FLAG_BTFF));
      I2C_GenerateSTOP(I2Cx, ENABLE);
      /** wait until BUSY clear */
      while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF));   
      i2c_comm_state = COMM_IN_PROCESS;
    }
    else /// slave send DMA finish
    {
    }
    DMA_ClearFlag(DMA1_FLAG_TC6);
  }
  if (DMA_GetFlagStatus(DMA1_FLAG_GL6))
  {
    DMA_ClearFlag( DMA1_FLAG_GL6);
  }
  if (DMA_GetFlagStatus(DMA1_FLAG_HT6))
  {
    DMA_ClearFlag( DMA1_FLAG_HT6);
  }
}

/**
  * @brief  I2c1 dma receive interrupt Service Routines.
  * @param  None
  * @retval None
  */
void i2c1_receive_dma_handle(void)
{
  if (DMA_GetFlagStatus(DMA1_FLAG_TC7))
  {
    if (I2Cx->STS2 & 0x01) /// master receive DMA finish
    {
      I2C_DMACmd(I2Cx, DISABLE);
      I2C_GenerateSTOP(I2Cx, ENABLE);
      i2c_comm_state = COMM_DONE;
    }
    else /// slave receive DMA finish
    {
    }
    DMA_ClearFlag(DMA1_FLAG_TC7);
  }
  if (DMA_GetFlagStatus(DMA1_FLAG_GL7))
  {
    DMA_ClearFlag( DMA1_FLAG_GL7);
  }
  if (DMA_GetFlagStatus(DMA1_FLAG_HT7))
  {
    DMA_ClearFlag( DMA1_FLAG_HT7);
  }
}

/**
  * @brief  Wait eeprom standby state.
  * @param  None
  * @retval None
  */
void I2C_EE_WaitEepromStandbyState(void)
{

	__IO uint16_t tmpSR1 = 0;
  __IO uint32_t sEETrials = 0;

  /** While the bus is busy */
  sEETimeout = sEE_LONG_TIMEOUT;
  while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSYF))
  {
    if((sEETimeout--) == 0) sEE_TIMEOUT_UserCallback();     
  }

  /** Keep looping till the slave acknowledge his address or maximum number 
     of trials is reached (this number is defined by sEE_MAX_TRIALS_NUMBER) */
  while (1)
  {
    /** Send START condition */
    I2C_GenerateSTART(I2Cx, ENABLE);

    /** Test on EV5 and clear it */
    sEETimeout = sEE_LONG_TIMEOUT;
    while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_START_GENERATED))
    {
      if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
    }    

    /** Send EEPROM address for write */
    I2C_Send7bitAddress(I2Cx, EEPROM_ADDRESS, I2C_Direction_Transmit); 
    /** Wait for ADDR flag to be set (Slave acknowledged his address) */
    sEETimeout = sEE_LONG_TIMEOUT;
    do
    {     
      /** Get the current value of the SR1 register */
      tmpSR1 = I2Cx->STS1;
      
      /** Update the timeout value and exit if it reach 0 */
      if((sEETimeout--) == 0)  sEE_TIMEOUT_UserCallback();
    }
    /** Keep looping till the Address is acknowledged or the AF flag is 
       set (address not acknowledged at time) */
    while((tmpSR1 & (I2C_STS1_ADDRF | I2C_STS1_ACKFAIL)) == 0);
     
    /** Check if the ADDR flag has been set */
    if (tmpSR1 & I2C_STS1_ADDRF)
    {
      /** Clear ADDR Flag by reading SR1 then SR2 registers (SR1 have already 
         been read) */
      (void)I2Cx->STS2;
      
      /** STOP condition */    
      I2C_GenerateSTOP(I2Cx, ENABLE);
        
      /** Exit the function */
      return;
    }
    else
    {
      /** Clear AF flag */
      I2C_ClearFlag(I2Cx, I2C_FLAG_ACKFAIL);                  
    }
    
    /** Check if the maximum allowed numbe of trials has bee reached */
    if (sEETrials++ == sEE_MAX_TRIALS_NUMBER)
    {
      /** If the maximum number of trials has been reached, exit the function */
       sEE_TIMEOUT_UserCallback();
    }
  }
}

/**
  * @}
  */ 

/**
  * @}
  */ 

/******************* (C) COPYRIGHT 2018 ArteryTek *****END OF FILE****/ 
