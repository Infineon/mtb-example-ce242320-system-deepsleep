/******************************************************************************
* File Name:   main.c
*
* Description: This is the Main application for PSOC™ Control C3M/P8 multicore deep
*              sleep demonstration for ModusToolbox.
*
* Related Document: See README.md
*
*
********************************************************************************
* (c) 2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/**
 * @brief Header Files
 *
 * Includes necessary PDL (Peripheral Driver Library) headers for hardware access,
 * BSP (Board Support Package) for board-specific initialization, and retarget-IO
 * for UART-based printf redirection.
 */
#include "cy_pdl.h"        // Core PDL functions and types
#include "cybsp.h"         // Board-specific initialization
#include "cy_retarget_io.h" // UART retargeting for printf
#include "stdint.h"

/**
 * @brief Macros
 *
 * Defines memory addresses for PPCA core images and shared variables,
 * as well as offsets for image sizing.
 */
#define CORE0_IMAGE_ADDRESS    CYMEM_CM33_0_S_m33s_ppca0_nvm_C_S_START    //  0x12030000  // Flash address for PPCA Core 0 image
#define CORE1_IMAGE_ADDRESS    CYMEM_CM33_0_S_m33s_ppca1_nvm_C_S_START    //  0x12038000  // Flash address for PPCA Core 1 image
#define PPCA0_IMAGE_SIZE       CYMEM_CM33_0_S_ppca0_code_SIZE  // Size of PPCA Core 0 image
#define PPCA1_IMAGE_SIZE       CYMEM_CM33_0_S_ppca1_code_SIZE  // Size of PPCA Core 1 image

// Shared memory addresses for inter-core communication
#define PPCA_M1_VAR_ADDRESS   0x53020400  // PPCA Core 0 shared variable
#define PPCA_M3_VAR_ADDRESS   0x53040800  // PPCA Core 1 shared variable

/**
 * @brief Global Variables
 *
 * UART context and HAL object for debug output redirection.
 */
static cy_stc_scb_uart_context_t DEBUG_UART_context;  // UART peripheral context
static mtb_hal_uart_t DEBUG_UART_hal_obj;            // HAL UART object for retarget-IO

/**
 * @brief Function Prototypes
 */
void init_ppca_peripheral(void);  // Initializes PPCA subsystem peripherals

// MCWDT Configuration
// Multi-Counter Watchdog Timer setup for wake-up interrupts during deep sleep
#define MCWDT0_HW MCWDT_STRUCT0  // Hardware instance

#if !defined MCWDT0_HW
#define MCWDT0_HW MCWDT_STRUCT0
#endif

#define MCWDT_INT           srss_interrupt_mcwdt_0_IRQn  // Interrupt number
#define TWO_CYCLES_TIME     (62u)                        // Delay cycles for enable

/**
 * @brief WDT interrupt configuration structure
 * Configures the interrupt for MCWDT counter 2 to wake the system.
 */
const cy_stc_sysint_t MCWDT_IRQ_cfg = {
    .intrSrc = (IRQn_Type)MCWDT_INT,
    .intrPriority = 4
};

/**
 * @brief MCWDT configuration
 * Sets up counters: C0/C1 disabled, C2 in interrupt mode for periodic wake-up.
 */
const cy_stc_mcwdt_config_t MCWDT0_config = {
    .c0Match = 32768U,                    // Match value for counter 0
    .c1Match = 32768U,                    // Match value for counter 1
    .c0Mode = CY_MCWDT_MODE_NONE,         // Counter 0 disabled
    .c1Mode = CY_MCWDT_MODE_NONE,         // Counter 1 disabled
    .c2ToggleBit = 18U,                   // Toggle bit for counter 2 (doubled timing - 8 seconds)
    .c2Mode = CY_MCWDT_MODE_INT,          // Counter 2 generates interrupts
    .c0ClearOnMatch = false,
    .c1ClearOnMatch = false,
    .c0c1Cascade = false,
    .c1c2Cascade = false,
    .c0LowerLimitMode = CY_MCWDT_LOWER_LIMIT_MODE_NOTHING,
    .c0LowerLimit = 0U,
    .c1LowerLimitMode = CY_MCWDT_LOWER_LIMIT_MODE_NOTHING,
    .c1LowerLimit = 0U,
};

void McwdtInterruptHandler()
{
    // Clear the MCWDT interrupt for counter 2
    Cy_MCWDT_ClearInterrupt(MCWDT0_HW, CY_MCWDT_CTR2);
}

void Configure_MCWDT_Reset(void)
{
    // Initialize the system interrupt for MCWDT
    Cy_SysInt_Init(&MCWDT_IRQ_cfg, McwdtInterruptHandler);

    // Initialize the MCWDT with the given configuration
    Cy_MCWDT_Init(MCWDT0_HW, &MCWDT0_config);
    // Enable the interrupt in NVIC
    NVIC_EnableIRQ(MCWDT_IRQ_cfg.intrSrc);
    // Set the interrupt mask for counter 2
    Cy_MCWDT_SetInterruptMask(MCWDT0_HW, CY_MCWDT_CTR2);
    // Enable MCWDT counter 2 with a delay of two cycles
    Cy_MCWDT_Enable(MCWDT0_HW, CY_MCWDT_CTR2, TWO_CYCLES_TIME);
}

/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for CM33 CPU. It demonstrates the multicore deep sleep mechanism
* across the main core and PPCA cores. The flow includes:
*    1. System initialization and multicore boot
*    2. Coordinated deep sleep request and readiness monitoring
*    3. Synchronized deep sleep entry when all cores are ready
*    4. Wake-up, reinitialization, and cycle repetition
*    5. Continuous monitoring of core states and system activity
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/

int main(void)
{
    cy_rslt_t result;

    // === Board and Peripheral Initialization ===
    // Initialize the board support package (BSP) for hardware setup
    result = cybsp_init();
    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(0);
    }

    // Initialize UART for debug output via retarget-IO
    Cy_SCB_UART_Init(DEBUG_UART_HW, &DEBUG_UART_config, &DEBUG_UART_context);
    Cy_SCB_UART_Enable(DEBUG_UART_HW);
    result = mtb_hal_uart_setup(&DEBUG_UART_hal_obj, &DEBUG_UART_hal_config, &DEBUG_UART_context, NULL);
    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(0);
    }
    result = cy_retarget_io_init(&DEBUG_UART_hal_obj);
    if (result != CY_RSLT_SUCCESS) {
        CY_ASSERT(0);
    }

    // === System Initialization Phase ===
    printf("\x1b[2J\x1b[;H");
    printf("************************************************************\r\n");
    printf("PSOC Control C3M/P8: System Deep Sleep application\r\n");
    printf("************************************************************\r\n\n");

    // Initialize PWM for system activity indication
    cy_en_tcpwm_status_t pwm_status = Cy_TCPWM_PWM_Init(PWM_HW, PWM_NUM, &PWM_config);
    if (pwm_status != CY_TCPWM_SUCCESS) {
        CY_ASSERT(0);
    }
    Cy_TCPWM_PWM_Enable(PWM_HW, PWM_NUM);
    __enable_irq();
    Cy_TCPWM_TriggerStart_Single(PWM_HW, PWM_NUM);
    printf("PWM initialized and started\r\n");

    // Initialize PPCA subsystem for multicore communication
    init_ppca_peripheral();
    printf("PPCA subsystem initialized\r\n");

    // === Multicore Boot Phase ===
    // Boot PPCA cores with their respective firmware images
    Cy_System_Init_CPU0((void*)CORE0_IMAGE_ADDRESS, PPCA0_IMAGE_SIZE);
    Cy_System_Init_CPU1((void*)CORE1_IMAGE_ADDRESS, PPCA1_IMAGE_SIZE);
    printf("PPCA CM33 cores booted\r\n");

    // === Shared Memory Setup ===
    // Set up pointers to shared memory locations for inter-core communication
    uint32_t *ppca_core0_var = (uint32_t *)PPCA_M1_VAR_ADDRESS;
    uint32_t *ppca_core1_var = (uint32_t *)PPCA_M3_VAR_ADDRESS;

    // === Deep Sleep Preparation ===
    cy_stc_syspm_ppca_images_t coreImages = {
        .flash_address0 = (void*)CORE0_IMAGE_ADDRESS,
        .flash_address1 = (void*)CORE1_IMAGE_ADDRESS,
        .image_size0 = PPCA0_IMAGE_SIZE,
        .image_size1 = PPCA1_IMAGE_SIZE,
    };

    // Request initial deep sleep for PPCA cores
    cy_en_syspm_status_t syspm_status = Cy_SysPm_PPCA_RequestDeepSleep(&coreImages);
    printf("Initial deep sleep request sent: 0x%x\r\n", syspm_status);

    // Set system to deep sleep mode
    Cy_SysPm_SetDeepSleepMode(CY_SYSPM_MODE_DEEPSLEEP);

    // Configure MCWDT for wake-up/reset
    Configure_MCWDT_Reset();

    printf("System ready for deep sleep cycles\r\n\n");

    // === Main Demonstration Loop ===
    uint32_t cycle_count = 0;
    while (1) {
        cycle_count++;

        // Phase 1: Check system-wide deep sleep readiness
        bool ready = Cy_SysPm_PPCA_IsDeepSleepReady();
        printf("Cycle %u: Deep sleep ready = %u\r\n", (unsigned int)cycle_count, (unsigned int)ready);

        // Phase 2: Enter deep sleep if all cores are ready
        if (ready) {
            printf(">>> Entering SYSTEM DEEP SLEEP (Cycle %u)...\r\n", (unsigned int)cycle_count);
            Cy_SysLib_Delay(10);  // Brief delay before sleep

            // Enter deep sleep and wait for interrupt (e.g., from MCWDT)
            syspm_status = Cy_SysPm_CpuEnterDeepSleep(CY_SYSPM_WAIT_FOR_INTERRUPT);
            printf("<<< Woke from DEEP SLEEP, status: 0x%X\r\n", syspm_status);

            // Post-wake reinitialization
            init_ppca_peripheral();  // Reinitialize PPCA after wake
            Cy_TCPWM_TriggerStart_Single(PWM_HW, PWM_NUM);  // Restart PWM
            Cy_SysPm_PPCA_ClearDeepSleepInterrupt();  // Clear interrupt flag

            // Request next deep sleep cycle
            syspm_status = Cy_SysPm_PPCA_RequestDeepSleep(&coreImages);
            printf("Next deep sleep cycle requested\r\n");
        } else {
            // If not ready, wait and try again in next cycle
            printf("Waiting for PPCA cores to become ready...\r\n");
        }

        // Phase 3: Monitor multicore activity via shared memory
        uint32_t core0_val = *ppca_core0_var;
        uint32_t core1_val = *ppca_core1_var;
        printf("PPCA Core0: %u, Core1: %u\r\n", (unsigned int)core0_val, (unsigned int)core1_val);

        // Phase 4: Indicate active operation (LED blink)
        Cy_GPIO_Inv(CYBSP_USER_LED3_PORT, CYBSP_USER_LED3_PIN);

        // Phase 5: Delay before next cycle
        Cy_SysLib_Delay(1000);
        printf("\r\n");
    }

}


void init_ppca_peripheral(void)
{
    /* Initialize and enable PPCA subsystem configuration */
    Cy_PPCA_CNFG_Init(PPCA_CNFG_HW, &PPCA_CNFG_config);
    Cy_PPCA_Enable(PPCA_CNFG_HW);

    /* Enable the EPU (Event Processing Unit) block */
    Cy_PPCA_EPU_Enable(EPU_BLK_HW);

    /* Configure EPU T2 and combiner for GPIO2 (LED1 driven by Core 0 SW events) */
    Cy_PPCA_EPU_PU_T2_Configure(PU_T2_3_GPIO2_HW, PU_T2_3_GPIO2_INDEX, &PU_T2_3_GPIO2_put2_config);
    Cy_PPCA_EPU_PU_T2_Enable(PU_T2_3_GPIO2_HW, PU_T2_3_GPIO2_INDEX, PU_T2_3_GPIO2_ENABLE_MODE);
    Cy_PPCA_EPU_Combo_Configure(COMBINER_IO2_HW, COMBINER_IO2_INDEX, &COMBINER_IO2_combo_config);

    /* Configure EPU T2 and combiner for GPIO1 (LED2 driven by Core 1 SW events) */
    Cy_PPCA_EPU_PU_T2_Configure(PU_T2_2_GPIO1_HW, PU_T2_2_GPIO1_INDEX, &PU_T2_2_GPIO1_put2_config);
    Cy_PPCA_EPU_PU_T2_Enable(PU_T2_2_GPIO1_HW, PU_T2_2_GPIO1_INDEX, PU_T2_2_GPIO1_ENABLE_MODE);
    Cy_PPCA_EPU_Combo_Configure(COMBINER_IO1_HW, COMBINER_IO1_INDEX, &COMBINER_IO1_combo_config);
}