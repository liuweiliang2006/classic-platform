/* -------------------------------- Arctic Core ------------------------------
 * Arctic Core - the open source AUTOSAR platform http://arccore.com
 *
 * Copyright (C) 2009  ArcCore AB <contact@arccore.com>
 *
 * This source code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation; See <http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * -------------------------------- Arctic Core ------------------------------*/

/** @reqSettings DEFAULT_SPECIFICATION_REVISION=3.1.5 */

/* ----------------------------[information]----------------------------------*/
/*
 * Author: ?+mahi
 *
 * Part of Release:
 *   3.1.5
 *
 * Description:
 *   Implements the Can Driver module
 *
 * Support:
 *   General                  Have Support
 *   -------------------------------------------
 *   ECUM_TTII_ENABLED            		N
 *   ECUM_DEV_ERROR_DETECT				Y
 *   ECUM_VERSION_INFO_API				Y
 *   ECUM_INCLUDE_DEM					N (controlled by USE_x macro's instead)
 *   ECUM_INCLUDE_NVRAM_MGR				N (controlled by USE_x macro's instead)
 *   ECUM_INLCUDE_DET					N (controlled by USE_x macro's instead)
 *   ECUM_MAIN_FUNCTION_PERDIOD			Y
 *   ECUM_TTII_WKSOURCE					N
 *
 *   Configuration            Have Support
 *   -------------------------------------------
 *   ECUM_SLEEP_ACTIVITY_PERIOD			?
 *   ECUM_CONFIGCONSISTENCY_HASH		N
 *   ECUM_RUN_SELF_REQUEST_PERIOD		?
 *   ECUM_NVRAM_WRITEALL_TIMEOUT		Y
 *   ECUM_DEFAULT_APP_MODE				?
 *
 *
 *   DefaultShutdownTarget
 *   -------------------------------------------
 *   ECUM_DEFAULT_SHUTDOWN_TARGET		N
 *
 *
 * Things to start with:
 * - EcuM2181
 * - EcuM2861 , Watchdog
 * - ComM_EcuM_RunModeIndication()  not called, See Figure 8 (Seems that the ComM does not do much either)
 *
 *
 *
 */

//lint -emacro(904,VALIDATE,VALIDATE_RV,VALIDATE_NO_RV) //904 PC-Lint exception to MISRA 14.7 (validate macros).
/* ----------------------------[includes]------------------------------------*/

#include "Std_Types.h"
#include "EcuM.h"
#include "Modules.h"
#include <string.h>
#include <Os.h>
#include "EcuM_Generated_Types.h"
#include "EcuM_Internals.h"
#include "EcuM_Cbk.h"
#include "SchM_EcuM.h"
#include "MemMap.h"
#include "Mcu.h"
#include "ComStack_Types.h"
#if defined(USE_DET)
#include "Det.h"
#endif
#include "isr.h"
#if defined(USE_NVM)
#include "NvM.h"
#endif
#if defined(USE_RTE)
#include "Rte_Main.h"
#endif
#if defined(USE_SCHM)
#include "SchM.h"
#endif

//#define USE_LDEBUG_PRINTF
#include "debug.h"

/* ----------------------------[private define]------------------------------*/
/* ----------------------------[private macro]-------------------------------*/
/* ----------------------------[private typedef]-----------------------------*/
/* ----------------------------[private function prototypes]-----------------*/
/* ----------------------------[private variables]---------------------------*/

EcuM_GlobalType internal_data;

/* ----------------------------[private functions]---------------------------*/

/* ----------------------------[public functions]----------------------------*/

#if !defined(USE_DET) && (ECUM_DEV_ERROR_DETECT == STD_ON)
#error EcuM configuration error. DET is not enabled when ECUM_DEV_ERROR_DETECT is set
#endif


/**
 * Initialize EcuM.
 */
void EcuM_Init(void) {
	Std_ReturnType status;
	set_current_state(ECUM_STATE_STARTUP_ONE);

	// Initialize drivers that are needed to determine PostBuild configuration
	EcuM_AL_DriverInitZero();

	// Initialize the OS
	InitOS();

	// Setup interrupts
	Os_IsrInit();

	// Determine PostBuild configuration
	internal_data.config = EcuM_DeterminePbConfiguration();

	// TODO: Check consistency of PB configuration

	// Initialize drivers needed before the OS-starts
	EcuM_AL_DriverInitOne(internal_data.config);

	// Determine the reset/wakeup reason
	switch (Mcu_GetResetReason()) {
	case MCU_POWER_ON_RESET:
		EcuM_SetWakeupEvent(ECUM_WKSOURCE_POWER);
		break;
	case MCU_SW_RESET:
		EcuM_SetWakeupEvent(ECUM_WKSOURCE_RESET);
		break;
	case MCU_RESET_UNDEFINED:
		break;
	case MCU_WATCHDOG_RESET:
		EcuM_SetWakeupEvent(ECUM_WKSOURCE_INTERNAL_WDG);
		break;
	default:
		assert(0);
		break;
	}

	// Moved this here because EcuM_SelectShutdownTarget needs us to be initilized.
	internal_data.initiated = TRUE;

	// Set default shutdown target

	status = EcuM_SelectShutdownTarget(
					internal_data.config->EcuMDefaultShutdownTarget,
					internal_data.config->EcuMDefaultSleepMode);/** @req EcuM2181 */
	if (status != E_OK) {
		//TODO: Report error.
	}

	// Set default application mode
	status = EcuM_SelectApplicationMode(
					internal_data.config->EcuMDefaultAppMode);
	if (status != E_OK) {
		//TODO: Report error.
	}

#if defined(USE_COMM) || defined(USE_ECUM_COMM)
	internal_data.run_comm_requests = 0;
#endif
	internal_data.run_requests = 0;
	internal_data.postrun_requests = 0;

	// Start this baby up
	AppModeType appMode;
	status = EcuM_GetApplicationMode(&appMode);
	if (status != E_OK) {
		//TODO: Report error.
	}

	StartOS(appMode); /** @req EcuM2141 */
}


/*
 * The order defined here is found in 3.1.5/EcuM2411
 */
void EcuM_StartupTwo(void)
{
	//TODO:  Validate that we are in state STARTUP_ONE.
#if defined(USE_NVM)
	extern CounterType Os_Arc_OsTickCounter;
	TickType tickTimerStart, tickTimerElapsed;
	static NvM_RequestResultType readAllResult;
	TickType tickTimer;
	StatusType tickTimerStatus;
#endif

	set_current_state(ECUM_STATE_STARTUP_TWO);

	// Initialize the BSW scheduler
#if defined(USE_SCHM)
	SchM_Init();
#endif

#if defined(USE_WDGM)
	if( internal_data.config->EcuMWdgMConfig != NULL ) {
	  WdgM_SetMode(internal_data.config->EcuMWdgMConfig->EcuMWdgMStartupMode);
	}
#endif

	// Initialize drivers that don't need NVRAM data
	EcuM_AL_DriverInitTwo(internal_data.config);

#if defined(USE_NVM)
	// Start timer to wait for NVM job to complete
	tickTimerStart = GetOsTick();
#endif

	// Prepare the system to startup RTE
	// TODO EcuM_OnRTEStartup();
#if defined(USE_RTE)
	Rte_Start();
#endif

#if defined(USE_NVM)
	/* Wait for the NVM job (NvM_ReadAll) to terminate. This assumes that:
	 * - A task runs the memory MainFunctions, e.g. Ea_MainFunction(), Eep_MainFunction()
	 *   are run in a higher priority task that the task that executes this code.
	 */
	do {
		/* Read the multiblock status */
		NvM_GetErrorStatus(0, &readAllResult);
		tickTimerElapsed = OS_TICKS2MS_OS_TICK(GetOsTick() - tickTimerStart);
		/* The timeout EcuMNvramReadAllTimeout is in ms */
	} while( (readAllResult == NVM_REQ_PENDING) && (tickTimerElapsed < internal_data.config->EcuMNvramReadAllTimeout) );
#endif

	// Initialize drivers that need NVRAM data
	EcuM_AL_DriverInitThree(internal_data.config);

	// TODO: Indicate mode change to RTE

	EcuM_enter_run_mode();

}


// Typically called from OS shutdown hook
void EcuM_Shutdown(void) {
	set_current_state(ECUM_STATE_GO_OFF_TWO);

	// Let the last drivers do a nice shutdown
	EcuM_OnGoOffTwo();

	if (internal_data.shutdown_target == ECUM_STATE_OFF) {
		EcuM_AL_SwitchOff();
	} else {
#if (MCU_PERFORM_RESET_API == STD_ON)
		Mcu_PerformReset();
#else
		for(;;)
		{
			;
		}
#endif
	}
}

Std_ReturnType EcuM_GetState(EcuM_StateType* state) {
	VALIDATE_RV(internal_data.initiated, ECUM_GETSTATE_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(state != NULL, ECUM_GETSTATE_ID, ECUM_E_NULL_POINTER, E_NOT_OK);

	*state = internal_data.current_state;

	return E_OK;
}

Std_ReturnType EcuM_SelectApplicationMode(AppModeType appMode) {
	VALIDATE_RV(internal_data.initiated, ECUM_SELECTAPPMODE_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);

	internal_data.app_mode = appMode;

	return E_OK;
}

Std_ReturnType EcuM_GetApplicationMode(AppModeType* appMode) {
	VALIDATE_RV(internal_data.initiated, ECUM_GETAPPMODE_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(appMode != NULL, ECUM_GETAPPMODE_ID, ECUM_E_NULL_POINTER, E_NOT_OK);

	*appMode = internal_data.app_mode;

	return E_OK;
}

Std_ReturnType EcuM_SelectBootTarget(EcuM_BootTargetType target) {
	VALIDATE_RV(internal_data.initiated, ECUM_SELECT_BOOTARGET_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);

	// TODO Do something great here
	(void) target;
	assert(0);

	return E_NOT_OK;
}

Std_ReturnType EcuM_GetBootTarget(EcuM_BootTargetType* target) {
	VALIDATE_RV(internal_data.initiated, ECUM_GET_BOOTARGET_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(target != NULL, ECUM_GET_BOOTARGET_ID, ECUM_E_NULL_POINTER, E_NOT_OK);

	// TODO Return selected boot target here
	(void) target;
	assert(0);

	return E_NOT_OK;
}

Std_ReturnType EcuM_SelectShutdownTarget(EcuM_StateType shutdownTarget,
				uint8 sleepMode) {
	VALIDATE_RV(internal_data.initiated, ECUM_SELECTSHUTDOWNTARGET_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV((shutdownTarget == ECUM_STATE_OFF) || (shutdownTarget == ECUM_STATE_RESET) || (shutdownTarget == ECUM_STATE_SLEEP), ECUM_SELECTSHUTDOWNTARGET_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

#if defined(DEBUG_SELECT_SHUTDOWN_TARGET)
	LDEBUG_PRINTF("EcuM_SelectShutdownTarget(): shutdownTarget=%s, sleepMode=%d\n",
			GetMainStateAsString(shutdownTarget),
			sleepMode);
#endif

	internal_data.shutdown_target = shutdownTarget;
	internal_data.sleep_mode = sleepMode;

	return E_OK;
}

Std_ReturnType EcuM_GetShutdownTarget(EcuM_StateType* shutdownTarget,
				uint8* sleepMode) /** @req EcuM2824 */
{
	VALIDATE_RV(internal_data.initiated, ECUM_GETSHUTDOWNTARGET_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(shutdownTarget != NULL, ECUM_GETSHUTDOWNTARGET_ID, ECUM_E_NULL_POINTER, E_NOT_OK);VALIDATE_RV(sleepMode != NULL, ECUM_GETSHUTDOWNTARGET_ID, ECUM_E_NULL_POINTER, E_NOT_OK);

	*shutdownTarget = internal_data.shutdown_target;
	*sleepMode = internal_data.sleep_mode;

	return E_OK;
}

Std_ReturnType EcuM_RequestRUN(EcuM_UserType user) {
	VALIDATE_RV(internal_data.initiated, ECUM_REQUESTRUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(user < ECUM_USER_ENDMARK, ECUM_REQUESTRUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

	//	LDEBUG_PRINTF("EcuM_RequestRUN(): User %d\n",user);
	internal_data.run_requests |= (uint32) 1 << user;

	return E_OK;
}

Std_ReturnType EcuM_ReleaseRUN(EcuM_UserType user) {
	VALIDATE_RV(internal_data.initiated, ECUM_RELEASERUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(user < ECUM_USER_ENDMARK, ECUM_RELEASERUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

#if defined(DEBUG_RELEASE_RUN)
	LDEBUG_PRINTF("EcuM_ReleaseRUN(): User %d\n",user);
#endif
	internal_data.run_requests &= ~((uint32) 1 << user);

	return E_OK;
}

/**
 *
 */
void EcuM_KillAllRUNRequests(void) {
	/* NOT IMPLEMENTED */
	assert(0);
}

/**
 *
 * @param sources
 */
void EcuM_SetWakeupEvent(EcuM_WakeupSourceType sources) {

	EcuM_WakeupSourceType wkSource;

	DEBUG_ECUM_CALLIN_W_ARG("EcuM_SetWakeupEvent","0x%lx",(uint32)sources);

	/* @req 3.1.5/EcuM2826 The function exists */
	/* @req 3.1.5/EcuM2171 */

	/* @req 4.0.3|.1.5/ECUM2565 Validate at once for pre-defined sources */
	wkSource = (ECUM_WKSOURCE_POWER | ECUM_WKSOURCE_RESET
					| ECUM_WKSOURCE_INTERNAL_RESET | ECUM_WKSOURCE_INTERNAL_WDG
					| ECUM_WKSOURCE_EXTERNAL_WDG);

	/* Sources that should be validated at once */
	wkSource &= sources;

	if (wkSource != 0) {
		/* Move to validated  */
		EcuM_ValidateWakeupEvent(wkSource);
	}

	/* Don't add pre-defined source to PENDING */
	sources ^= wkSource;

	/* @req 3.1.5/EcuM2867 */
#if  ( ECUM_DEV_ERROR_DETECT == STD_ON )
	/* Get user defined sources */
	wkSource = internal_data.config->EcuMSleepModeConfig[internal_data.sleep_mode].EcuMWakeupSourceMask;

	/* Add always validated sources */
	wkSource |= (ECUM_WKSOURCE_POWER|ECUM_WKSOURCE_RESET | ECUM_WKSOURCE_INTERNAL_RESET |
			ECUM_WKSOURCE_INTERNAL_WDG | ECUM_WKSOURCE_EXTERNAL_WDG);

	if( !((sources | wkSource) == wkSource))
	{
		Det_ReportError(MODULE_ID_ECUM, 0, ECUM_VALIDATE_WAKEUP_EVENT_ID, ECUM_E_UNKNOWN_WAKEUP_SOURCE );
		return;
	}
#endif

	/* @req 3.1.5/EcuM1117 */
	internal_data.wakeupEvents |= sources;

	/* @req 3.1.5/EcuM2707 @req 3.1.5/EcuM2709*/
	internal_data.validationTimer = ECUM_VALIDATION_TIMEOUT
					/ ECUM_MAIN_FUNCTION_PERIOD;

}

#if defined(USE_COMM) || defined(USE_ECUM_COMM)
Std_ReturnType EcuM_ComM_RequestRUN(NetworkHandleType channel)
{
	VALIDATE_RV(internal_data.initiated, ECUM_COMM_REQUESTRUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);
	VALIDATE_RV(channel < 32, ECUM_COMM_REQUESTRUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

	internal_data.run_comm_requests |= (uint32)1 << channel;

	return E_OK;
}

Std_ReturnType EcuM_ComM_ReleaseRUN(NetworkHandleType channel)
{
	VALIDATE_RV(internal_data.initiated, ECUM_COMM_RELEASERUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);
	VALIDATE_RV(channel < 32, ECUM_COMM_RELEASERUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

	internal_data.run_comm_requests &= ~((uint32)1 << channel);

	return E_OK;
}

boolean EcuM_ComM_HasRequestedRUN(NetworkHandleType channel)
{
	VALIDATE_RV(internal_data.initiated, ECUM_COMM_HASREQUESTEDRUN_ID, ECUM_E_NOT_INITIATED, FALSE);
	VALIDATE_RV(channel < 32, ECUM_COMM_HASREQUESTEDRUN_ID, ECUM_E_INVALID_PAR, FALSE);

	return (internal_data.run_comm_requests &((uint32)1 << channel)) != 0;
}
#endif

Std_ReturnType EcuM_RequestPOST_RUN(EcuM_UserType user) {
	VALIDATE_RV(internal_data.initiated, ECUM_REQUESTPOSTRUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(user < ECUM_USER_ENDMARK, ECUM_REQUESTPOSTRUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

	internal_data.postrun_requests |= (uint32) 1 << user;

	return E_OK;
}

Std_ReturnType EcuM_ReleasePOST_RUN(EcuM_UserType user) {
	VALIDATE_RV(internal_data.initiated, ECUM_RELEASEPOSTRUN_ID, ECUM_E_NOT_INITIATED, E_NOT_OK);VALIDATE_RV(user < ECUM_USER_ENDMARK, ECUM_RELEASEPOSTRUN_ID, ECUM_E_INVALID_PAR, E_NOT_OK);

	internal_data.postrun_requests &= ~((uint32) 1 << user);

	return E_OK;
}

/*
 * TODO: Don't yet understand the use
 */
void EcuM_ClearWakeupEvent(EcuM_WakeupSourceType source) {
	switch (source) {
	case ECUM_WKSTATUS_NONE:
		/* Seems quite pointless */
		break;
	case ECUM_WKSTATUS_PENDING:
		internal_data.wakeupEvents = 0;
		break;
	case ECUM_WKSTATUS_VALIDATED:
		internal_data.validEvents = 0;
		break;
	case ECUM_WKSTATUS_EXPIRED:
		assert(0);
		// NO SUPPORT
		break;
	default:
		break;
	}
}

/**
 * Get the pending wakeup events.
 *
 * @return
 */
EcuM_WakeupSourceType EcuM_GetPendingWakeupEvents(void) {
	/* @req 3.1.5/EcuM2827 API
	 * @req 3.1.5/EcuM2172 Callable from interrupt context
	 * */

	/* @req 3.1.5/EcuM1156 */
	return internal_data.wakeupEvents;

}

EcuM_WakeupSourceType EcuM_GetValidatedWakeupEvents(void) {

	return internal_data.validEvents;
}

EcuM_WakeupStatusType EcuM_GetStatusOfWakeupSource(
				EcuM_WakeupSourceType sources) {
	assert(0);
	// NO SUPPORT
	return 0;
}

/**
 *
 * @param sources
 */
void EcuM_ValidateWakeupEvent(EcuM_WakeupSourceType sources) {

	/* !req 3.1.5/EcuM2344 */
	/* !req 3.1.5/EcuM2645 */
	/* !req 3.1.5/EcuM2868 */
	/* !req 3.1.5/EcuM2345 */

	internal_data.validEvents |= sources;
	/*
	#if defined(USE_COMM)
	if( internal_data.config->)
	ComM_EcuM_WakeUpIndication()
	#endif
	 */
	/* !req 3.1.5/EcuM2790 */
	/* !req 3.1.5/EcuM2791 */

}

#if defined(USE_NVM)
void EcuM_CB_NfyNvMJobEnd(uint8 ServiceId, NvM_RequestResultType JobResult)
{

}
#endif

