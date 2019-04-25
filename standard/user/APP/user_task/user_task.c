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


#define user_is_error() toe_is_error(errorListLength)

#if INCLUDE_uxTaskGetStackHighWaterMark
uint32_t UserTaskStack;
#endif

//��̬�� ��λ��
fp32 angle_degree[3] = {0.0f, 0.0f, 0.0f};


////�Զ�������
//����ṹ�壬��ȡ����
Gimbal_Control_t gimbal_control;
tx2_aim_package_t tx2;
int32_t final_angle_set;

//����filter����
Group_Delay_t group_delay;
Blackman_Filter_t blackman;
IIR_Filter_t butterworth_aim_yaw;
IIR_Filter_t butterworth_aim_pitch;
IIR_Filter_t butterworth_final_angle;

//����filter
double Group_Delay(Group_Delay_t *GD);
double Blackman_Filter(Blackman_Filter_t *F);
double Butterworth_Filter(IIR_Filter_t *F);

//extern �˲�������ݣ����������ļ�
extern fp32 delayed_relative_angle;
extern fp32 filtered_horizontal_pixel;//extern �����������ͱ���һ��
extern int32_t filtered_vertical_pixel;
extern fp32 filtered_final_angle_set;

//����flag
static int filter_tx2_yaw_data_flag;
static int filter_final_angle_set_flag;




static void Filter_Running(Gimbal_Control_t *gimbal_data)
	{

		
		filter_tx2_yaw_data_flag=1;
		filter_final_angle_set_flag=0;//ûʲôЧ��
		
		
		
		
		//�Ƿ��tx2������yaw���������ݽ����˲�
		if(filter_tx2_yaw_data_flag==1)
		{
			filtered_horizontal_pixel=Butterworth_Filter(&butterworth_aim_yaw);//Blackman_Filter(&blackman);
		}
		else if(filter_tx2_yaw_data_flag==0)
		{
			filtered_horizontal_pixel=tx2.horizontal_pixel;
		}
		
		//�Ƿ��tx2������pitch���������ݽ����˲���pitch����������ƽ����Ԥ��
//		if(filter_tx2_yaw_data_flag==1)
//		{
//			filtered_horizontal_pixel=Butterworth_Filter(&butterworth_aim_yaw);//Blackman_Filter(&blackman);
//		}
//		else if(filter_tx2_yaw_data_flag==0)
//		{
			filtered_vertical_pixel=tx2.horizontal_pixel;
//		}
		
		
		//�Ƿ�����սǶȽ����˲�
		if(filter_final_angle_set_flag==1)
		{
			filtered_final_angle_set=Butterworth_Filter(&butterworth_final_angle);
		}
		else if(filter_final_angle_set_flag==0)
		{
			filtered_final_angle_set=final_angle_set;
		}		
		
		
		
		
		
		delayed_relative_angle=Group_Delay(&group_delay);//����x ms delay��ı�������ֵ

	}


void UserTask(void *pvParameters)
{

    const volatile fp32 *angle;
    //��ȡ��̬��ָ��
    angle = get_INS_angle_point();
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
				
				
				
				
				//filter������
				group_delay.group_delay_raw_value=gimbal_control.gimbal_yaw_motor.relative_angle;//group delay fir filter����Ϊ���������ecd_offset�ĽǶ�(-pi/2,pi/2)			
				butterworth_aim_yaw.raw_value=tx2.horizontal_pixel;
				butterworth_final_angle.raw_value=final_angle_set;
				
				
				Filter_Running(&gimbal_control);//filter���м���
				
				
				
				
				
				
				
				
				vTaskDelay(1);//ÿ��1msѭ��һ��
//        led_green_off();
//        vTaskDelay(500);
#if INCLUDE_uxTaskGetStackHighWaterMark
        UserTaskStack = uxTaskGetStackHighWaterMark(NULL);
#endif
    }
}
