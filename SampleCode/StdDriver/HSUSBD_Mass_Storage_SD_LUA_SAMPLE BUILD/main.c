/**************************************************************************//**
 * @file     main.c
 * @version  V3.00
 * @brief    Use SD as back end storage media to simulate an USB pen drive.
 *
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @copyright Copyright (C) 2021 Nuvoton Technology Corp. All rights reserved.
 ******************************************************************************/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "NuMicro.h"
#include "massstorage.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"
#include "lauxlib.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "luaconf.h"
extern char LUA_SCRIPT_GLOBAL[4096];
extern int luaopen_mylib(lua_State *L);
uint8_t volatile g_u8SdInitFlag = 0;
extern int32_t g_TotalSectors;
/*--------------------------------------------------------------------------*/

void SDH0_IRQHandler(void)
{
    unsigned int volatile isr;
    unsigned int volatile ier;

    // FMI data abort interrupt
    if (SDH0->GINTSTS & SDH_GINTSTS_DTAIF_Msk)
    {
        /* ResetAllEngine() */
        SDH0->GCTL |= SDH_GCTL_GCTLRST_Msk;
    }

    //----- SD interrupt status
    isr = SDH0->INTSTS;

    if (isr & SDH_INTSTS_BLKDIF_Msk)
    {
        // block down
        SD0.DataReadyFlag = TRUE;
        SDH0->INTSTS = SDH_INTSTS_BLKDIF_Msk;
    }

    if (isr & SDH_INTSTS_CDIF_Msk)
    {
        // card detect
        //----- SD interrupt status
        // it is work to delay 50 times for SD_CLK = 200KHz
        {
            int volatile i;         // delay 30 fail, 50 OK

            for (i = 0; i < 0x500; i++); // delay to make sure got updated value from REG_SDISR.

            isr = SDH0->INTSTS;
        }

        if (isr & SDH_INTSTS_CDSTS_Msk)
        {
            printf("\n***** card remove !\n");
            SD0.IsCardInsert = FALSE;   // SDISR_CD_Card = 1 means card remove for GPIO mode
            memset(&SD0, 0, sizeof(SDH_INFO_T));
        }
        else
        {
            printf("***** card insert !\n");
            SDH_Open(SDH0, CardDetect_From_GPIO);

            if (SDH_Probe(SDH0))
            {
                g_u8SdInitFlag = 0;
                printf("SD initial fail!!\n");
            }
            else
            {
                g_u8SdInitFlag = 1;
                g_TotalSectors = SD0.totalSectorN;
            }
        }

        SDH0->INTSTS = SDH_INTSTS_CDIF_Msk;
    }

    // CRC error interrupt
    if (isr & SDH_INTSTS_CRCIF_Msk)
    {
        if (!(isr & SDH_INTSTS_CRC16_Msk))
        {
            //printf("***** ISR sdioIntHandler(): CRC_16 error !\n");
            // handle CRC error
        }
        else if (!(isr & SDH_INTSTS_CRC7_Msk))
        {
            if (!SD0.R3Flag)
            {
                //printf("***** ISR sdioIntHandler(): CRC_7 error !\n");
                // handle CRC error
            }
        }

        SDH0->INTSTS = SDH_INTSTS_CRCIF_Msk;      // clear interrupt flag
    }

    if (isr & SDH_INTSTS_DITOIF_Msk)
    {
        printf("***** ISR: data in timeout !\n");
        SDH0->INTSTS |= SDH_INTSTS_DITOIF_Msk;
    }

    // Response in timeout interrupt
    if (isr & SDH_INTSTS_RTOIF_Msk)
    {
        printf("***** ISR: response in timeout !\n");
        SDH0->INTSTS |= SDH_INTSTS_RTOIF_Msk;
    }

    portYIELD_FROM_ISR(NULL);
}


void SYS_Init(void)
{
    uint32_t volatile i;

    /* Unlock protected registers */
    SYS_UnlockReg();

    /*---------------------------------------------------------------------------------------------------------*/
    /* Init System Clock                                                                                       */
    /*---------------------------------------------------------------------------------------------------------*/

    /* Enable HIRC and HXT clock */
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk | CLK_PWRCTL_HXTEN_Msk);

    /* Wait for HIRC and HXT clock ready */
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk | CLK_STATUS_HXTSTB_Msk);

    /* Set PCLK0 and PCLK1 to HCLK/2 */
    CLK->PCLKDIV = (CLK_PCLKDIV_APB0DIV_DIV2 | CLK_PCLKDIV_APB1DIV_DIV2);

    /* Set core clock to 200MHz */
    CLK_SetCoreClock(FREQ_200MHZ);

    /* Enable all GPIO clock */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPACKEN_Msk | CLK_AHBCLK0_GPBCKEN_Msk | CLK_AHBCLK0_GPCCKEN_Msk | CLK_AHBCLK0_GPDCKEN_Msk |
                    CLK_AHBCLK0_GPECKEN_Msk | CLK_AHBCLK0_GPFCKEN_Msk | CLK_AHBCLK0_GPGCKEN_Msk | CLK_AHBCLK0_GPHCKEN_Msk;
    CLK->AHBCLK1 |= CLK_AHBCLK1_GPICKEN_Msk | CLK_AHBCLK1_GPJCKEN_Msk;

    /* Enable UART0 module clock */
    CLK_EnableModuleClock(UART0_MODULE);

    /* Select UART0 module clock source as HIRC and UART0 module clock divider as 1 */
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART0SEL_HIRC, CLK_CLKDIV0_UART0(1));

    /* Select HSUSBD */
    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;

    /* Enable USB PHY */
    SYS->USBPHY = (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk)) | SYS_USBPHY_HSUSBEN_Msk;

    for (i = 0; i < 0x1000; i++);  // delay > 10 us

    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;

    /* Enable HSUSBD module clock */
    CLK_EnableModuleClock(HSUSBD_MODULE);

    /* Enable SDH0 module clock */
    CLK_EnableModuleClock(SDH0_MODULE);

    /* Select SDH0 module clock source as HCLK and SDH0 module clock divider as 10 */
    CLK_SetModuleClock(SDH0_MODULE, CLK_CLKSEL0_SDH0SEL_HCLK, CLK_CLKDIV0_SDH0(10));
    /* Enable Tiemr 0 module clock */
    CLK_EnableModuleClock(TMR0_MODULE);

    /* Select Timer 0 module clock source as HXT */
    CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HXT, 0);
    /*---------------------------------------------------------------------------------------------------------*/
    /* Init I/O Multi-function                                                                                 */
    /*---------------------------------------------------------------------------------------------------------*/

    /* Set multi-function pins for UART0 RXD and TXD */
    SET_UART0_RXD_PB12();
    SET_UART0_TXD_PB13();

    /* Select multi-function pins for SD0 */
    SET_SD0_DAT0_PE2();
    SET_SD0_DAT1_PE3();
    SET_SD0_DAT2_PE4();
    SET_SD0_DAT3_PE5();
    SET_SD0_CLK_PE6();
    SET_SD0_CMD_PE7();
    SET_SD0_nCD_PD13();

    /* Lock protected registers */
    SYS_LockReg();
}

// 任務優先級
#define USB_MSD_SD_TASK_PRIO 2
// 任務堆棧大小
#define USB_MSD_SD_STK_SIZE 256
// 任務句柄
TaskHandle_t USB_MSD_SD_Handler;

// 任務優先級
#define BUTTON_TASK_PRIO 3
// 任務堆棧大小
#define BUTTON_STK_SIZE 1024
// 任務句柄
TaskHandle_t BUTTON_Task_Handler;
// 任務函數
void BUTTON_task(void *pvParameters);


// 任務優先級
#define START_TASK_PRIO 1
// 任務堆棧大小
#define START_STK_SIZE 256
// 任務句柄
TaskHandle_t StartTask_Handler;
// 任務函數
void USB_MSD_SD_task(void *pvParameters);
void start_task(void *pvParameters)
{
    taskENTER_CRITICAL();

    xTaskCreate((TaskFunction_t)USB_MSD_SD_task,
                (const char *)"vcom_task",
                (uint16_t)USB_MSD_SD_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)USB_MSD_SD_TASK_PRIO,
                (TaskHandle_t *)&USB_MSD_SD_Handler);

    xTaskCreate((TaskFunction_t)BUTTON_task,
                (const char *)"task2_task",
                (uint16_t)BUTTON_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)BUTTON_TASK_PRIO,
                (TaskHandle_t *)&BUTTON_Task_Handler);

    vTaskDelete(StartTask_Handler);
    taskEXIT_CRITICAL();
}
uint32_t volatile gSec = 0;
uint32_t volatile gSdInit = 0;
void TMR0_IRQHandler(void)
{
    gSec++;

    // clear timer interrupt flag
    TIMER_ClearIntFlag(TIMER0);

}

unsigned long get_fattime(void)
{
    unsigned long tmr;

    tmr = 0x00000;

    return tmr;
}
void timer_init()
{
    // Set timer frequency to 1HZ
    TIMER_Open(TIMER0, TIMER_PERIODIC_MODE, 1000);

    // Enable timer interrupt
    TIMER_EnableInt(TIMER0);
    NVIC_EnableIRQ(TMR0_IRQn);


    // Start Timer 0
    TIMER_Start(TIMER0);
}
/*---------------------------------------------------------------------------------------------------------*/
/*  Main Function                                                                                          */
/*---------------------------------------------------------------------------------------------------------*/
int main(void)
{
    /* Init System, peripheral clock and multi-function I/O */
    SYS_Init();

    /* Init UART to 115200-8n1 for print message */
    UART_Open(UART0, 115200);

    printf("NuMicro HSUSBD Mass Storage\n");

    /* initial SD card */
    SDH_Open(SDH0, CardDetect_From_GPIO);

    if (SDH_Probe(SDH0))
    {
        g_u8SdInitFlag = 0;
        printf("SD initial fail!!\n");
    }
    else
        g_u8SdInitFlag = 1;

    HSUSBD_Open(&gsHSInfo, MSC_ClassRequest, NULL);
    timer_init();
    /* Endpoint configuration */
    MSC_Init();

    /* Enable HSUSBD interrupt */
    NVIC_EnableIRQ(USBD20_IRQn);


    xTaskCreate((TaskFunction_t)start_task,
                (const char *)"start_task",
                (uint16_t)START_STK_SIZE,
                (void *)NULL,
                (UBaseType_t)START_TASK_PRIO,
                (TaskHandle_t *)&StartTask_Handler);
    vTaskStartScheduler();
#if 0

    /* Start transaction */
    while (1)
    {
        if (HSUSBD_IS_ATTACHED())
        {
            HSUSBD_Start();
            break;
        }
    }

    while (1)
    {
        if (g_hsusbd_Configured)
            MSC_ProcessCmd();
    }

#endif
}


/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook(void)
{
    /* vApplicationMallocFailedHook() will only be called if
    configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
    function that will get called if a call to pvPortMalloc() fails.
    pvPortMalloc() is called internally by the kernel whenever a task, queue,
    timer or semaphore is created.  It is also called by various parts of the
    demo application.  If heap_1.c or heap_2.c are used, then the size of the
    heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
    FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
    to query the size of free heap space that remains (although it does not
    provide information on how the remaining heap might be fragmented). */
    taskDISABLE_INTERRUPTS();

    for (;;)
        ;
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook(void)
{
    /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
    to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
    task.  It is essential that code added to this hook function never attempts
    to block in any way (for example, call xQueueReceive() with a block time
    specified, or call vTaskDelay()).  If the application makes use of the
    vTaskDelete() API function (as this demo application does) then it is also
    important that vApplicationIdleHook() is permitted to return to its calling
    function, because it is the responsibility of the idle task to clean up
    memory allocated by the kernel to any task that has since been deleted. */
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook(xTaskHandle pxTask, signed char *pcTaskName)
{
    (void)pcTaskName;
    (void)pxTask;

    /* Run time stack overflow checking is performed if
    configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
    function is called if a stack overflow is detected. */
    taskDISABLE_INTERRUPTS();

    for (;;)
        ;
}
#define BUFF_SIZE       (8*1024)
char Line[256]; 
static UINT blen = BUFF_SIZE;
uint8_t Buff_Pool[BUFF_SIZE] __attribute__((aligned(4)));       /* Working buffer */
#define DEF_CARD_DETECT_SOURCE       CardDetect_From_GPIO
uint8_t  *Buff;
FILINFO Finfo;
void put_rc(FRESULT rc)
{
    const TCHAR *p =
        _T("OK\0DISK_ERR\0INT_ERR\0NOT_READY\0NO_FILE\0NO_PATH\0INVALID_NAME\0")
        _T("DENIED\0EXIST\0INVALID_OBJECT\0WRITE_PROTECTED\0INVALID_DRIVE\0")
        _T("NOT_ENABLED\0NO_FILE_SYSTEM\0MKFS_ABORTED\0TIMEOUT\0LOCKED\0")
        _T("NOT_ENOUGH_CORE\0TOO_MANY_OPEN_FILES\0");

    uint32_t i;
    for(i = 0; (i != (UINT)rc) && *p; i++)
    {
        while(*p++) ;
    }
    printf(_T("rc=%u FR_%s\n"), (UINT)rc, p);
}

unsigned char load_lua_script(void)
{
    unsigned int cnt = 0;
    FIL file1;

    if (f_open(&file1, (const char *)"0:\\isp.lua", FA_OPEN_EXISTING | FA_READ))
    {
        return 1;//false
    }

    f_read(&file1, LUA_SCRIPT_GLOBAL, 4096, &cnt);
    printf("lua file size %d\n\r", cnt);
    f_close(&file1);
    return 0;
}

void BUTTON_task(void *pvParameters)
{
	 lua_State *L = NULL;
    TCHAR       sd_path[] = { '0', ':', 0 };    /* SD drive started from 0 */
		#if 0
    char        *ptr, *ptr2;
    long        p1, p2, p3;
    BYTE        *buf;
    FATFS       *fs;              /* Pointer to file system object */
    BYTE        SD_Drv = 0;

    FRESULT     res;

    DIR dir;                /* Directory object */
    UINT s1, s2, cnt;
    static const BYTE ft[] = {0, 12, 16, 32};
    DWORD ofs = 0, sect = 0;

    Buff = (BYTE *)Buff_Pool;
		#endif
    while (1)
    {
        if (!(SDH_CardDetection(SDH0)))
        {
            gSdInit = 0;
            printf("No card!!\n");
        }

        if ((PH0 == 0))
        {					
            printf("open disk and file system\n\r");
					  if (gSdInit==0)
						{
            gSdInit = (SDH_Open_Disk(SDH0, DEF_CARD_DETECT_SOURCE) == 0) ? 1 : 0;
						}
            f_chdrive(sd_path);          /* set default path */
	#if 0					
						ptr = Line;
            

            res = f_opendir(&dir, ptr);

            if (res)
            {
                put_rc(res);
                break;
            }

            p1 = s1 = s2 = 0;

            for (;;)
            {
                res = f_readdir(&dir, &Finfo);

                if ((res != FR_OK) || !Finfo.fname[0]) break;

                if (Finfo.fattrib & AM_DIR)
                {
                    s2++;
                }
                else
                {
                    s1++;
                    p1 += Finfo.fsize;
                }

                printf("%c%c%c%c%c %d/%02d/%02d %02d:%02d    %9lu  %s",
                       (Finfo.fattrib & AM_DIR) ? 'D' : '-',
                       (Finfo.fattrib & AM_RDO) ? 'R' : '-',
                       (Finfo.fattrib & AM_HID) ? 'H' : '-',
                       (Finfo.fattrib & AM_SYS) ? 'S' : '-',
                       (Finfo.fattrib & AM_ARC) ? 'A' : '-',
                       (Finfo.fdate >> 9) + 1980, (Finfo.fdate >> 5) & 15, Finfo.fdate & 31,
                       (Finfo.ftime >> 11), (Finfo.ftime >> 5) & 63, Finfo.fsize, Finfo.fname);
#if _USE_LFN

                for (p2 = strlen(Finfo.fname); p2 < 14; p2++)
                    printf(" ");

                printf("%s\n", Lfname);
#else
                printf("\n");
#endif
            }

            printf("%4u File(s),%10lu bytes total\n%4u Dir(s)", s1, p1, s2);

            if (f_getfree(ptr, (DWORD *)&p1, &fs) == FR_OK)
                printf(", %10lu bytes free\n", p1 * fs->csize * 512);
    #endif 
    		 if (load_lua_script() == 0)
            {
                L = luaL_newstate();
                luaopen_base(L);
                luaopen_mylib(L);
                luaL_openlibs(L);
                printf("run lua\n\r");
                int k = luaL_dostring(L, LUA_SCRIPT_GLOBAL);
                if (k != 0) 
									printf("error!");
                //luaL_dostring(L, Buff);
                lua_close(L);
                printf("lua end\n\r");
            }


        }

        vTaskDelay(100);
    }
}

void USB_MSD_SD_task(void *pvParameters)
{
    while (1)
    {
        while (1)
        {
            if (HSUSBD_IS_ATTACHED())
            {
                HSUSBD_Start();
							if(BUTTON_Task_Handler!=NULL)
								{	
								printf("pause the BUTTON_Task_Handler\n\r");
							  vTaskSuspend(BUTTON_Task_Handler);
									
								}
                break;
            }

            vTaskDelay(10);
        }

        while (1)
        {
            //the usb is unplug state
            if (HSUSBD_IS_ATTACHED() == 0)
            {							
                printf("resume the BUTTON_Task_Handler\n\r");									
							  vTaskResume(BUTTON_Task_Handler);
							  
                break;
            }

            if (g_hsusbd_Configured)
            {
                MSC_ProcessCmd();
            }

            vTaskDelay(10);
        }
    }
}
