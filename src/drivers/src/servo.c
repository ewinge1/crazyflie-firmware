/**
 *    ||          ____  _ __
 * +------+      / __ )(_) /_______________ _____  ___
 * | 0xBC |     / __  / / __/ ___/ ___/ __ `/_  / / _ \
 * +------+    / /_/ / / /_/ /__/ /  / /_/ / / /_/  __/
 *  ||  ||    /_____/_/\__/\___/_/   \__,_/ /___/\___/
 *
 * Crazyflie control firmware
 *
 * Copyright (C) 2011-2012 Bitcraze AB
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, in version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * servo.c - Servo driver
 *
 * This code mainly interfacing the PWM peripheral lib of ST.
 * Author: Eric Ewing, Will Wu
 */
#define DEBUG_MODULE "SERVO"
#
#include <stdbool.h>

/* ST includes */
#include "stm32fxxx.h"

//FreeRTOS includes
#include "FreeRTOS.h"
#include "task.h"
#include "debug.h"
#include <string.h>
#include <inttypes.h>
#include "motors.h"
#include "param.h"
#include "deck.h"

// TODO Verify PWM settings
static const double SERVO_ZERO_PULSE_ms = 0.5;
static const double SERVO_180_PULSE_ms  = 2.5;

#include "servo.h"

static bool isInit = false;
// we use the "servoMapMOSI" struct to initialize PWM
extern const MotorPerifDef* servoMapMOSI;

/* Public functions */
static int8_t s_servo_angle = 0;

void servoInit()
{
  if (isInit){
    return;
  }

  GPIO_InitTypeDef GPIO_InitStructure;
  TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
  TIM_OCInitTypeDef  TIM_OCInitStructure;

  //clock the servo pin and the timers
  RCC_AHB1PeriphClockCmd(servoMapMOSI->gpioPerif, ENABLE);
  RCC_APB1PeriphClockCmd(servoMapMOSI->timPerif, ENABLE);

  //configure gpio for timer out
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz; // TODO: verify this
  GPIO_InitStructure.GPIO_Pin = servoMapMOSI->gpioPin;
  GPIO_Init(servoMapMOSI->gpioPort, &GPIO_InitStructure);

  //map timer to alternate function
  GPIO_PinAFConfig(servoMapMOSI->gpioPort, servoMapMOSI->gpioPinSource, servoMapMOSI->gpioAF);

  //Timer configuration
  TIM_TimeBaseStructure.TIM_Period = SERVO_PWM_PERIOD;
  TIM_TimeBaseStructure.TIM_Prescaler = SERVO_PWM_PRESCALE;
  TIM_TimeBaseStructure.TIM_ClockDivision = 0;
  TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
  TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
  TIM_TimeBaseInit(servoMapMOSI->tim, &TIM_TimeBaseStructure);

  // PWM channels configuration
  TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
  TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
  TIM_OCInitStructure.TIM_Pulse = 0;
  TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
  TIM_OCInitStructure.TIM_OCIdleState = TIM_OCIdleState_Set;

  // Configure OC1
  servoMapMOSI->ocInit(servoMapMOSI->tim, &TIM_OCInitStructure);
  servoMapMOSI->preloadConfig(servoMapMOSI->tim, TIM_OCPreload_Enable);


  //Enable the timer PWM outputs
  TIM_CtrlPWMOutputs(servoMapMOSI->tim, ENABLE);
  servoMapMOSI->setCompare(servoMapMOSI->tim, 0x00);

  //Enable the timer
  TIM_Cmd(servoMapMOSI->tim, ENABLE);

  DEBUG_PRINT("Init [OK]\n");
  servoSetAngle(SERVO_ANGLE_ZERO);

  isInit = true;
}

bool servoTest(void)
{
  return isInit;
}

void servoSetAngle(uint8_t angle)
{
  // set CCR register
  // Duty% = CCR/ARR*100, so CCR = Duty%/100 * ARR

  double pulse_length_ms = (double)(angle) / 180. * (SERVO_180_PULSE_ms - SERVO_ZERO_PULSE_ms) + SERVO_ZERO_PULSE_ms;
  double pulse_length_s = pulse_length_ms / 1000;
  const uint32_t ccr_val = (uint32_t)(pulse_length_s * SERVO_PWM_PERIOD * SERVO_PWM_FREQUENCY_HZ);
  servoMapMOSI->setCompare(servoMapMOSI->tim, ccr_val);
  DEBUG_PRINT("[SERVO]: Set Angle: %u, set ratio: %" PRId32 "\n", angle, ccr_val);

}

uint8_t saturateAngle(int8_t angle)
{
  if (angle > SERVO_ANGLE_LIMIT || angle < -SERVO_ANGLE_LIMIT) {
//    DEBUG_PRINT("[SERVO]: Servo angle out of range! Capping...\n");
    bool sign = angle > 0;
    return sign ? SERVO_ANGLE_LIMIT + SERVO_ANGLE_ZERO : -SERVO_ANGLE_LIMIT+SERVO_ANGLE_ZERO;
  }
  else {
    return angle + SERVO_ANGLE_ZERO;
  }

}

void servoAngleCallBack(void)
{
  servoSetAngle(saturateAngle(s_servo_angle));
}

static const DeckDriver servo_deck = {
  .vid = 0x00,
  .pid = 0x00,
  .name = "bcServo",

  .usedPeriph = 0,
  .usedGpio = 0,
  .init = servoInit,
  .test = 0,
};

DECK_DRIVER(servo_deck);

PARAM_GROUP_START(servo)

PARAM_ADD(PARAM_UINT8 | PARAM_RONLY, servoInitialized, &isInit)
PARAM_ADD_WITH_CALLBACK(PARAM_UINT8 , servoAngle, &s_servo_angle, &servoAngleCallBack)

PARAM_GROUP_STOP(servo)