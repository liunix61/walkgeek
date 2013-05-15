/*
 * cpu.c
 *
 * Copyright (c) 2012, Oleg Tsaregorodtsev
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Includes ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
#include "cpu.h"
#include "system.h"
#include "profile.h"

/* Imported variables ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Private define ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Private typedef ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Private macro ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Private variables ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
static volatile u32 nesting = 0;

static u32 profile_entry_points[PF_MAX];
static u32 profile_results[PF_MAX];
static char* profile_func_names[PF_MAX];

extern unsigned int _sstack;
extern unsigned int _estack;

/* Private function prototypes ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/* Private functions ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void CPU_EnableSysTick(u16 hz)
{
  RCC_ClocksTypeDef RCC_Clocks;

  /* SysTick end of count event each 1ms !!! */
  RCC_GetClocksFreq(&RCC_Clocks);
  SysTick_Config(RCC_Clocks.HCLK_Frequency / hz);
}

void CPU_EnableFPU(void)
{
  /* (c) http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0553a/BEHBJHIG.html*/

  asm volatile (
          "LDR.W   R0, =0xE000ED88                                \n"
          "LDR     R1, [R0]                                       \n"
          "ORR     R1, R1, #(0xF << 20)                           \n"
          "STR     R1, [R0]                                       \n"
          "DSB                                                    \n"
          "ISB                                                    \n"
  );
}

void CPU_EnterLowPowerState(void)
{
  GPIO_InitTypeDef GPIO_InitStructure;

  /* Save the GPIO pins current configuration then put all GPIO pins in Analog
   Input mode ...*/

  //USB_OTG_BSP_DeInit(0);

  /* Configure all GPIO as analog to reduce current consumption on non used IOs */
  /* Enable GPIOs clock */
  RCC_AHB1PeriphClockCmd(
          RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC
                  | RCC_AHB1Periph_GPIOD | RCC_AHB1Periph_GPIOE, ENABLE);

  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
  GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Pin = GPIO_Pin_All;
  GPIO_Init(GPIOC, &GPIO_InitStructure);
  GPIO_Init(GPIOD, &GPIO_InitStructure);
  GPIO_Init(GPIOE, &GPIO_InitStructure);
  GPIO_Init(GPIOA, &GPIO_InitStructure);

  /* Disable GPIOs clock */
  RCC_AHB1PeriphClockCmd(
          RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOC
                  | RCC_AHB1Periph_GPIOD | RCC_AHB1Periph_GPIOE, DISABLE);

//  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);

  /* Enable WakeUp pin */
  //  PWR_WakeUpPinCmd(ENABLE);

  /* Enable Clock Security System(CSS) */
//  RCC_ClockSecuritySystemCmd(ENABLE);

//  PWR_FlashPowerDownCmd(ENABLE);

//  PWR_EnterSTANDBYMode();
  while(1)
    ;
}

void CPU_DisableInterrupts(void)
{
  __disable_irq();
  nesting++;
}

void CPU_RestoreInterrupts(void)
{
  if (nesting)
  {
    nesting--;
    __enable_irq();
  }
}

extern unsigned int _sheap_user;
void *CPU_GetUserHeapStart(void)
{
  return &_sheap_user;
}

size_t CPU_GetUserHeapSize(void)
{
  extern unsigned int _ssram1;

  int has_256K_sram = (*(volatile uint16_t *) 0x1FFF7A22 == 2048);
  char *_eheap_user = (char *) &_ssram1 + (has_256K_sram ? 64 : 0) * 1024;

  return (size_t) (_eheap_user - (char *) &_sheap_user);
}

size_t CPU_GetStackSize(void)
{
  return (size_t) (&_estack - &_sstack);
}

void *CPU_AllocFromStackBottom(size_t size)
{
  if (size < CPU_GetStackSize())
    return &_sstack;

  return NULL;
}

void CPU_FreeStackBottom(void)
{
  //todo: refill with 0xa5 pattern
}

#if PROFILING
static u32 uS_Profiler_GetValue(void)
{
  u32 ret;

  CPU_DisableInterrupts();

  ret = SysMsCounter*1000 + (SysTick->VAL) / (SysTick->LOAD + 1);

  CPU_RestoreInterrupts();

  return ret;
}

static u32 uS_Profiler_GetDiff(u32 value)
{
  u32 cur;

  cur = uS_Profiler_GetValue();

  assert_param(cur >= value);

  return cur - value;
}

void Profiler_DoEnterFunc(char *func_name, ProfileFunction_Typedef func)
{
  assert_param(func < PF_MAX);

  profile_func_names[func] = func_name;

  profile_entry_points[func] = uS_Profiler_GetValue();
}

unsigned int Profiler_GetResult(ProfileFunction_Typedef func)
{
  assert_param(func < PF_MAX);

  return profile_results[func];
}

void Profiler_ExitFunc(ProfileFunction_Typedef func)
{
  assert_param(func < PF_MAX);

  profile_results[func] += uS_Profiler_GetDiff(profile_entry_points[func]);
}

void Profiler_Print(void)
{
  int i;

  if (!profile_results[PF_TOTAL])
  {
    printf("Ooops... no profile results\n");
    return;
  }

  printf("\nCPU profile results:\n");

  for (i = 0; i < PF_MAX; i++)
  {
    printf("%s:\t%3u.%02u%%\n", profile_func_names[i],
            FLOAT_TO_1_2((double) profile_results[i] / ((double) profile_results[PF_TOTAL] / 100)));
  }
}
#endif
