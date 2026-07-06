#include "dac.h"
#include "sys.h"	 
#include <math.h>
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//DAC 驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2014/5/6
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved 
////////////////////////////////////////////////////////////////////////////////// 	
 #define RELOAD_VAL(hz) (u16)(84000000UL /256/hz) //计算重装值
 #define RELOAD_VAL1(hz)(u16)(84000000UL /256/hz1) //计算重装值
// #define RELOAD_VAL2(hz)(u16)(84000000UL /256/hz2) //计算重装值
u16 Sine12bit[256] = { //正弦波描点
 2048, 2098, 2148, 2198, 2248, 2298, 2348, 2398, 2447, 2496,
 2545, 2594, 2642, 2690, 2737, 2785, 2831, 2877, 2923, 2968,
 3013, 3057, 3100, 3143, 3185, 3227, 3267, 3307, 3347, 3385,
 3423, 3460, 3496, 3531, 3565, 3598, 3631, 3662, 3692, 3722,
 3750, 3778, 3804, 3829, 3854, 3877, 3899, 3920, 3940, 3958,
 3976, 3992, 4007, 4021, 4034, 4046, 4056, 4065, 4073, 4080,
 4086, 4090, 4093, 4095, 4095, 4095, 4093, 4090, 4086, 4080,
 4073, 4065, 4056, 4046, 4034, 4021, 4007, 3992, 3976, 3958,
 3940, 3920, 3899, 3877, 3854, 3829, 3804, 3778, 3750, 3722,
 3692, 3662, 3631, 3598, 3565, 3531, 3496, 3460, 3423, 3385,
 3347, 3307, 3267, 3227, 3185, 3143, 3100, 3057, 3013, 2968,
 2923, 2877, 2831, 2785, 2737, 2690, 2642, 2594, 2545, 2496,
 2447, 2398, 2348, 2298, 2248, 2198, 2148, 2098, 2047, 1997,
 1947, 1897, 1847, 1797, 1747, 1697, 1648, 1599, 1550, 1501,
 1453, 1405, 1358, 1310, 1264, 1218, 1172, 1127, 1082, 1038,
 995, 952, 910, 868, 828, 788, 748, 710, 672, 635,
 599, 564, 530, 497, 464, 433, 403, 373, 345, 317,
 291, 266, 241, 218, 196, 175, 155, 137, 119, 103,
 88, 74, 61, 49, 39, 30, 22, 15, 9, 5,
 2, 0, 0, 0, 2, 5, 9, 15, 22, 30,
 39, 49, 61, 74, 88, 103, 119, 137, 155, 175,
 196, 218, 241, 266, 291, 317, 345, 373, 403, 433,
 464, 497, 530, 564, 599, 635, 672, 710, 748, 788,
 828, 868, 910, 952, 995, 1038, 1082, 1127, 1172, 1218,
 1264, 1310, 1358, 1405, 1453, 1501, 1550, 1599, 1648, 1697,
 1747, 1797, 1847, 1897, 1947, 1997 };
u16 Sine12bit1[256] = { //正弦波描点
 2048, 2098, 2148, 2198, 2248, 2298, 2348, 2398, 2447, 2496,
 2545, 2594, 2642, 2690, 2737, 2785, 2831, 2877, 2923, 2968,
 3013, 3057, 3100, 3143, 3185, 3227, 3267, 3307, 3347, 3385,
 3423, 3460, 3496, 3531, 3565, 3598, 3631, 3662, 3692, 3722,
 3750, 3778, 3804, 3829, 3854, 3877, 3899, 3920, 3940, 3958,
 3976, 3992, 4007, 4021, 4034, 4046, 4056, 4065, 4073, 4080,
 4086, 4090, 4093, 4095, 4095, 4095, 4093, 4090, 4086, 4080,
 4073, 4065, 4056, 4046, 4034, 4021, 4007, 3992, 3976, 3958,
 3940, 3920, 3899, 3877, 3854, 3829, 3804, 3778, 3750, 3722,
 3692, 3662, 3631, 3598, 3565, 3531, 3496, 3460, 3423, 3385,
 3347, 3307, 3267, 3227, 3185, 3143, 3100, 3057, 3013, 2968,
 2923, 2877, 2831, 2785, 2737, 2690, 2642, 2594, 2545, 2496,
 2447, 2398, 2348, 2298, 2248, 2198, 2148, 2098, 2047, 1997,
 1947, 1897, 1847, 1797, 1747, 1697, 1648, 1599, 1550, 1501,
 1453, 1405, 1358, 1310, 1264, 1218, 1172, 1127, 1082, 1038,
 995, 952, 910, 868, 828, 788, 748, 710, 672, 635,
 599, 564, 530, 497, 464, 433, 403, 373, 345, 317,
 291, 266, 241, 218, 196, 175, 155, 137, 119, 103,
 88, 74, 61, 49, 39, 30, 22, 15, 9, 5,
 2, 0, 0, 0, 2, 5, 9, 15, 22, 30,
 39, 49, 61, 74, 88, 103, 119, 137, 155, 175,
 196, 218, 241, 266, 291, 317, 345, 373, 403, 433,
 464, 497, 530, 564, 599, 635, 672, 710, 748, 788,
 828, 868, 910, 952, 995, 1038, 1082, 1127, 1172, 1218,
 1264, 1310, 1358, 1405, 1453, 1501, 1550, 1599, 1648, 1697,
 1747, 1797, 1847, 1897, 1947, 1997 };
////uint32_t DualSine12bit[32];
void DAC_Config(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;
	DAC_InitTypeDef  DAC_InitStructure;

  /* 使能GPIOA时钟 */
  RCC_AHB1PeriphClockCmd(DAC_CH1_GPIO_CLK|DAC_CH2_GPIO_CLK, ENABLE);	
	
	/* 使能DAC时钟 */	
  RCC_APB1PeriphClockCmd(DAC_CLK, ENABLE);
  
  /* DAC的GPIO配置，模拟输入 */
  GPIO_InitStructure.GPIO_Pin =  DAC_CH1_GPIO_PIN;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
  GPIO_Init(DAC_CH1_GPIO_PORT, &GPIO_InitStructure);
	
		  /* 配置DAC 通道1 */
  DAC_InitStructure.DAC_Trigger = DAC_TRIGGER;						//使用TIM2作为触发源
  DAC_InitStructure.DAC_WaveGeneration = DAC_WaveGeneration_None;	//不使用波形发生器
  DAC_InitStructure.DAC_OutputBuffer = DAC_OutputBuffer_Disable;	//不使用DAC输出缓冲
	//三角波振幅（本实验没有用到，可配置成任意值，但本结构体成员不能为空）
	DAC_InitStructure.DAC_LFSRUnmask_TriangleAmplitude = DAC_LFSRUnmask_Bit0;
  DAC_Init(DAC_CH1_CHANNEL, &DAC_InitStructure);
	
  DAC_InitStructure.DAC_Trigger = DAC_TRIGGER1;						//使用TIM2作为触发源
  /* 配置DAC 通道2 */
  /* 配置DAC 通道1、2 */
  DAC_Cmd(DAC_Channel_1, ENABLE);
	/* 使能 DAC的DMA请求 */
  DAC_DMACmd(DAC_Channel_1, ENABLE);
}


/**
  * @brief  配置TIM
  * @param  无
  * @retval 无
  */
 void DAC_TIM_Config(u32 hz,u32 hz1)
{
	
	TIM_TimeBaseInitTypeDef    TIM_TimeBaseStructure;
	
	/* 使能TIM2时钟，TIM2CLK 为84M */
  RCC_APB1PeriphClockCmd(DAC_TIM_CLK|DAC_TIM_CLK1, ENABLE);
	
  /* TIM2基本定时器配置 */
 // TIM_TimeBaseStructInit(&TIM_TimeBaseStructure); 
  TIM_TimeBaseStructure.TIM_Period = RELOAD_VAL(hz);       									//定时周期 20  
  TIM_TimeBaseStructure.TIM_Prescaler = 0x0;       							//预分频，不分频 84M / (0+1) = 84M
  TIM_TimeBaseStructure.TIM_ClockDivision = 0x0;    						//时钟分频系数
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;  	//向上计数模式
  TIM_TimeBaseInit(DAC_TIM, &TIM_TimeBaseStructure);
	
  TIM_TimeBaseStructure.TIM_Period = RELOAD_VAL1(hz);       									//定时周期 20  
	TIM_TimeBaseInit(DAC_TIM1, &TIM_TimeBaseStructure);
  /* 配置TIM2触发源 */
  TIM_SelectOutputTrigger(DAC_TIM, TIM_TRGOSource_Update);
  TIM_SelectOutputTrigger(DAC_TIM1, TIM_TRGOSource_Update);
	/* 使能TIM2 */
  TIM_Cmd(DAC_TIM, ENABLE);
  TIM_Cmd(DAC_TIM1, ENABLE);
}

/**
  * @brief  配置DMA
  * @param  无
  * @retval 无
  */
 void DAC_DMA_Config(void)
{	
	DMA_InitTypeDef  DMA_InitStructure;

	/* DAC1使用DMA1 通道7 数据流5时钟 */
	RCC_AHB1PeriphClockCmd(DAC_DMA_CLK, ENABLE);
	
	/* 配置DMA2 */
  DMA_InitStructure.DMA_Channel = DAC_CHANNEL;  
  DMA_InitStructure.DMA_PeripheralBaseAddr = DAC_DHR1RD_Address;					//外设数据地址
  DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)&Sine12bit;			//内存数据地址 DualSine12bit
  DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;									//数据传输方向内存至外设
  DMA_InitStructure.DMA_BufferSize = sizeof(Sine12bit);																	//缓存大小为32字节	
  DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;				//外设数据地址固定	
  DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;									//内存数据地址自增
  DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;	//外设数据以字为单位
  DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;					//内存数据以字为单位	
  DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;													//循环模式
  DMA_InitStructure.DMA_Priority = DMA_Priority_High;											//高DMA通道优先级
  DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;         
  DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
  DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
  DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

  DMA_Init(DAC_DMA_STREAM, &DMA_InitStructure);
	
  DMA_InitStructure.DMA_PeripheralBaseAddr = DAC_DHR2RD_Address;					//外设数据地址
	DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)&Sine12bit1;			//内存数据地址 DualSine12bit
	
	DMA_Init(DAC_DMA_STREAM1, &DMA_InitStructure);
  /* 使能 DMA_Stream */
  DMA_Cmd(DAC_DMA_STREAM, ENABLE); 
	DMA_Cmd(DAC_DMA_STREAM1, ENABLE); 

}
/*************************************************************
Function  :  set_Sine12bit
Description : 设置正玄波码表
Input  :   MAX(正峰峰值) MIN(负峰峰值)
return  :  none
*************************************************************/ 
 void Set_Sine12bit(float MAX,float MIN)
{
   int i;
   float  jiaodu=0;
   float MID=(MAX+MIN)/2.0f;                        //中间峰值
   if(MAX>3.3f) MAX=3.3f;
   else if(MAX<=MIN) MIN=0;
   for(i=0;i<256;i++)
         {
             jiaodu=i*0.0247369f;      //当i =127时,表示为180度,由于sin()是弧度制,所以需要转换
             Sine12bit[i]=  ((float)sin(jiaodu)*(MAX-MID)+MID)*1241.212f;     //1241.212是比例,等于4096/3.3  
             Sine12bit1[i]=  ((float)sin(jiaodu)*(MAX-MID)+MID)*1241.212f;     //1241.212是比例,等于4096/3.3  					 
         }  
}

////void  Set_Period(u32 value)
////{
////     TIM_ARRPreloadConfig(TIM2,DISABLE);
////     TIM2->ARR = RELOAD_VAL(value);       //更新预装载值 
////     TIM_ARRPreloadConfig(TIM2,ENABLE);
////}





/**
  * @brief  DAC初始化函数
  * @param  无
  * @retval 无
  */
void DAC_Mode_Init(void)
{
//	uint32_t Idx = 0;  

	DAC_Config();
	DAC_TIM_Config(1000,1000);
	Set_Sine12bit(0,3);
	/* 填充正弦波形数据，双通道右对齐*/
//  for (Idx = 0; Idx < 32; Idx++)
//  {
//    DualSine12bit[Idx] = (Sine12bit1[Idx] << 16) + (Sine12bit[Idx]);
//  }
	
	DAC_DMA_Config();
}









