/**
  ****************************(C) COPYRIGHT 2016 DJI****************************
  * @file       can_receive.c/h
  * @brief      完成can设备数据收发函数，该文件是通过can中断完成接收
  * @note       该文件不是freeRTOS任务
  * @history
  *  Version    Date            Author          Modification
  *  V1.0.0     Dec-26-2018     RM              1. 完成
  *  V1.0.1     Feb-17-2018     Tony-OSU        Add tx2 can bus config
  @verbatim
  ==============================================================================

  ==============================================================================
  @endverbatim
  ****************************(C) COPYRIGHT 2016 DJI****************************
  */

#include "CAN_Receive.h"

#include "stm32f4xx.h"
#include "rng.h"

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"
#include "buzzer.h"
#include "Detect_Task.h"

//Read Chassis Motor data
//底盘电机数据读取
//"ecd" represents "encoder"
//Left shift first 8-bit message by 8 bits, then add(by Bitwise Or) second 8-bit message together to generate an entire 16-bit message   
#define get_motor_measure(ptr, rx_message)                                                 \
{                                                                                          \
		(ptr)->last_ecd = (ptr)->ecd;                                                          \
		(ptr)->ecd = (uint16_t)((rx_message)->Data[0] << 8 | (rx_message)->Data[1]);           \
		(ptr)->speed_rpm = (uint16_t)((rx_message)->Data[2] << 8 | (rx_message)->Data[3]);     \
		(ptr)->given_current = (uint16_t)((rx_message)->Data[4] << 8 | (rx_message)->Data[5]); \
		(ptr)->temperate = (rx_message)->Data[6];                                              \
}

//Read Gimbal Motor data
//云台电机数据读取
#define get_gimbal_motor_measure(ptr, rx_message)                                          \
{                                                                                          \
		(ptr)->last_ecd = (ptr)->ecd;                                                          \
		(ptr)->ecd = (uint16_t)((rx_message)->Data[0] << 8 | (rx_message)->Data[1]);           \
		(ptr)->given_current = (uint16_t)((rx_message)->Data[2] << 8 | (rx_message)->Data[3]); \
		(ptr)->speed_rpm = (uint16_t)((rx_message)->Data[4] << 8 | (rx_message)->Data[5]);     \
		(ptr)->temperate = (rx_message)->Data[6];                                              \
}

//TX2 Data Receive
//TX2数据读取
//Data[0] is used to determine the type of package, pitch_package & yaw_package are used to adjust PID, aim_package is used to transmit the pixel coordinates
#define get_tx2_measure(ptr,rx_message)                                                        												\
{																																																											\
	   if(rx_message->Data[0]==0xff) buzzer_on(150,10000);                                                              \
		(ptr)->package_type=(uint16_t)(rx_message->Data[0]);																	 														\
		tx2_package_type_e tx2_package_type;                                                   														\
		switch(tx2_package_type){                                                              														\
			case pitch_package:       																													 														\
				(ptr)->pitch_pid_package.error=0;																									 														\
				(ptr)->pitch_pid_package.err_last=(ptr)->pitch_pid_package.error;                  														\
				(ptr)->pitch_pid_package.kp=(uint8_t)(rx_message->Data[1]);																										\
				(ptr)->pitch_pid_package.ki=(uint8_t)(rx_message->Data[2]);                                   								\
				(ptr)->pitch_pid_package.kd=(uint8_t)(rx_message->Data[3]);                                   								\
				(ptr)->pitch_pid_package.error=(uint16_t)((rx_message->Data[4]<<8)|(rx_message)->Data[5]);   									\
				(ptr)->pitch_pid_package.power=(uint16_t)((rx_message->Data[6]<<8)|(rx_message)->Data[7]);	 									\
				break;																																						 														\
			case yaw_package:																																		 														\
				(ptr)->yaw_pid_package.error=0;																									 															\
				(ptr)->yaw_pid_package.err_last=(ptr)->pitch_pid_package.error;                  															\
				(ptr)->yaw_pid_package.kp=(uint8_t)(rx_message->Data[1]);																					 						\
				(ptr)->yaw_pid_package.ki=(uint8_t)(rx_message->Data[2]);                                          						\
				(ptr)->yaw_pid_package.kd=(uint8_t)(rx_message->Data[3]);                                          						\
				(ptr)->yaw_pid_package.error=(uint16_t)((rx_message->Data[4]<<8)|(rx_message)->Data[5]);   										\
				(ptr)->yaw_pid_package.power=(uint16_t)((rx_message->Data[6]<<8)|(rx_message)->Data[7]);	 										\
				break;																																						 														\
			case aim_package:																																		 														\
				(ptr)->aim_data_package.horizontal_pixel_buffer=(uint16_t)((rx_message->Data[1]<<8)|(rx_message)->Data[2]);		\
				(ptr)->aim_data_package.vertical_pixel_buffer=(uint16_t)((rx_message->Data[3]<<8)|(rx_message)->Data[4]);			\
				(ptr)->aim_data_package.horizontal_pixel-=32767;																	 														\
				(ptr)->aim_data_package.vertical_pixel-=32767;																		 														\
				break;																																						 														\
																																																											\
		}																																											 														\
} 
		
//Process CAN Receive funtion together
//统一处理CAN接收函数
static void CAN_hook(CanRxMsg *rx_message);
		
//Declare Motor variables
//声明电机变量
static motor_measure_t motor_yaw, motor_pit, motor_trigger, motor_chassis[4];
    
//Declare TX2 variables struct
//声明TX2变量结构体
tx2_measure_t tx2;

//Declare Gimbal Sending Message
//声明云台的发送信息
static CanTxMsg GIMBAL_TxMessage;

//If Gimbal Motor fails to send CAN message, initially define delay_time as 100 ms
//如果云台电机发送CAN失败，初始定义delay_time为100ms
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
static uint8_t delay_time = 100;
#endif

//CAN1 Interrupt
//CAN1中断
void CAN1_RX0_IRQHandler(void)
{
    static CanRxMsg rx1_message;

    if (CAN_GetITStatus(CAN1, CAN_IT_FMP0) != RESET)
    {
        CAN_ClearITPendingBit(CAN1, CAN_IT_FMP0);//Clear the CAN1 interrupt flag to avoid entering the interrupt immediately after exiting the interrupt
        CAN_Receive(CAN1, CAN_FIFO0, &rx1_message);
        CAN_hook(&rx1_message);//wait to be processed
    }
}

//CAN2 Interrupt
//CAN2中断
void CAN2_RX0_IRQHandler(void)
{
    static CanRxMsg rx2_message;
    if (CAN_GetITStatus(CAN2, CAN_IT_FMP0) != RESET)
    {
        CAN_ClearITPendingBit(CAN2, CAN_IT_FMP0);//Clear the CAN2 interrupt flag to avoid entering the interrupt immediately after exiting the interrupt
        CAN_Receive(CAN2, CAN_FIFO0, &rx2_message);
        CAN_hook(&rx2_message);//wait to be processed
    }
}

//If Gimbal Motor fails to send CAN message, try to solve by sending command in random delay time
//如果云台电机CAN发送失败，尝试使用 随机延迟 发送控制指令的方式解决
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
void GIMBAL_lose_slove(void)
{
        delay_time = RNG_get_random_range(13,239);
}
#endif
//Transmit Gimbal Control command, "rev" is reserved data
//发送云台控制命令，其中rev为保留字节
void CAN_CMD_GIMBAL(int16_t yaw, int16_t pitch, int16_t shoot, int16_t rev)
{
    GIMBAL_TxMessage.StdId = CAN_GIMBAL_ALL_ID;
    GIMBAL_TxMessage.IDE = CAN_ID_STD;//CAN_identifier_type=standard
    GIMBAL_TxMessage.RTR = CAN_RTR_DATA;
    GIMBAL_TxMessage.DLC = 0x08;//length of data
    GIMBAL_TxMessage.Data[0] = (yaw >> 8);
    GIMBAL_TxMessage.Data[1] = yaw;
    GIMBAL_TxMessage.Data[2] = (pitch >> 8);
    GIMBAL_TxMessage.Data[3] = pitch;
    GIMBAL_TxMessage.Data[4] = (shoot >> 8);
    GIMBAL_TxMessage.Data[5] = shoot;
    GIMBAL_TxMessage.Data[6] = (rev >> 8);
    GIMBAL_TxMessage.Data[7] = rev;
//If Gimbal Motor fails to send CAN message
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE

    TIM6->CNT = 0;//clear count of TIM6 
    TIM6->ARR = delay_time ;//set Auto-Reload Register as delay_time

    TIM_Cmd(TIM6,ENABLE);//Enable TIM6
#else
    CAN_Transmit( GIMBAL_CAN,  &GIMBAL_TxMessage );
#endif

}
//TIM6 Timer Interrupt
//TIM6定时器中断
void TIM6_DAC_IRQHandler(void)
{
    if( TIM_GetITStatus( TIM6, TIM_IT_Update )!= RESET )
    {

        TIM_ClearFlag( TIM6, TIM_IT_Update );
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
        CAN_Transmit( GIMBAL_CAN,  &GIMBAL_TxMessage );
#endif
        TIM_Cmd(TIM6,DISABLE);//Disable TIM6
    }
}
//CAN transmits the data of 0x700's ID，trigger M3508 Gear Motor into Quick ID Setting Mode
//CAN 发送 0x700的ID的数据，会引发M3508进入快速设置ID模式
void CAN_CMD_CHASSIS_RESET_ID(void)
{

    CanTxMsg TxMessage;
    TxMessage.StdId = 0x700;
    TxMessage.IDE = CAN_ID_STD;
    TxMessage.RTR = CAN_RTR_DATA;
    TxMessage.DLC = 0x08;
    TxMessage.Data[0] = 0;
    TxMessage.Data[1] = 0;
    TxMessage.Data[2] = 0;
    TxMessage.Data[3] = 0;
    TxMessage.Data[4] = 0;
    TxMessage.Data[5] = 0;
    TxMessage.Data[6] = 0;
    TxMessage.Data[7] = 0;

    CAN_Transmit(CAN2, &TxMessage);
}

//Transmit Chassis Control command
//发送底盘电机控制命令
void CAN_CMD_CHASSIS(int16_t motor1, int16_t motor2, int16_t motor3, int16_t motor4)
{
    CanTxMsg TxMessage;
    TxMessage.StdId = CAN_CHASSIS_ALL_ID;
    TxMessage.IDE = CAN_ID_STD;
    TxMessage.RTR = CAN_RTR_DATA;
    TxMessage.DLC = 0x08;
    TxMessage.Data[0] = motor1 >> 8;
    TxMessage.Data[1] = motor1;
    TxMessage.Data[2] = motor2 >> 8;
    TxMessage.Data[3] = motor2;
    TxMessage.Data[4] = motor3 >> 8;
    TxMessage.Data[5] = motor3;
    TxMessage.Data[6] = motor4 >> 8;
    TxMessage.Data[7] = motor4;

    CAN_Transmit(CHASSIS_CAN, &TxMessage);
}

//Send gyro data to TX2
//发送陀螺仪数据到TX2
void CAN_CMD_TX2(int16_t yaw, int16_t pitch){//-32767-32768
  
  //yaw+=32767; //0-32767 <- -32767-32768
  //pitch+=32767;//0-32767 <- -32767-32768
  
  CanTxMsg TxMessage;
  TxMessage.StdId=CAN_TX2_ID;
  TxMessage.IDE=CAN_ID_STD;
  TxMessage.RTR=CAN_RTR_DATA;
  TxMessage.DLC=0x08;
  TxMessage.Data[0]=yaw>>8;
  TxMessage.Data[1]=yaw;
  TxMessage.Data[2]=pitch>>8;
  TxMessage.Data[3]=pitch;
  TxMessage.Data[4] = 0;
  TxMessage.Data[5] = 0;
  TxMessage.Data[6] = 0;
  TxMessage.Data[7] = 0;
  
  CAN_Transmit(CAN2, &TxMessage);
}

//Return Yaw Address of motor，retrieve original data through Pointer
//返回yaw（左右水平轴）电机变量地址，通过指针方式获取原始数据
const motor_measure_t *get_Yaw_Gimbal_Motor_Measure_Point(void)
{
    return &motor_yaw;
}
//Return Pitch Address of motor，retrieve original data through Pointer
//返回pitch（上下垂直轴）电机变量地址，通过指针方式获取原始数据
const motor_measure_t *get_Pitch_Gimbal_Motor_Measure_Point(void)
{
    return &motor_pit;
}
//Return Trigger Address of motor，retrieve original data through Pointer
//返回trigger电机变量地址，通过指针方式获取原始数据
const motor_measure_t *get_Trigger_Motor_Measure_Point(void)
{
    return &motor_trigger;
}
//Return Chassis Address of motor，retrieve original data through Pointer
//返回底盘电机变量地址，通过指针方式获取原始数据
const motor_measure_t *get_Chassis_Motor_Measure_Point(uint8_t i)
{
    return &motor_chassis[(i & 0x03)];
}

//Process CAN Interrupt funtion together，record the time of sending data as reference of offline
//统一处理CAN中断函数，并且记录发送数据的时间，作为离线判断依据
static void CAN_hook(CanRxMsg *rx_message)
{
    switch (rx_message->StdId)
    {
    case CAN_YAW_MOTOR_ID:
    {
        //Process Yaw Gimbal Motor Function
				//处理yaw电机数据宏函数
        get_gimbal_motor_measure(&motor_yaw, rx_message);
        //Record time
				//记录时间
        DetectHook(YawGimbalMotorTOE);
        break;
    }
    case CAN_PIT_MOTOR_ID:
    {
        //Process Pitch Gimbal Motor Function
				//处理pitch电机数据宏函数
        get_gimbal_motor_measure(&motor_pit, rx_message);
        DetectHook(PitchGimbalMotorTOE);
        break;
    }
    case CAN_TRIGGER_MOTOR_ID:
    {
        //Process Trigger Motor Function
				//处理电机数据宏函数
				get_motor_measure(&motor_trigger, rx_message);
				//Record time
				//记录时间
        DetectHook(TriggerMotorTOE);
        break;
    }
    case CAN_3508_M1_ID:
    case CAN_3508_M2_ID:
    case CAN_3508_M3_ID:
    case CAN_3508_M4_ID:
    {
        static uint8_t i = 0;
				//Get Motor ID
        //处理电机ID号
        i = rx_message->StdId - CAN_3508_M1_ID;
				//Process Motor #i Measure
        //处理对应电机数据宏函数
        get_motor_measure(&motor_chassis[i], rx_message);
				//Record time
        //记录时间
        DetectHook(ChassisMotor1TOE + i);
        break;
    }
    
    case CAN_TX2_ID:
    {
				//处理TX2数据
				//Process TX2 data
			  //buzzer_on(150,10000);
        get_tx2_measure(&tx2,rx_message);
			  			
			  //CAN_CMD_TX2(100,100);
        break;
    }

    default:
    {
        break;
    }
    }
}
