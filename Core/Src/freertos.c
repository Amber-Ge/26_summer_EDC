/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* 引入初始化门控接口：默认任务启动后先等待 InitTask 完成全局初始化。 */
#include "task_init.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for GpioTask */
osThreadId_t GpioTaskHandle;
const osThreadAttr_t GpioTask_attributes = {
  .name = "GpioTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for KeyTask */
osThreadId_t KeyTaskHandle;
const osThreadAttr_t KeyTask_attributes = {
  .name = "KeyTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for OledTask */
osThreadId_t OledTaskHandle;
const osThreadAttr_t OledTask_attributes = {
  .name = "OledTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for TestTask */
osThreadId_t TestTaskHandle;
const osThreadAttr_t TestTask_attributes = {
  .name = "TestTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for StepperTask */
osThreadId_t StepperTaskHandle;
const osThreadAttr_t StepperTask_attributes = {
  .name = "StepperTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for DccTask */
osThreadId_t DccTaskHandle;
const osThreadAttr_t DccTask_attributes = {
  .name = "DccTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for InitTask */
osThreadId_t InitTaskHandle;
const osThreadAttr_t InitTask_attributes = {
  .name = "InitTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityRealtime7,
};
/* Definitions for PcMutex */
osMutexId_t PcMutexHandle;
const osMutexAttr_t PcMutex_attributes = {
  .name = "PcMutex"
};
/* Definitions for Sem_RedLED */
osSemaphoreId_t Sem_RedLEDHandle;
const osSemaphoreAttr_t Sem_RedLED_attributes = {
  .name = "Sem_RedLED"
};
/* Definitions for Sem_Dcc */
osSemaphoreId_t Sem_DccHandle;
const osSemaphoreAttr_t Sem_Dcc_attributes = {
  .name = "Sem_Dcc"
};
/* Definitions for Sem_TaskChange */
osSemaphoreId_t Sem_TaskChangeHandle;
const osSemaphoreAttr_t Sem_TaskChange_attributes = {
  .name = "Sem_TaskChange"
};
/* Definitions for Sem_Init */
osSemaphoreId_t Sem_InitHandle;
const osSemaphoreAttr_t Sem_Init_attributes = {
  .name = "Sem_Init"
};
/* Definitions for Sem_ReadyToggle */
osSemaphoreId_t Sem_ReadyToggleHandle;
const osSemaphoreAttr_t Sem_ReadyToggle_attributes = {
  .name = "Sem_ReadyToggle"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
extern void StartGpioTask(void *argument);
extern void StartKeyTask(void *argument);
extern void StartOledTask(void *argument);
extern void StartTestTask(void *argument);
extern void StartStepperTask(void *argument);
extern void StartDccTask(void *argument);
extern void StartInitTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of PcMutex */
  PcMutexHandle = osMutexNew(&PcMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of Sem_RedLED */
  Sem_RedLEDHandle = osSemaphoreNew(1, 0, &Sem_RedLED_attributes);

  /* creation of Sem_Dcc */
  Sem_DccHandle = osSemaphoreNew(1, 0, &Sem_Dcc_attributes);

  /* creation of Sem_TaskChange */
  Sem_TaskChangeHandle = osSemaphoreNew(1, 0, &Sem_TaskChange_attributes);

  /* creation of Sem_Init */
  Sem_InitHandle = osSemaphoreNew(1, 0, &Sem_Init_attributes);

  /* creation of Sem_ReadyToggle */
  Sem_ReadyToggleHandle = osSemaphoreNew(1, 0, &Sem_ReadyToggle_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of GpioTask */
  GpioTaskHandle = osThreadNew(StartGpioTask, NULL, &GpioTask_attributes);

  /* creation of KeyTask */
  KeyTaskHandle = osThreadNew(StartKeyTask, NULL, &KeyTask_attributes);

  /* creation of OledTask */
  OledTaskHandle = osThreadNew(StartOledTask, NULL, &OledTask_attributes);

  /* creation of TestTask */
  TestTaskHandle = osThreadNew(StartTestTask, NULL, &TestTask_attributes);

  /* creation of StepperTask */
  StepperTaskHandle = osThreadNew(StartStepperTask, NULL, &StepperTask_attributes);

  /* creation of DccTask */
  DccTaskHandle = osThreadNew(StartDccTask, NULL, &DccTask_attributes);

  /* creation of InitTask */
  InitTaskHandle = osThreadNew(StartInitTask, NULL, &InitTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /*
   * 统一启动门控：
   * 默认任务通常最先被调度到，先在此等待可确保系统“初始化先于业务任务”。
   * InitTask 释放 Sem_Init 后，该门控会保持常开状态。
   */
  task_wait_init_done();
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

