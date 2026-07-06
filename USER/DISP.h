#ifndef __DISP_H
#define __DISP_H	
#include "sys.h" 
//////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK STM32F407开发板
//ADC 驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2014/5/6
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2014-2024
//All rights reserved									  
////////////////////////////////////////////////////////////////////////////////// 	 
 							   
void clear_point(u16 num);//更新显示屏当前列	
void Set_BackGround(void);//设置背景
void Lcd_DrawNetwork(void);//画网格
float get_vpp(u16 *buf);//获取峰峰值
void DrawOscillogram(u16* buf);//画波形图	
#endif 
