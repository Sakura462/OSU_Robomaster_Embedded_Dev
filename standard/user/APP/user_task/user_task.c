/**
  ****************************(C) COPYRIGHT 2016 DJI****************************
  * @file       user_task.c/h
  * @brief      һ����ͨ������������豸�޴����̵�1Hz��˸,Ȼ���ȡ��̬��
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



#define user_is_error() toe_is_error(errorListLength)

#if INCLUDE_uxTaskGetStackHighWaterMark
uint32_t UserTaskStack;
#endif

//��̬�� ��λ��
fp32 angle_degree[3] = {0.0f, 0.0f, 0.0f};


////�Զ�������
//����gimbal�ṹ�壬��ȡ����
Gimbal_Control_t gimbal_control;

//����filter����
FIR_Filter_t group_delay;
//FIR_Filter_t double_group_delay;

//����filter
double Group_Delay_Filter(FIR_Filter_t *F);
//extern group delay��ı����������������ļ�
extern fp32 delayed_ecd;
//fp32 double_delayed_value;

static void Filter_running(Gimbal_Control_t *gimbal_data)
	{
		delayed_ecd=Group_Delay_Filter(&group_delay);//����x ms delay��ı�������ֵ, ��������ο�GroupDelayTable
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
				
				
				
				
				
				group_delay.fir_raw_value=gimbal_control.gimbal_yaw_motor.relative_angle;//group delay fir filter����Ϊ���������ecd_offset�ĽǶ�(-pi/2,pi/2)

				//double_group_delay.fir_raw_value=delayed_value;
				//double_delayed_value=Group_Delay_Filter(&double_group_delay);

				
				
				
				Filter_running(&gimbal_control);//����filter
				
				
				
				
				
				
				
				
				vTaskDelay(1);//ÿ��1msѭ��һ��
//        led_green_off();
//        vTaskDelay(500);
#if INCLUDE_uxTaskGetStackHighWaterMark
        UserTaskStack = uxTaskGetStackHighWaterMark(NULL);
#endif
    }
}
