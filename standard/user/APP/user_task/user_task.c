/**
  ****************************(C) COPYRIGHT 2016 DJI****************************
  * @file       user_task.c/h
  * @brief      *һ����ͨ������������豸�޴����̵�1Hz��˸,Ȼ���ȡ��̬��*
  *             ������filter�����߳�
  * @note       
  * @history
  *  Version    Date            Author          Modification
  *  V1.0.0     Dec-26-2018     RM              1. ���
  *
  @verbatim
  ==============================================================================

  ==============================================================================
  @endverbatim
  ****************************(C) COPYRIGHT 2016 DJI****************************
  */

#include "User_Task.h"
#include "main.h"

#include "FreeRTOSConfig.h"
#include "FreeRTOS.h"
#include "task.h"

#include "led.h"

#include "Detect_Task.h"
#include "INS_Task.h"



//�Զ���
#include "gimbal_task.h"
#include "filter.h"
#include "CAN_Receive.h"
#include "chassis_task.h"


#define user_is_error() toe_is_error(errorListLength)

#if INCLUDE_uxTaskGetStackHighWaterMark
uint32_t UserTaskStack;
#endif

//��̬�� ��λ��
fp32 angle_degree[3] = {0.0f, 0.0f, 0.0f};


////�Զ�������
//����ṹ��
Gimbal_Control_t gimbal_control;
tx2_aim_package_t tx2;


//����filter����
Group_Delay_t group_delay_ecd_aim;
Group_Delay_t group_delay_gyro_aim;
kalman_filter_t kalman;
kalman_filter_init_t kalman_initial;


//����filter
double Group_Delay(Group_Delay_t *GD);
float *kalman_filter_calc(kalman_filter_t *F, float x, float y, float vx, float vy);

//��ʼ��Kalman
void kalman_filter_init(kalman_filter_t *F, kalman_filter_init_t *I);

//�������ݣ����������ļ�
//extern �����������ͱ���һ��
extern fp32 delayed_yaw_relative_angle;
extern fp32 delayed_yaw_absolute_angle;
extern fp32 delayed_pitch_relative_angle;
extern fp32 *filtered_aim_data[4];
extern fp32 *filtered_final_angle_set[4];

//��������
int32_t final_yaw_angle_set;
int32_t final_pitch_angle_set;


//����flag
static int filter_aim_data_flag;
static int filter_final_angle_set_flag;



static void Filter_Running(Gimbal_Control_t *gimbal_data)
{

		
		filter_aim_data_flag=1;
		filter_final_angle_set_flag=0;
	
		
		//�Ƿ���Ӿ��������������ݽ����˲�
		if(filter_aim_data_flag==1)
		{
			*filtered_aim_data=kalman_filter_calc(&kalman,
																						tx2.horizontal_pixel,
																						tx2.vertical_pixel,
																						gimbal_control.gimbal_yaw_motor.motor_gyro,
																						gimbal_control.gimbal_pitch_motor.motor_gyro);
			

		}
		else if(filter_aim_data_flag==0)
		{
			static fp32 temp_aim_data[4];
			temp_aim_data[0]=tx2.horizontal_pixel;
			temp_aim_data[1]=tx2.vertical_pixel;
			temp_aim_data[2]=gimbal_control.gimbal_yaw_motor.motor_gyro;
			temp_aim_data[3]=gimbal_control.gimbal_pitch_motor.motor_gyro;
			*filtered_aim_data=temp_aim_data;
		}
		
		
		//�Ƿ�����սǶȽ����˲�
		if(filter_final_angle_set_flag==1)
		{	
			*filtered_final_angle_set=kalman_filter_calc(&kalman,
																									 final_yaw_angle_set,
																									 final_pitch_angle_set,
																									 gimbal_control.gimbal_yaw_motor.motor_gyro,
																									 gimbal_control.gimbal_pitch_motor.motor_gyro);
		}
		else if(filter_final_angle_set_flag==0)
		{	
			static fp32 temp_final_angle_set[4];
			temp_final_angle_set[0]=final_yaw_angle_set;
			temp_final_angle_set[1]=final_pitch_angle_set;
			temp_final_angle_set[2]=gimbal_control.gimbal_yaw_motor.motor_gyro;
			temp_final_angle_set[3]=gimbal_control.gimbal_pitch_motor.motor_gyro;
			*filtered_final_angle_set=temp_final_angle_set;
		}		
		
		//��ȡ����x ms group delay��ı�����pitch���ֵ
		delayed_pitch_relative_angle=Group_Delay(&group_delay_ecd_aim);
		//��ȡ����x ms group delay���������yaw���ֵ
		delayed_yaw_absolute_angle=Group_Delay(&group_delay_gyro_aim);
	}


void UserTask(void *pvParameters)
{

		const volatile fp32 *angle;
		const fp32 dt=0;//����Ƶ�ʣ�
		//��ȡ��̬��ָ��
		angle = get_INS_angle_point();
	
		//��ʼ��Kalman Filter����
		//Covariance of the Process Noise Matrix Q
		kalman_initial.Q_data[0]	= 1;			kalman_initial.Q_data[1]	=	0;			kalman_initial.Q_data[2]	=	0;			kalman_initial.Q_data[3]	=	0;
		kalman_initial.Q_data[4]	= 0;			kalman_initial.Q_data[5]	=	1;			kalman_initial.Q_data[6]	=	0;			kalman_initial.Q_data[7]	=	0;
		kalman_initial.Q_data[8]	= 0;			kalman_initial.Q_data[9]	=	0;			kalman_initial.Q_data[10]	=	1;			kalman_initial.Q_data[11]	=	0;
		kalman_initial.Q_data[12]	=	0;			kalman_initial.Q_data[13]	=	0;			kalman_initial.Q_data[14]	=	0;			kalman_initial.Q_data[15]	=	1;
		
		//Covariance of the Measurement Noise Matrix R
		kalman_initial.R_data[0]	= 20000;	kalman_initial.R_data[1]	=	0;			kalman_initial.R_data[2]	=	0;			kalman_initial.R_data[3]	=	0;
		kalman_initial.R_data[4]	= 0;			kalman_initial.R_data[5]	=	20000;	kalman_initial.R_data[6]	=	0;			kalman_initial.R_data[7]	=	0;
		kalman_initial.R_data[8]	= 0;			kalman_initial.R_data[9]	=	0;			kalman_initial.R_data[10]	=	20000;	kalman_initial.R_data[11]	=	0;
		kalman_initial.R_data[12]	=	0;			kalman_initial.R_data[13]	=	0;			kalman_initial.R_data[14]	=	0;			kalman_initial.R_data[15]	=	20000;
		
		//System Term Matrix A
		kalman_initial.A_data[0]	= 1;			kalman_initial.A_data[1]	=	0;			kalman_initial.A_data[2]	=	dt;			kalman_initial.A_data[3]	=	0;
		kalman_initial.A_data[4]	= 0;			kalman_initial.A_data[5]	=	1;			kalman_initial.A_data[6]	=	0;			kalman_initial.A_data[7]	=	dt;
		kalman_initial.A_data[8]	= 0;			kalman_initial.A_data[9]	=	0;			kalman_initial.A_data[10]	=	1;			kalman_initial.A_data[11]	=	0;
		kalman_initial.A_data[12]	=	0;			kalman_initial.A_data[13]	=	0;			kalman_initial.A_data[14]	=	0;			kalman_initial.A_data[15]	=	1;
		
		kalman_initial.AT_data[0]	= 0;			kalman_initial.AT_data[1]	=	0;			kalman_initial.AT_data[2]	=	0;			kalman_initial.AT_data[3]	=	0;
		kalman_initial.AT_data[4]	= 0;			kalman_initial.AT_data[5]	=	0;			kalman_initial.AT_data[6]	=	0;			kalman_initial.AT_data[7]	=	0;
		kalman_initial.AT_data[8]	= 0;			kalman_initial.AT_data[9]	=	0;			kalman_initial.AT_data[10]=	0;			kalman_initial.AT_data[11]=	0;
		kalman_initial.AT_data[12]=	0;			kalman_initial.AT_data[13]=	0;			kalman_initial.AT_data[14]=	0;			kalman_initial.AT_data[15]=	0;
	
	
		//Observation Model Matrix H
		kalman_initial.H_data[0]	= 1;			kalman_initial.H_data[1]	=	0;			kalman_initial.H_data[2]	=	0;			kalman_initial.H_data[3]	=	0;
		kalman_initial.H_data[4]	= 0;			kalman_initial.H_data[5]	=	1;			kalman_initial.H_data[6]	=	0;			kalman_initial.H_data[7]	=	0;
		kalman_initial.H_data[8]	= 0;			kalman_initial.H_data[9]	=	0;			kalman_initial.H_data[10]	=	1;			kalman_initial.H_data[11]	=	0;
		kalman_initial.H_data[12]	=	0;			kalman_initial.H_data[13]	=	0;			kalman_initial.H_data[14]	=	0;			kalman_initial.H_data[15]	=	1;
		
		kalman_initial.HT_data[0]	= 0;			kalman_initial.HT_data[1]	=	0;			kalman_initial.HT_data[2]	=	0;			kalman_initial.HT_data[3]	=	0;
		kalman_initial.HT_data[4]	= 0;			kalman_initial.HT_data[5]	=	0;			kalman_initial.HT_data[6]	=	0;			kalman_initial.HT_data[7]	=	0;
		kalman_initial.HT_data[8]	= 0;			kalman_initial.HT_data[9]	=	0;			kalman_initial.HT_data[10]=	0;			kalman_initial.HT_data[11]=	0;
		kalman_initial.HT_data[12]=	0;			kalman_initial.HT_data[13]=	0;			kalman_initial.HT_data[14]=	0;			kalman_initial.HT_data[15]=	0;

		//Prior Estimate
		kalman_initial.xhat_data[0]=0;
		kalman_initial.xhat_data[1]=0;
		kalman_initial.xhat_data[2]=0;
		kalman_initial.xhat_data[3]=0;
		
		//Last State of Prior Estimate
		kalman_initial.xhatminus_data[0]=0;			
		kalman_initial.xhatminus_data[1]=0;
		kalman_initial.xhatminus_data[2]=0;			
		kalman_initial.xhatminus_data[3]=0;
		
		//Actual Measurement of x
		kalman_initial.z_data[0]=0;
		kalman_initial.z_data[1]=0;
		kalman_initial.z_data[2]=0;
		kalman_initial.z_data[3]=0;
	
		//Covariance of the Estimation-Error Matrix P-
		kalman_initial.Pminus_data[0]	= 0;	kalman_initial.Pminus_data[1]	=	0;	kalman_initial.Pminus_data[2]	=	0;		kalman_initial.Pminus_data[3]	=	0;
		kalman_initial.Pminus_data[4]	= 0;	kalman_initial.Pminus_data[5]	=	0;	kalman_initial.Pminus_data[6]	=	0;		kalman_initial.Pminus_data[7]	=	0;
		kalman_initial.Pminus_data[8]	= 0;	kalman_initial.Pminus_data[9]	=	0;	kalman_initial.Pminus_data[10]=	0;		kalman_initial.Pminus_data[11]=	0;
		kalman_initial.Pminus_data[12]=	0;	kalman_initial.Pminus_data[13]=	0;	kalman_initial.Pminus_data[14]=	0;		kalman_initial.Pminus_data[15]=	0;
		
		//Kalman Gain Matrix K
		kalman_initial.K_data[0]	= 0;			kalman_initial.K_data[1]	=	0;			kalman_initial.K_data[2]	=	0;				kalman_initial.K_data[3]	=	0;
		kalman_initial.K_data[4]	= 0;			kalman_initial.K_data[5]	=	0;			kalman_initial.K_data[6]	=	0;				kalman_initial.K_data[7]	=	0;
		kalman_initial.K_data[8]	= 0;			kalman_initial.K_data[9]	=	0;			kalman_initial.K_data[10] =	0;				kalman_initial.K_data[11] =	0;
		kalman_initial.K_data[12] =	0;			kalman_initial.K_data[13] =	0;			kalman_initial.K_data[14] =	0;				kalman_initial.K_data[15] =	0;

		//Initial Covariance of the Estimation-Error Matrix P
		kalman_initial.P_data[0]	= 0;			kalman_initial.P_data[1]	=	0;			kalman_initial.P_data[2]	=	0;				kalman_initial.P_data[3]	=	0;
		kalman_initial.P_data[4]	= 0;			kalman_initial.P_data[5]	=	0;			kalman_initial.P_data[6]	=	0;				kalman_initial.P_data[7]	=	0;
		kalman_initial.P_data[8]	= 0;			kalman_initial.P_data[9]	=	0;			kalman_initial.P_data[10] =	0;				kalman_initial.P_data[11] =	0;
		kalman_initial.P_data[12] =	0;			kalman_initial.P_data[13] =	0;			kalman_initial.P_data[14] =	0;				kalman_initial.P_data[15] =	0;
	
		//��ʼ��Kalman Filter
		kalman_filter_init(&kalman,&kalman_initial);
    while (1)
    {

        //��̬�� ��rad ��� �ȣ����������̬�ǵĵ�λΪ�ȣ������ط�����̬�ǣ���λ��Ϊ����
        angle_degree[0] = (*(angle + INS_YAW_ADDRESS_OFFSET)) * 57.3f;
        angle_degree[1] = (*(angle + INS_PITCH_ADDRESS_OFFSET)) * 57.3f;
        angle_degree[2] = (*(angle + INS_ROLL_ADDRESS_OFFSET)) * 57.3f;

        if (!user_is_error())
        {
            led_green_on();
        }
				
				
				
				
				//����filter������
				//group delay������Ϊ������YAW��ĽǶ�
				group_delay_gyro_aim.group_delay_raw_value=gimbal_control.gimbal_yaw_motor.absolute_angle;
				//group delay������Ϊ������PITCH������ڵ�ecd_offset�ĽǶ�(-pi/2,pi/2)
				group_delay_ecd_aim.group_delay_raw_value=gimbal_control.gimbal_pitch_motor.relative_angle;
				
				Filter_Running(&gimbal_control);//filter���м���
				
				
				
				
				
				
				
				
				vTaskDelay(1);//ÿ��1msѭ��һ��
//        led_green_off();
//        vTaskDelay(500);
#if INCLUDE_uxTaskGetStackHighWaterMark
        UserTaskStack = uxTaskGetStackHighWaterMark(NULL);
#endif
    }
}
