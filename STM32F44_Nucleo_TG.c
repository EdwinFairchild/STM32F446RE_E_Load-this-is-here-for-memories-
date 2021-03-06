/*

Activate USE NUCLEO in CONFIG 
And USE TIM 2 for delay

DAC max output = 3.279V

ADC Ch 0 is reading V drop across R_SHUNT : storing value in adc_val[0]
ADC Ch 1 is reading Pot :storing value in adc_val[1]

*/




#include <stm32f4xx_ll_bus.h>
#include <stm32f4xx_ll_gpio.h>
#include <stm32f4xx_ll_spi.h>
#include <stm32f4xx_ll_i2c.h>
#include <stm32f4xx_ll_usart.h>
#include <stm32f4xx_ll_adc.h>
#include <stm32f4xx_ll_dac.h>
#include <stm32f4xx_ll_dma.h>
#include <stm32f4xx_ll_exti.h>
#include <stm32f4xx_ll_utils.h>
#include <stm32f4xx_ll_rcc.h>
#include <stm32f4xx_ll_pwr.h>
#include <stm32f4xx_ll_system.h>
#include <stm32f4xx_ll_cortex.h>
#include "math.h"
//---my custyom libs
#include "CL_CONFIG.h"
#include "CL_bfp.h"
#include "CL_delay.h"
#include "CL_printMsg.h"
#include <stdlib.h>

//--Display Driver
#include "st7735.h"
#include "fonts.h"
#include "st7735_cfg.h"
#include "testimg.h"
#include "testimg2.h"
#include "top_i.h"
#include "top_p.h"
#include "top_r.h"

#include "ssd1306.h"
#include "ssd1306_fonts.h"


CL_cli_type cli;
#define R_SHUNT		0.47  //value of my shunt resistor
#define ERROR		5	  //trimming error
uint32_t adc_setpoint = 0;
uint32_t adc_val[2] ;
uint32_t dac_setpoint; 


// this is the voltage for every DAC tick: 3.3/4096 to make it a multiplicatioin we just take the reciprical and multiply
#define ADC_OFFSET 1241.21 


//-------------------------------| Prorotypes |-------------------------------
void SystemClock_Config(void);
void MX_SPI1_Init(void);

void init_led(void);
void encoder_init(void);
void button_interrupt(void);
void MX_ADC1_Init(void);
void MX_DAC_Init(void);
void MX_I2C1_Init(void);
void i2c_w_byte(uint8_t address, uint8_t *data, uint8_t len);

void tft_gpio_init(void);
void spiSend(uint8_t *data, uint8_t len);
void spiSend16(uint16_t *data, uint32_t len);


void drawr_img(void);
void drawr_img2(void);
void drawr_top_i(void);
void drawr_top_r(void);
void drawr_top_p(void);
void setCurrent(float  i); 
void setPower(float p);
void cmd_ok_handler(uint8_t num, char *values[]);
void cmd_setPoint_handler(uint8_t num, char *values[]);
void cmd_setDac_handler(uint8_t num, char *values[]);
void cmd_setCurrent_handler(uint8_t num, char *values[]);
void cmd_getDac_handler(uint8_t num, char *values[]);
void cmd_getAdc_handler(uint8_t num, char *values[]);


//-------------------------------| Finite State Machine Stuff |-------------------------------
typedef enum redrawState
{
	REDRAW_NO = 0,
	REDRAW_YES

}redrawState;

redrawState redraw_state = REDRAW_YES;

void(*redraw_state_function[3])(void); 


typedef enum outputState
{
	OUTPUT_ON = 0,
	OUTPUT_OFF

}outputState;

outputState output_state = OUTPUT_OFF;


typedef enum mainState
{
	STATE_R = 0,
	STATE_I,
	STATE_P
}mainState;


mainState main_state = STATE_R;



void(*state_function[3])(void);

void state_function_R(void);
void state_function_I(void);
void state_function_P(void);


//-------------------------------| Global Values |-------------------------------
uint16_t encoder_previous_val = 0; 
uint16_t encoder_current_val = 0;
float current_value = 0.0;  //will be in resolution 10mA


int main(void)
{
	SystemClock_Config();
	
	CL_delay_init();
	init_led();
	encoder_init();
	button_interrupt();
	CL_printMsg_init_Default(true);
	CL_printMsg("CL Libs initialized\n");


	//-----MX init functions
//	MX_ADC1_Init(); //PA0(CH0 ) and PA1 (CH1): one is for rshunt and other  
//	MX_DAC_Init();  //A2 goes to op amp
	MX_SPI1_Init(); //display
//	MX_I2C1_Init(); ads1115


	//-------------ST7735
	tft_gpio_init();
	ST7735_Reset();
	ST7735_Backlight_On();
	ST7735_Init();
	//ST7735_SetRotation(0);
	//ST7735_FillScreen(ST7735_RED);
	//drawr_img();

	
//	CL_cli_init(&cli);
//	cli.prompt = "eddie> ";
//	cli.delimeter = '\r';
//	cli.registerCommand("ok", ' ', cmd_ok_handler, "Prints \"ok\" if cli is ok");
//	cli.registerCommand("setdac", ' ', cmd_setDac_handler, "Sets DAC output on PA_4");
//	cli.registerCommand("seti", '.', cmd_setCurrent_handler, "Sets Current Draw");
//	cli.registerCommand("getdac", ' ', cmd_getDac_handler, "Returns current DAC value");
//	cli.registerCommand("getadc", ' ', cmd_getAdc_handler, "Returns current ADC value");


	drawr_img();
	delayMS(1000);

	//define initial state
	output_state = OUTPUT_OFF;
	redraw_state = REDRAW_YES;
	state_function[0] = state_function_R;
	state_function[1] = state_function_I;
	state_function[2] = state_function_P;

	redraw_state_function[0] = drawr_top_r;
	redraw_state_function[1] = drawr_top_i;
	redraw_state_function[2] = drawr_top_p;

	int num = 0; 

	for (;;)
	{
	
		//	LL_ADC_REG_StartConversionSWStart(ADC1);
		
		
		//	LL_DAC_ConvertData12RightAligned(DAC, LL_DAC_CHANNEL_1, (TIM3->CNT & 0xFFF));
		//only redraw when necessary 
		if(redraw_state)
		{
			(*redraw_state_function[main_state % 3])(); 
			redraw_state = REDRAW_NO;
		}
		
		(*state_function[main_state % 3])(); //call the current state 



		
		
	}
}


void state_function_R(void)
{
	//redraw stuff
	

	//handle encoder stuff

	//if output is on do it else dont
}
void state_function_I(void)
{
    
	//if encoder has incremented counter
	encoder_current_val = TIM3->CNT;

	if (encoder_previous_val != encoder_current_val)
	{
		encoder_previous_val = encoder_current_val;
		if (encoder_previous_val > encoder_current_val) // when decreased in value
		{
			current_value -= 0.01;
		}
		if (encoder_previous_val < encoder_current_val) // when increased in value
		{
			current_value += 0.01; 
		}
			
		//clear where previous text was
		ST7735_FillRectangle(70, 40, 64, 26, 0xFFFF);
		//write new text000
		ST7735_printMsg(70, 40, "%.2f", current_value);// TIM3->CNT);		
	//	uint8_t stringy[4];
	//	sprintf(stringy, "%d", TIM3->CNT);
		
		//-----------------------------

		//encoder
	//	CL_printMsg("%d\n", TIM3->CNT);

		//LL_GPIO_TogglePin(GPIOA, LL_GPIO_PIN_5);
			
		//delayMS(10);
	}

	//if output is on do it else dont
}
void state_function_P(void)
{
	


	//if output is on do it else dont
}

void drawr_top_i(void)
{
	ST7735_DrawImage(0, 0, ST7735_WIDTH, ST7735_HEIGHT, (uint16_t*)top_i);
	//   HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
	
	    // Display test image 128x128 pixel by pixel
	    for(int x = 0 ; x < ST7735_WIDTH ; x++) 
	{
		for (int y = 0; y < 25; y++) {
			uint16_t color565 = top_i[y][x];
			// fix endiness
			color565 = ((color565 & 0xFF00) >> 8) | ((color565 & 0xFF) << 8);
			ST7735_DrawPixel(x, y, color565);
		}
	}
	
	//  HAL_Delay(15000);
	  //HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
}
void drawr_top_r(void)
{
	ST7735_DrawImage(0, 0, ST7735_WIDTH, ST7735_HEIGHT, (uint16_t*)top_r);
	//   HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
	
	    // Display test image 128x128 pixel by pixel
	    for(int x = 0 ; x < ST7735_WIDTH ; x++) 
	{
		for (int y = 0; y < 25; y++) {
			uint16_t color565 = top_r[y][x];
			// fix endiness
			color565 = ((color565 & 0xFF00) >> 8) | ((color565 & 0xFF) << 8);
			ST7735_DrawPixel(x, y, color565);
		}
	}
	
	//  HAL_Delay(15000);
	  //HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
}
void drawr_top_p(void)
{
	ST7735_DrawImage(0, 0, ST7735_WIDTH, ST7735_HEIGHT, (uint16_t*)top_p);
	//   HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
	
	    // Display test image 128x128 pixel by pixel
	    for(int x = 0 ; x < ST7735_WIDTH ; x++) 
	{
		for (int y = 0; y < 25; y++) {
			uint16_t color565 = top_p[y][x];
			// fix endiness
			color565 = ((color565 & 0xFF00) >> 8) | ((color565 & 0xFF) << 8);
			ST7735_DrawPixel(x, y, color565);
		}
	}
	
	//  HAL_Delay(15000);
	  //HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
}



void encoder_init(void)
{
	//config encoder stuff

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN | RCC_AHB1ENR_GPIOAEN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

	LL_GPIO_InitTypeDef encoderPin;
	LL_GPIO_StructInit(&encoderPin);
	encoderPin.Mode = LL_GPIO_MODE_ALTERNATE;
	encoderPin.Pin	= LL_GPIO_PIN_7;
	encoderPin.Alternate = LL_GPIO_AF_2;
	LL_GPIO_Init(GPIOC, &encoderPin);

	encoderPin.Pin	= LL_GPIO_PIN_6;
	LL_GPIO_Init(GPIOA, &encoderPin);


	TIM3->ARR = 0xFFFF;

	//steps according to page 547 of the RM
	TIM3->CCMR1 |= TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  //step 1 and 2
	TIM3->CCER  &= ~(TIM_CCER_CC1P | TIM_CCER_CC2P);  //step 3 and 4 
	TIM3->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;

	LL_TIM_InitTypeDef tim_base;
	LL_TIM_StructInit(&tim_base);
	LL_TIM_Init(TIM3, &tim_base);
	LL_TIM_ENCODER_InitTypeDef tim;
	LL_TIM_ENCODER_StructInit(&tim);
	tim.EncoderMode = LL_TIM_ENCODERMODE_X2_TI1;
	LL_TIM_ENCODER_Init(TIM3, &tim);
	TIM3->CR1 |= TIM_CR1_CEN;
}

void init_led(void)
{
	//initled
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_5, LL_GPIO_MODE_OUTPUT);
	LL_GPIO_SetPinOutputType(GPIOA, LL_GPIO_PIN_5, LL_GPIO_OUTPUT_PUSHPULL);
	LL_GPIO_SetPinSpeed(GPIOA, LL_GPIO_PIN_5, LL_GPIO_SPEED_FREQ_LOW);
}
void button_interrupt(void)
{
	RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; 
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);
	LL_SYSCFG_SetEXTISource(LL_SYSCFG_EXTI_PORTC, LL_SYSCFG_EXTI_LINE13);

	LL_GPIO_ResetOutputPin(GPIOC, LL_GPIO_PIN_13);
	LL_EXTI_InitTypeDef EXTI_InitStruct = { 0 };
	EXTI_InitStruct.Line_0_31 = LL_EXTI_LINE_13;
	EXTI_InitStruct.LineCommand = ENABLE;
	EXTI_InitStruct.Mode = LL_EXTI_MODE_IT;
	EXTI_InitStruct.Trigger = LL_EXTI_TRIGGER_FALLING;
	LL_EXTI_Init(&EXTI_InitStruct);

	/**/
	LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_13, LL_GPIO_PULL_NO);
	LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_13, LL_GPIO_MODE_INPUT);
	__NVIC_EnableIRQ(EXTI15_10_IRQn);
}
void EXTI15_10_IRQHandler(void)
{
	LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_13);
	EXTI->PR = 1 <<  13;
	__NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
	output_state = OUTPUT_OFF;
	main_state++;
	redraw_state = REDRAW_YES;
}



void SystemClock_Config(void)
{
	LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);

	if (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_5)
	{
		//Error_Handler();  
	}
	LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
	LL_PWR_EnableOverDriveMode();
	LL_RCC_HSE_Enable();

	/* Wait till HSE is ready */
	while (LL_RCC_HSE_IsReady() != 1)
	{
    
	}
	LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_4, 180, LL_RCC_PLLP_DIV_2);
	LL_RCC_PLL_Enable();

	/* Wait till PLL is ready */
	while (LL_RCC_PLL_IsReady() != 1)
	{
    
	}
	LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
	LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_4);
	LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
	LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);

	/* Wait till System clock is ready */
	while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
	{
  
	}
	LL_Init1msTick(180000000);
	LL_SYSTICK_SetClkSource(LL_SYSTICK_CLKSOURCE_HCLK);
	LL_SetSystemCoreClock(180000000);
	LL_RCC_SetTIMPrescaler(LL_RCC_TIM_PRESCALER_TWICE);
}
void MX_SPI1_Init(void)
{

	/* USER CODE BEGIN SPI1_Init 0 */

	/* USER CODE END SPI1_Init 0 */

	LL_SPI_InitTypeDef SPI_InitStruct = { 0 };

	LL_GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	/* Peripheral clock enable */
	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
  
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
	/**SPI1 GPIO Configuration  
	PA7   ------> SPI1_MOSI
	PB3   ------> SPI1_SCK 
	*/
	GPIO_InitStruct.Pin = LL_GPIO_PIN_7;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
	GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
	GPIO_InitStruct.Alternate = LL_GPIO_AF_5;
	LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = LL_GPIO_PIN_3;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
	GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
	GPIO_InitStruct.Alternate = LL_GPIO_AF_5;
	LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* USER CODE BEGIN SPI1_Init 1 */

	/* USER CODE END SPI1_Init 1 */
	/* SPI1 parameter configuration*/
	SPI_InitStruct.TransferDirection = LL_SPI_HALF_DUPLEX_TX;
	SPI_InitStruct.Mode = LL_SPI_MODE_MASTER;
	SPI_InitStruct.DataWidth = LL_SPI_DATAWIDTH_8BIT;
	SPI_InitStruct.ClockPolarity = LL_SPI_POLARITY_LOW;
	SPI_InitStruct.ClockPhase = LL_SPI_PHASE_1EDGE;
	SPI_InitStruct.NSS = LL_SPI_NSS_SOFT;
	SPI_InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV2;
	SPI_InitStruct.BitOrder = LL_SPI_MSB_FIRST;
	SPI_InitStruct.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
	SPI_InitStruct.CRCPoly = 10;
	LL_SPI_Init(SPI1, &SPI_InitStruct);
	LL_SPI_SetStandard(SPI1, LL_SPI_PROTOCOL_MOTOROLA);
	/* USER CODE BEGIN SPI1_Init 2 */
	LL_SPI_Enable(SPI1);
	/* USER CODE END SPI1_Init 2 */

}
void USART2_IRQHandler(void)
{
//	if ((USART2->SR & USART_SR_RXNE)) //if data has arrived on the uart
//		{
//			USART2->SR &= ~(USART_SR_RXNE); //clear interrupt
//
//			//fetch data
//			cli.charReceived = USART2->DR;
//
//			//if the character receieved is not the delimeter then echo the character
//			if(cli.charReceived != cli.delimeter)
//				USART2->DR = cli.charReceived;
//			cli.parseChar(&cli);
//		}


}
void MX_ADC1_Init(void)
{

	/*	ADC1 
		Channel 0 (ADC_IN_0) : PA_0  Datasheet pg57
		Channel 1 (ADC_IN_6) : PA_1
		
		DMA2 -> ADC1 : STREAM 0 : Channel 0 : RM pg 308
		*/
	

	LL_ADC_InitTypeDef ADC_InitStruct = { 0 };
	LL_ADC_REG_InitTypeDef ADC_REG_InitStruct = { 0 };
	LL_ADC_CommonInitTypeDef ADC_CommonInitStruct = { 0 };
	LL_GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	//enalbe periph clocks
	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC1);  
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

	//gpio setup as analog 
	GPIO_InitStruct.Pin = LL_GPIO_PIN_0 | LL_GPIO_PIN_1;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ANALOG;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
	LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	/*	Configure ADC in continuous scan mode
		sequence register 3 will have channel 0 as
		first sequence followed by channel 1 as second sequence
		DMa unlimited transfer will be enalbed*/

	ADC_InitStruct.Resolution			= LL_ADC_RESOLUTION_12B;
	ADC_InitStruct.DataAlignment		= LL_ADC_DATA_ALIGN_RIGHT;
	ADC_InitStruct.SequencersScanMode	= LL_ADC_SEQ_SCAN_ENABLE;   //enablde/disable sequence in scan mode
	LL_ADC_Init(ADC1, &ADC_InitStruct);
	ADC_REG_InitStruct.TriggerSource	= LL_ADC_REG_TRIG_SOFTWARE;
	ADC_REG_InitStruct.SequencerLength	= LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS;  //number of sequences
	ADC_REG_InitStruct.ContinuousMode	= LL_ADC_REG_CONV_CONTINUOUS; 		 //continuous mode
	ADC_REG_InitStruct.DMATransfer		= LL_ADC_REG_DMA_TRANSFER_UNLIMITED;  //dma transfet must be set to unlimited
	LL_ADC_REG_Init(ADC1, &ADC_REG_InitStruct);

	//LL_ADC_REG_SetFlagEndOfConversion(ADC1, LL_ADC_REG_FLAG_EOC_SEQUENCE_CONV); //dont need this unless i find a reason to use the flag
	//make sure interrupts are disabled
	LL_ADC_DisableIT_EOCS(ADC1);



	ADC_CommonInitStruct.CommonClock	= LL_ADC_CLOCK_SYNC_PCLK_DIV4;  //90Mhz / 4  ADC max clock is 30MHz Datasheet pg 158
	ADC_CommonInitStruct.Multimode		= LL_ADC_MULTI_INDEPENDENT;
	LL_ADC_CommonInit(__LL_ADC_COMMON_INSTANCE(ADC1), &ADC_CommonInitStruct);

	//sets channel 0 as the first sequence (rank) to be sampled
	LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_0);
	LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_0, LL_ADC_SAMPLINGTIME_480CYCLES);

	//sets channel 1 as the second sequence (rank) to be sampled
	LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_1);
	LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_1, LL_ADC_SAMPLINGTIME_480CYCLES);

	
	LL_DMA_InitTypeDef dma;
	LL_DMA_StructInit(&dma);
	
	//DMA2 -> ADC1 : STREAM 0 : Channel 0 : RM pg 308
	dma.PeriphOrM2MSrcAddress	= (uint32_t)(&ADC1->DR);
	dma.PeriphOrM2MSrcDataSize	= LL_DMA_PDATAALIGN_WORD;
	dma.MemoryOrM2MDstAddress	= (uint32_t) &adc_val;
	dma.MemoryOrM2MDstDataSize	= LL_DMA_PDATAALIGN_WORD;
	dma.MemoryOrM2MDstIncMode   = LL_DMA_MEMORY_INCREMENT;
	dma.Direction				= LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
	dma.Mode					= LL_DMA_MODE_CIRCULAR;  //must be circular
	dma.NbData					= 2; 
	dma.Channel					= LL_DMA_CHANNEL_0;
	LL_DMA_Init(DMA2, LL_DMA_STREAM_0, &dma);
	LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_0);

	//NO interrupts when using DMA
//	LL_ADC_EnableIT_EOCS(ADC1);
//	__NVIC_EnableIRQ(ADC_IRQn);
	LL_ADC_Enable(ADC1);
	delayMS(5);
	LL_ADC_Enable(ADC1);

}
void MX_DAC_Init(void)
{

	
//
//	/*	DAC1   
//		PA4   -> DAC_OUT1 
//		DMA1  -> DAC1 : STREAM 5 : Channel 7 : RM pg 308
//
//	*/
//	LL_DAC_InitTypeDef DAC_InitStruct = { 0 };
//
//	LL_GPIO_InitTypeDef GPIO_InitStruct = { 0 };
//
//	/* Peripheral clock enable */
//	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_DAC1);
//	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
//	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
//
//	//PA_4 as analog
//	GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
//	GPIO_InitStruct.Mode = LL_GPIO_MODE_ANALOG;
//	GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
//	LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
//
//
//	//DMA1 -> DAC1 : STREAM 5 : Channel 7 : RM pg 308
//	/*
//	LL_DMA_InitTypeDef dma;
//	LL_DMA_StructInit(&dma);
//
//	dma.PeriphOrM2MSrcAddress	= (uint32_t)(&DAC->DHR12R1);
//	dma.PeriphOrM2MSrcDataSize	= LL_DMA_PDATAALIGN_WORD;
//	dma.MemoryOrM2MDstAddress	= (uint32_t) &sinX;
//	dma.MemoryOrM2MDstDataSize	= LL_DMA_PDATAALIGN_WORD;
//	dma.MemoryOrM2MDstIncMode   = LL_DMA_MEMORY_INCREMENT;
//	dma.Direction				= LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
//	dma.Mode					= LL_DMA_MODE_CIRCULAR;
//	dma.NbData					= 1000; 
//	dma.Channel					= LL_DMA_CHANNEL_7;
//	LL_DMA_Init(DMA1, LL_DMA_STREAM_5, &dma);
//	LL_DMA_EnableStream(DMA1, LL_DMA_STREAM_5);
//
//
//	//set trigger as timer 5
//	DAC_InitStruct.TriggerSource = LL_DAC_TRIG_EXT_TIM5_TRGO; //LL_DAC_TRIG_SOFTWARE
//	*/
//	LL_DAC_StructInit(&DAC_InitStruct);
//	DAC_InitStruct.TriggerSource = LL_DAC_TRIG_SOFTWARE;    //LL_DAC_TRIG_SOFTWARE
//	DAC_InitStruct.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE;
//	DAC_InitStruct.OutputBuffer = LL_DAC_OUTPUT_BUFFER_ENABLE;
//	LL_DAC_Init(DAC, LL_DAC_CHANNEL_1, &DAC_InitStruct);
//	
//	//enable everything the cube doesnt enable
//	//but do not enable the timer yet , it will start conversions
//	//If DAC trigger is disabled, DAC conversion is performed
//    //automatically once the data holding register is updated
//	LL_DAC_EnableTrigger(DAC, LL_DAC_CHANNEL_1);
////	LL_DAC_EnableDMAReq(DAC, LL_DAC_CHANNEL_1);
//	LL_DAC_Enable(DAC, LL_DAC_CHANNEL_1);


		/* USER CODE BEGIN DAC_Init 0 */

		/* USER CODE END DAC_Init 0 */

		LL_DAC_InitTypeDef DAC_InitStruct = { 0 };

		LL_GPIO_InitTypeDef GPIO_InitStruct = { 0 };

		/* Peripheral clock enable */
		LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_DAC1);
  
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
		/**DAC GPIO Configuration  
		PA4   ------> DAC_OUT1 
		*/
		GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
		GPIO_InitStruct.Mode = LL_GPIO_MODE_ANALOG;
		GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
		LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

		/* USER CODE BEGIN DAC_Init 1 */

		/* USER CODE END DAC_Init 1 */
		/** DAC channel OUT1 config 
		*/
		DAC_InitStruct.TriggerSource = LL_DAC_TRIG_SOFTWARE;
		DAC_InitStruct.WaveAutoGeneration = LL_DAC_WAVE_AUTO_GENERATION_NONE;
		DAC_InitStruct.OutputBuffer = LL_DAC_OUTPUT_BUFFER_ENABLE;
		LL_DAC_Init(DAC, LL_DAC_CHANNEL_1, &DAC_InitStruct);
		/* USER CODE BEGIN DAC_Init 2 */
	//LL_DAC_EnableTrigger(DAC, LL_DAC_CHANNEL_1);
	    LL_DAC_Enable(DAC, LL_DAC_CHANNEL_1);
		/* USER CODE END DAC_Init 2 */

	

}
void MX_I2C1_Init(void)
{

	/* USER CODE BEGIN I2C1_Init 0 */

	/* USER CODE END I2C1_Init 0 */

	LL_I2C_InitTypeDef I2C_InitStruct = { 0 };

	LL_GPIO_InitTypeDef GPIO_InitStruct = { 0 };

	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
	/**I2C1 GPIO Configuration  
	PB6   ------> I2C1_SCL
	PB7   ------> I2C1_SDA 
	*/
	GPIO_InitStruct.Pin = LL_GPIO_PIN_6 | LL_GPIO_PIN_7;
	GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
	GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
	GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
	GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
	GPIO_InitStruct.Alternate = LL_GPIO_AF_4;
	LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	/* Peripheral clock enable */
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

	/* USER CODE BEGIN I2C1_Init 1 */

	/* USER CODE END I2C1_Init 1 */
	/** I2C Initialization 
	*/
	LL_I2C_DisableOwnAddress2(I2C1);
	LL_I2C_DisableGeneralCall(I2C1);
	LL_I2C_EnableClockStretching(I2C1);
	I2C_InitStruct.PeripheralMode = LL_I2C_MODE_I2C;
	I2C_InitStruct.ClockSpeed = 400000;
	I2C_InitStruct.DutyCycle = LL_I2C_DUTYCYCLE_2;
	I2C_InitStruct.OwnAddress1 = 0;
	I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
	I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;
	LL_I2C_Init(I2C1, &I2C_InitStruct);
	LL_I2C_SetOwnAddress2(I2C1, 0);
	/* USER CODE BEGIN I2C1_Init 2 */
	LL_I2C_Enable(I2C1);
	/* USER CODE END I2C1_Init 2 */

}

void i2c_w_byte(uint8_t address , uint8_t *data , uint8_t len)
{
	
	//these 3 lines repeated for read and write so it should be a function
	LL_I2C_GenerateStartCondition(I2C1);
	while (!(I2C1->SR1 & I2C_SR1_SB)) ; 
	//delayMS(1);
	LL_I2C_TransmitData8(I2C1, 0x78);
	while (!(LL_I2C_IsActiveFlag_ADDR(I2C1))) ; 


	uint32_t temp = I2C1->SR2;
	LL_I2C_TransmitData8(I2C1, address);
	while (!(I2C1->SR1 & I2C_SR1_TXE)) ;


	for (int i = 0; i < len; i++)
	{
		LL_I2C_TransmitData8(I2C1, *data++);
	
		while (!(I2C1->SR1 & I2C_SR1_TXE)) ;
	}
	LL_I2C_GenerateStopCondition(I2C2);		
}//----------------------------------------------------------------------------------------

void tft_gpio_init(void)
{
	/*
	
		BL : Pin 15
		CS : Pin 14
		RES: Pin 13
	*/
	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);
	LL_GPIO_ResetOutputPin(GPIOB, LL_GPIO_PIN_15 | LL_GPIO_PIN_14 | LL_GPIO_PIN_13 | LL_GPIO_PIN_1);
	
	LL_GPIO_InitTypeDef spiPort;
	LL_GPIO_StructInit(&spiPort);
	
	spiPort.Mode 	= LL_GPIO_MODE_OUTPUT;
	spiPort.Speed	= LL_GPIO_SPEED_FREQ_HIGH;
	spiPort.Pin			= LL_GPIO_PIN_15 | LL_GPIO_PIN_14 | LL_GPIO_PIN_13 | LL_GPIO_PIN_1; 
	spiPort.OutputType  = LL_GPIO_OUTPUT_PUSHPULL;
	
	LL_GPIO_Init(GPIOB, &spiPort);
}

void spiSend(uint8_t *data, uint8_t len)
{
	uint8_t volatile *spidr = ((__IO uint8_t *)&SPI1->DR);

	while (len > 0)
	{
		//while (!(SPI1->SR&SPI_SR_TXE)) 
		//;
		len--;
		//*spidr = (uint8_t)*data++;
		LL_SPI_TransmitData8(SPI1, *data++);
	    while ((SPI1->SR&SPI_SR_BSY)) ;
	}	
//	while (!(SPI1->SR&SPI_SR_TXE)) ;


		
}
void spiSend16(uint16_t *data, uint32_t len)
{	
	uint16_t volatile *spidr = ((__IO uint16_t *)&SPI1->DR);

	while (len > 0)
	{
		
		len--;
	//	*spidr = *data++;
	//	while (!(SPI1->SR&SPI_SR_TXE)) ;
		LL_SPI_TransmitData16(SPI1,  (uint16_t)*data++);
		while ((SPI1->SR&SPI_SR_BSY)) ;
	}	
		
}


//testing purpose
void drawr_img(void)
{
	ST7735_DrawImage(0, 0, ST7735_WIDTH, ST7735_HEIGHT, (uint16_t*)test_img_128x160);
	//   HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
	
	    // Display test image 128x128 pixel by pixel
	    for(int x = 0 ; x < ST7735_WIDTH ; x++) 
	{
		for (int y = 0; y < ST7735_HEIGHT; y++) {
			uint16_t color565 = test_img_128x160[y][x];
			// fix endiness
			color565 = ((color565 & 0xFF00) >> 8) | ((color565 & 0xFF) << 8);
			ST7735_DrawPixel(x, y, color565);
		}
	}
	
	//  HAL_Delay(15000);
	  //HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
}
void drawr_img2(void)
{
	ST7735_DrawImage(0, 0, ST7735_WIDTH, ST7735_HEIGHT, (uint16_t*)test_img2_128x160);
	//   HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
	
	    // Display test image 128x128 pixel by pixel
	    for(int x = 0 ; x < ST7735_WIDTH ; x++) 
	{
		for (int y = 0; y < ST7735_HEIGHT; y++) {
			uint16_t color565 = test_img2_128x160[y][x];
			// fix endiness
			color565 = ((color565 & 0xFF00) >> 8) | ((color565 & 0xFF) << 8);
			ST7735_DrawPixel(x, y, color565);
		}
	}
	
	//  HAL_Delay(15000);
	  //HAL_GPIO_TogglePin(GPIOC,	LED0_Pin);
}


void setCurrent(float  i)
{
/*
Here I have to calculate the voltage that I need across the R_SHUNT
in order to get the desired currnet. Since V= I*R , 
I solve algebraically for V. 
Once I have V then I have to figure out How many DAC "ticks"
or rathewr what DAC value will give me that voltage.
I do this by multiplying by the ADC_OFFSET value which is explained at the top of this file

*/

	float temp = ( (R_SHUNT * i) * ADC_OFFSET ) + ERROR;
//	temp = temp / (0.000808081) + 5;
	
	dac_setpoint = (uint32_t) temp; 
}
void setPower(float p)
{
  // p = v^2 / R
	float temp = sqrt(p * R_SHUNT); // no i have a voltage
	temp = temp / 0.000800; 
    dac_setpoint = (uint32_t) temp; 
}
void cmd_setPoint_handler(uint8_t num, char *values[])
{
	adc_setpoint = atoi(values[0]);
}
void cmd_getAdc_handler(uint8_t num, char *values[])
{
	LL_ADC_REG_StartConversionSWStart(ADC1);
	CL_printMsg("ADC value: %d\r\n", adc_val[0]);
}

void cmd_setDac_handler(uint8_t num, char *values[])
{
	uint32_t val = atoi(values[0]);
	dac_setpoint = val;
	LL_DAC_ConvertData12RightAligned(DAC, LL_DAC_CHANNEL_1, (val & 0xFFF));
}
void cmd_setCurrent_handler(uint8_t num, char *values[])
{
	uint32_t integer = atoi(values[0]);
	uint32_t fract = atoi(values[1]);
	
	float temp, tempfract;
	if (fract >= 10)
		tempfract = fract * 0.01;
	if (fract < 10)
		tempfract = fract * 0.1;
	temp = integer + (tempfract);

	setCurrent(temp);
	LL_DAC_ConvertData12RightAligned(DAC, LL_DAC_CHANNEL_1, (dac_setpoint & 0xFFF));
}
void cmd_getDac_handler(uint8_t num, char *values[])
{
	CL_printMsg("Dac value: %d\r\n", dac_setpoint);
}
void cmd_ok_handler(uint8_t num, char *values[])
{
	CL_printMsg("System ok! \r\n");
}
