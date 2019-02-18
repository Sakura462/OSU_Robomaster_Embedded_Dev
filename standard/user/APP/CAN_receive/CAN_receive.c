/**
  ****************************(C) COPYRIGHT 2016 DJI****************************
  * @file       can_receive.c/h
  * @brief      ���can�豸�����շ����������ļ���ͨ��can�ж���ɽ���
  * @note       ���ļ�����freeRTOS����
  * @history
  *  Version    Date            Author          Modification
  *  V1.0.0     Dec-26-2018     RM              1. ���
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

#include "Detect_Task.h"

//Read Chassis Motor data
//���̵�����ݶ�ȡ
#define get_motor_measure(ptr, rx_message)                                                 \
{                                                                                          \
		(ptr)->last_ecd = (ptr)->ecd;                                                          \
		(ptr)->ecd = (uint16_t)((rx_message)->Data[0] << 8 | (rx_message)->Data[1]);           \
		(ptr)->speed_rpm = (uint16_t)((rx_message)->Data[2] << 8 | (rx_message)->Data[3]);     \
		(ptr)->given_current = (uint16_t)((rx_message)->Data[4] << 8 | (rx_message)->Data[5]); \
		(ptr)->temperate = (rx_message)->Data[6];                                              \
}

//Read Gimbal Motor data
//��̨������ݶ�ȡ
#define get_gimbal_motor_measure(ptr, rx_message)                                          \
{                                                                                          \
		(ptr)->last_ecd = (ptr)->ecd;                                                          \
		(ptr)->ecd = (uint16_t)((rx_message)->Data[0] << 8 | (rx_message)->Data[1]);           \
		(ptr)->given_current = (uint16_t)((rx_message)->Data[2] << 8 | (rx_message)->Data[3]); \
		(ptr)->speed_rpm = (uint16_t)((rx_message)->Data[4] << 8 | (rx_message)->Data[5]);     \
		(ptr)->temperate = (rx_message)->Data[6];                                              \
}

//TX2 Data Receive
//TX2ͨ�����ݶ�ȡ
#define get_tx2_measure(ptr,rx_message)                                                        												\
{																																																											\
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
//ͳһ����CAN���պ���
static void CAN_hook(CanRxMsg *rx_message);
		
//Declare Motor variables
//�����������
static motor_measure_t motor_yaw, motor_pit, motor_trigger, motor_chassis[4];
    
//Declare TX2 variables struct
//����TX2�����ṹ��
tx2_measure_t tx2;
    
static CanTxMsg GIMBAL_TxMessage;    
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
static uint8_t delay_time = 100;
#endif
//CAN1 Interrupt
//CAN1�ж�
void CAN1_RX0_IRQHandler(void)
{
    static CanRxMsg rx1_message;

    if (CAN_GetITStatus(CAN1, CAN_IT_FMP0) != RESET)
    {
        CAN_ClearITPendingBit(CAN1, CAN_IT_FMP0);
        CAN_Receive(CAN1, CAN_FIFO0, &rx1_message);
        CAN_hook(&rx1_message);
    }
}

//CAN2 Interrupt
//CAN2�ж�
void CAN2_RX0_IRQHandler(void)
{
    static CanRxMsg rx2_message;
    if (CAN_GetITStatus(CAN2, CAN_IT_FMP0) != RESET)
    {
        CAN_ClearITPendingBit(CAN2, CAN_IT_FMP0);
        CAN_Receive(CAN2, CAN_FIFO0, &rx2_message);
        CAN_hook(&rx2_message);
    }
}

#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
void GIMBAL_lose_slove(void)
{
        delay_time = RNG_get_random_range(13,239);
}
#endif
//Transmit Gimbal Control command, "rev" is reserved data
//������̨�����������revΪ�����ֽ�
void CAN_CMD_GIMBAL(int16_t yaw, int16_t pitch, int16_t shoot, int16_t rev)
{
    GIMBAL_TxMessage.StdId = CAN_GIMBAL_ALL_ID;
    GIMBAL_TxMessage.IDE = CAN_ID_STD;
    GIMBAL_TxMessage.RTR = CAN_RTR_DATA;
    GIMBAL_TxMessage.DLC = 0x08;
    GIMBAL_TxMessage.Data[0] = (yaw >> 8);
    GIMBAL_TxMessage.Data[1] = yaw;
    GIMBAL_TxMessage.Data[2] = (pitch >> 8);
    GIMBAL_TxMessage.Data[3] = pitch;
    GIMBAL_TxMessage.Data[4] = (shoot >> 8);
    GIMBAL_TxMessage.Data[5] = shoot;
    GIMBAL_TxMessage.Data[6] = (rev >> 8);
    GIMBAL_TxMessage.Data[7] = rev;

#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE

    TIM6->CNT = 0;
    TIM6->ARR = delay_time ;

    TIM_Cmd(TIM6,ENABLE);
#else
    CAN_Transmit( GIMBAL_CAN,  &GIMBAL_TxMessage );
#endif

}

void TIM6_DAC_IRQHandler(void)
{
    if( TIM_GetITStatus( TIM6, TIM_IT_Update )!= RESET )
    {

        TIM_ClearFlag( TIM6, TIM_IT_Update );
#if GIMBAL_MOTOR_6020_CAN_LOSE_SLOVE
        CAN_Transmit( GIMBAL_CAN,  &GIMBAL_TxMessage );
#endif
        TIM_Cmd(TIM6,DISABLE);
    }
}
//CAN transmits the data of 0x700's ID��trigger M3508 Gear Motor into  Quick ID Setting Mode
//CAN ���� 0x700��ID�����ݣ�������M3508�����������IDģʽ
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
//���͵��̵����������
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
//�������������ݵ�TX2
void CAN_CMD_TX2(int16_t yaw, int16_t pitch){//-32767-32768
  
  yaw+=32767; //0-32767 <- -32767-32768
  pitch+=32767;//0-32767 <- -32767-32768
  
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

//Return Yaw Address of motor��retrieve original data through Pointer
//����yaw������ˮƽ�ᣩ���������ַ��ͨ��ָ�뷽ʽ��ȡԭʼ����
const motor_measure_t *get_Yaw_Gimbal_Motor_Measure_Point(void)
{
    return &motor_yaw;
}
//Return Pitch Address of motor��retrieve original data through Pointer
//����pitch�����´�ֱ�ᣩ���������ַ��ͨ��ָ�뷽ʽ��ȡԭʼ����
const motor_measure_t *get_Pitch_Gimbal_Motor_Measure_Point(void)
{
    return &motor_pit;
}
//Return Trigger Address of motor��retrieve original data through Pointer
//����trigger���������ַ��ͨ��ָ�뷽ʽ��ȡԭʼ����
const motor_measure_t *get_Trigger_Motor_Measure_Point(void)
{
    return &motor_trigger;
}
//Return Chassis Address of motor��retrieve original data through Pointer
//���ص��̵��������ַ��ͨ��ָ�뷽ʽ��ȡԭʼ����
const motor_measure_t *get_Chassis_Motor_Measure_Point(uint8_t i)
{
    return &motor_chassis[(i & 0x03)];
}

//Process CAN Interrupt funtion together��record the time of sending data as reference of offline
//ͳһ����CAN�жϺ��������Ҽ�¼�������ݵ�ʱ�䣬��Ϊ�����ж�����
static void CAN_hook(CanRxMsg *rx_message)
{
    switch (rx_message->StdId)
    {
    case CAN_YAW_MOTOR_ID:
    {
        //Get Yaw Gimbal Motor Measure
				//����yaw������ݺ꺯��
        get_gimbal_motor_measure(&motor_yaw, rx_message);
        //Record time
				//��¼ʱ��
        DetectHook(YawGimbalMotorTOE);
        break;
    }
    case CAN_PIT_MOTOR_ID:
    {
        //Get Pitch Gimbal Motor Measure
				//����pitch������ݺ꺯��
        get_gimbal_motor_measure(&motor_pit, rx_message);
        DetectHook(PitchGimbalMotorTOE);
        break;
    }
    case CAN_TRIGGER_MOTOR_ID:
    {
        //Get Trigger Gimbal Motor Measure
				//���������ݺ꺯��
       get_motor_measure(&motor_trigger, rx_message);
				//Record time
				//��¼ʱ��
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
        //������ID��
        i = rx_message->StdId - CAN_3508_M1_ID;
				//Get Motor #i Measure
        //�����Ӧ������ݺ꺯��
        get_motor_measure(&motor_chassis[i], rx_message);
				//Record time
        //��¼ʱ��
        DetectHook(ChassisMotor1TOE + i);
        break;
    }
    
    case CAN_TX2_ID:
    {
        get_tx2_measure(&tx2,rx_message);
    }

    default:
    {
        break;
    }
    }
}
