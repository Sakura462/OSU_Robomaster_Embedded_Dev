/**
  ****************************(C) COPYRIGHT 2016 DJI****************************
  * @file       can_receive.c/h
  * @brief      can device transmit and recevice function��receive via CAN interrupt
  * @note       This is NOT a freeRTOS TASK
  * @history
  *  Version    Date            Author          Modification
  *  V1.0.0     Dec-26-2018     RM              1. Compete
  *  V1.0.1     Feb-17-2019     Tony-OSU        Add tx2 can bus config
	*  V1.1.0     Feb-21-2019     Tony-OSU        Finish Custom CAN Bus, fully functional
	*  V1.2.0     Mar-01-2019     Tony-OSU        CAN unpackaging simplified. Pixel bias changed.
	* 																						@note some packages ID has CHANGED!! See .h file for detail
  @verbatim
  ==============================================================================

  ==============================================================================
  @endverbatim
  ****************************(C) COPYRIGHT 2016 DJI****************************
	**************Modifid by Ohio State University Robomaster Team****************
  */

#include "CAN_Receive.h"

#include "stm32f4xx.h"
#include "rng.h"

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"
#include "buzzer.h"
#include "Detect_Task.h"
#include "pid.h"

//Read Chassis Motor data
//���̵�����ݶ�ȡ
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
//��̨������ݶ�ȡ
#define get_gimbal_motor_measure(ptr, rx_message)                                          \
{                                                                                          \
		(ptr)->last_ecd = (ptr)->ecd;                                                          \
		(ptr)->ecd = (uint16_t)((rx_message)->Data[0] << 8 | (rx_message)->Data[1]);           \
		(ptr)->given_current = (uint16_t)((rx_message)->Data[2] << 8 | (rx_message)->Data[3]); \
		(ptr)->speed_rpm = (uint16_t)((rx_message)->Data[4] << 8 | (rx_message)->Data[5]);     \
		(ptr)->temperate = (rx_message)->Data[6];                                              \
}

////TX2 Data Receive
////TX2���ݶ�ȡ
////Data[0] is used to determine the type of package, pitch_package & yaw_package are used to adjust PID, aim_package is used to transmit the pixel coordinates
//#define get_tx2_measure(ptr,rx_message)                                                        												\
//{																																																											\
//	   if(rx_message->Data[0]==0xff) buzzer_on(150,10000);                                                              \
//		(ptr)->package_type=(uint16_t)(rx_message->Data[0]);																	 														\
//		tx2_package_type_e tx2_package_type;                                                   														\
//		switch(tx2_package_type){                                                              														\
//			case pitch_package:       																													 														\
//				(ptr)->pitch_pid_package.error=0;																									 														\
//				(ptr)->pitch_pid_package.err_last=(ptr)->pitch_pid_package.error;                  														\
//				(ptr)->pitch_pid_package.kp=(uint8_t)(rx_message->Data[1]);																										\
//				(ptr)->pitch_pid_package.ki=(uint8_t)(rx_message->Data[2]);                                   								\
//				(ptr)->pitch_pid_package.kd=(uint8_t)(rx_message->Data[3]);                                   								\
//				(ptr)->pitch_pid_package.error=(uint16_t)((rx_message->Data[4]<<8)|(rx_message)->Data[5]);   									\
//				(ptr)->pitch_pid_package.power=(uint16_t)((rx_message->Data[6]<<8)|(rx_message)->Data[7]);	 									\
//				break;																																						 														\
//			case yaw_package:																																		 														\
//				(ptr)->yaw_pid_package.error=0;																									 															\
//				(ptr)->yaw_pid_package.err_last=(ptr)->pitch_pid_package.error;                  															\
//				(ptr)->yaw_pid_package.kp=(uint8_t)(rx_message->Data[1]);																					 						\
//				(ptr)->yaw_pid_package.ki=(uint8_t)(rx_message->Data[2]);                                          						\
//				(ptr)->yaw_pid_package.kd=(uint8_t)(rx_message->Data[3]);                                          						\
//				(ptr)->yaw_pid_package.error=(uint16_t)((rx_message->Data[4]<<8)|(rx_message)->Data[5]);   										\
//				(ptr)->yaw_pid_package.power=(uint16_t)((rx_message->Data[6]<<8)|(rx_message)->Data[7]);	 										\
//				break;																																						 														\
//			case aim_package:																																		 														\
//				(ptr)->aim_data_package.horizontal_pixel=(uint16_t)((rx_message->Data[1]<<8)|(rx_message)->Data[2]);		\
//				(ptr)->aim_data_package.vertical_pixel=(uint16_t)((rx_message->Data[3]<<8)|(rx_message)->Data[4]);			\
//				(ptr)->aim_data_package.horizontal_pixel-=640;																																	\
//				(ptr)->aim_data_package.vertical_pixel-=320;																																		\
//				break;																																						 														\
//																																																											\
//		}\
//}		\
////////////////////////////////		tx2�������ݰ�		////////////////////////////////
#define get_aim_data(ptr,rx_message)																																							\
{ 																																																							  \
	(ptr)->aim_data_package.horizontal_pixel=(uint16_t)((rx_message->Data[0]<<8)|(rx_message->Data[1]));						\
	if((ptr)->aim_data_package.horizontal_pixel>=1700.0f)																														\
		{																																																							\
			(ptr)->aim_data_package.horizontal_pixel=1700.0f;																														\
		}																																																							\
	if((ptr)->aim_data_package.horizontal_pixel<=100.0f)																														\
		{																																																							\
			(ptr)->aim_data_package.horizontal_pixel=100.0f;																														\
		}																																																							\
	(ptr)->aim_data_package.vertical_pixel=(uint16_t)((rx_message->Data[2]<<8)|((rx_message)->Data[3]));            \
	if((ptr)->aim_data_package.vertical_pixel>=400.0f)																															\
		{																																																							\
			(ptr)->aim_data_package.vertical_pixel=400.0f;																															\
		}																																																							\
	if((ptr)->aim_data_package.vertical_pixel<=100.0f)																															\
		{																																																							\
			(ptr)->aim_data_package.vertical_pixel=100.0f;																															\
		}																																																							\
}
////////////////////////////////		tx2�������ݰ�		////////////////////////////////

//��ʱ����
#define get_absolute_angle_data(ptr,rx_message)																																		\
{ 																																																							  \
	(ptr)->aim_data_package.horizontal_pixel=(uint16_t)((rx_message->Data[0]<<8)|(rx_message->Data[1]));						\
	(ptr)->aim_data_package.vertical_pixel=(uint16_t)((rx_message->Data[2]<<8)|((rx_message)->Data[3]));            \
}

//Process CAN Receive funtion together
//ͳһ����CAN���պ���
static void CAN_hook(CanRxMsg *rx_message);
		
//Declare Motor variables
//�����������
static motor_measure_t motor_yaw, motor_pit, motor_trigger, motor_chassis[4];
    
//Declare TX2 variables struct
//����TX2�����ṹ��
extern tx2_measure_t tx2;//externȫ�ֶ��壬ʹ�����ļ�Ҳ�ܵ���

//Declare Gimbal Sending Message
//������̨�ķ�����Ϣ
static CanTxMsg GIMBAL_TxMessage;

//If Gimbal Motor fails to send CAN message, initially define delay_time as 100 ms
//�����̨�������CANʧ�ܣ���ʼ����delay_timeΪ100ms
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
        CAN_ClearITPendingBit(CAN1, CAN_IT_FMP0);//Clear the CAN1 interrupt flag to avoid entering the interrupt immediately after exiting the interrupt
        CAN_Receive(CAN1, CAN_FIFO0, &rx1_message);
        CAN_hook(&rx1_message);//wait to be processed
    }
}

//CAN2 Interrupt
//CAN2�ж�
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
//�����̨���CAN����ʧ�ܣ�����ʹ�� ����ӳ� ���Ϳ���ָ��ķ�ʽ���
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
//TIM6��ʱ���ж�
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
//CAN transmits the data of 0x700's ID��trigger M3508 Gear Motor into Quick ID Setting Mode
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
	  //Transmit config
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

//������̨����������
void CAN_GIMBAL_TO_CAN2(uint8_t *data,int id){
	CanTxMsg TxMessage;
	TxMessage.StdId=id;
	TxMessage.IDE=CAN_ID_STD;
	TxMessage.RTR=CAN_RTR_DATA;
	TxMessage.DLC=0x08;
	
	for(int i=0;i<8;i++){
		TxMessage.Data[i]=data[i];
	}
	
	CAN_Transmit(CAN2,&TxMessage);
	
}

//Send PID Tuning Data
void CAN_CMD_PID_TUNING(uint8_t Device_ID, PidTypeDef *PID_struct){
	//Transmit config
	//The realization of Transmitting package is based on 
	//declaring a CanTxMsg struct with some necessary configuration
	CanTxMsg TxMessage;
	TxMessage.StdId=CAN_PID_TUNING_ID;
	TxMessage.IDE=CAN_ID_STD;
	TxMessage.RTR=CAN_RTR_DATA;
	TxMessage.DLC=0x04;
	//type casting to make data easy to transmit
	//These are returned feeback of easier PID tuning and dynamic tuning
	uint16_t output=(uint16_t)(PID_struct->out);
	uint16_t error=(uint16_t)(PID_struct->error[0]);
	
	TxMessage.Data[0]=output >> 8;
	TxMessage.Data[1]=output;
	TxMessage.Data[2]=error >> 8;
	TxMessage.Data[3]=error;
	
	CAN_Transmit(PID_TUNING_CAN, &TxMessage);
}

//Send gimbal gyro data to TX2
//������̨���������ݵ�TX2
void CAN_GIMBAL_GYRO_DATA_TX2(int16_t yaw, int16_t pitch){//-32767-32768
  
  CanTxMsg TxMessage;
  TxMessage.StdId=GYRO_DATA_TX2_ID;
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
  
  CAN_Transmit(TX2_CAN, &TxMessage);
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
        //Process Yaw Gimbal Motor Function
				//����yaw������ݺ꺯��
        get_gimbal_motor_measure(&motor_yaw, rx_message);
			  CAN_GIMBAL_TO_CAN2(rx_message->Data,CAN_GIMBAL_YAW_INTER_TRANSFER_ID);  //INTERCHANGE DATA TO CAN2
			
        //Record time
				//��¼ʱ��
        DetectHook(YawGimbalMotorTOE);
        break;
    }
    case CAN_PIT_MOTOR_ID:
    {
        //Process Pitch Gimbal Motor Function
				//����pitch������ݺ꺯��
        get_gimbal_motor_measure(&motor_pit, rx_message);
			  CAN_GIMBAL_TO_CAN2(rx_message->Data,CAN_GIMBAL_PITCH_INTER_TRANSFER_ID);  //INTERCHANGE DATA TO CAN2
				
				//Record time
				//��¼ʱ��
				DetectHook(PitchGimbalMotorTOE);
        break;
    }
    case CAN_TRIGGER_MOTOR_ID:
    {
        //Process Trigger Motor Function
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
				//Process Motor #i Measure
        //�����Ӧ������ݺ꺯��
        get_motor_measure(&motor_chassis[i], rx_message);
				//Record time
        //��¼ʱ��
        DetectHook(ChassisMotor1TOE + i);
        break;
    }
    
//    case CAN_TX2_ID:
//    {
//				//����TX2����
//				//Process TX2 data
//			  //buzzer_on(150,10000);
//        //get_tx2_measure(&tx2,rx_message);
//		
//        break;
//		}
		case GYRO_DATA_TX2_ID:
    {
				//������̨�����Ǿ��ԽǶȵ�TX2����

				//CAN_GIMBAL_GYRO_DATA_TX2()
        break;
		}
		
		case CAN_AIM_DATA_ID:
		{
				get_aim_data(&tx2,rx_message);//tx2����
				break;
		}


    default:
    {
        break;
    }
    }
}
