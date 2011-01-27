/*
 * Configuration of module EcuM (EcuM_Cfg.c)
 *
 * Created by: 
 * Configured for (MCU): STM32_F107
 *
 * Module vendor: ArcCore
 * Module version: 2.0.1
 *
 * 
 * Generated by Arctic Studio (http://arccore.com) 
 *           on Fri Jan 21 11:28:13 CET 2011
 */



#include "EcuM.h"

EcuM_ConfigType EcuMConfig =
{
	.EcuMDefaultShutdownTarget = ECUM_STATE_RESET,
	.EcuMDefaultShutdownMode = 0, // Don't care
	.EcuMDefaultAppMode = OSDEFAULTAPPMODE,
	.EcuMNvramReadAllTimeout = ECUM_NVRAM_READALL_TIMEOUT,
	.EcuMNvramWriteAllTimeout = ECUM_NVRAM_WRITEALL_TIMEOUT,
	.EcuMRunMinimumDuration = ECUM_NVRAM_MIN_RUN_DURATION,

#if defined(USE_MCU)
        .McuConfig = McuConfigData,
#endif
#if defined(USE_PORT)
        .PortConfig = &PortConfigData,
#endif
#if defined(USE_CAN)
        .CanConfig = &CanConfigData,
#endif
#if defined(USE_CANIF)
        .CanIfConfig = &CanIf_Config,
#endif
#if defined(USE_CANSM)
        .CanSMConfig = &CanSM_Config,
#endif
#if defined(USE_CANNM)
        .CanNmConfig = &CanNm_Config,
#endif
#if defined(USE_COM)
        .ComConfig = &ComConfiguration,
#endif
#if defined(USE_COMM)
        .ComMConfig = &ComM_Config,
#endif
#if defined(USE_NM)
        .NmConfig = &Nm_Config,
#endif
#if defined(USE_PDUR)
        .PduRConfig = &PduR_Config,
#endif
#if defined(USE_DMA)
        .DmaConfig = DmaConfig,
#endif
#if defined(USE_ADC)
        .AdcConfig = AdcConfig,
#endif
#if defined(USE_PWM)
        .PwmConfig = &PwmConfig,
#endif
#if defined(USE_GPT)
        .GptConfig = GptConfigData,
#endif
#if defined(USE_FLS)
	.FlashConfig = FlsConfigSet,
#endif
};
