/******************************************************************************
* File Name:   main.c
*
* Description: This is the source code for the System Deep Sleep demostration
*              for ModusToolbox.
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
 * configuration headers for board-specific settings, and standard I/O for debugging.
 */
#include "cy_pdl.h"        // Core PDL functions and types
#include "cycfg.h"         // Board configuration
#include <stdio.h>         // Standard I/O functions

/**
 * @brief Macros
 *
 * Defines timing delays and shared memory addresses for inter-core communication.
 */
#define LOOP_DELAY_MS      200   // Base delay between operations
#define DEEP_SLEEP_DELAY_MS 500  // Delay for deep sleep signaling

/* Shared memory addresses for communication with main CM33 core */
#define PPCA_M1_VAR_ADDRESS 0x20000400  // PPCA Core 0 shared variable
#define PPCA_M3_VAR_ADDRESS 0x20000800  // PPCA Core 1 shared variable (this core)

/**
 * @brief Function Prototypes
 */
void enter_deep_sleep(void);  // Function to handle deep sleep entry


/*******************************************************************************
* Function Name: main
********************************************************************************
* Summary:
* This is the main function for PPCA Core 1 in the System Deep Sleep example.
* It drives LED2 via EPU software events, updates shared memory for the main
* core to monitor, and enters coordinated deep sleep when ready:
*    1. Generate EPU software events to toggle LED2 (500 ms on/off)
*    2. Increment shared memory counter for main core activity monitoring
*    3. Enter deep sleep when the counter reaches the readiness threshold
*    4. Reset counter and repeat after waking up
*
* Parameters:
*  void
*
* Return:
*  int
*
*******************************************************************************/
/**
 * @brief Main function for PPCA Core 1
 *
 * This function implements the main application flow for PPCA Core 1:
 *    1. Initialize shared memory communication
 *    2. Signal readiness to main core
 *    3. Generate periodic events for LED control
 *    4. Monitor for deep sleep requests from main core
 *    5. Enter coordinated deep sleep when requested
 *    6. Wake up and continue operation
 *
 * @param void
 * @return int (unreachable)
 */
int main(void)
{
    // === Initialization Phase ===
    // Set up shared memory pointer for communication with main CM33 core
    uint32_t *shared_var = (uint32_t *)PPCA_M3_VAR_ADDRESS;

    // Initialize shared variable to indicate core is starting
    *shared_var = 0;

    // Brief delay to allow main core to initialize
    Cy_SysLib_Delay(LOOP_DELAY_MS);

    // === Main Operation Loop ===
    // Infinite loop demonstrating multicore coordination and deep sleep
    uint32_t cycle_count = 0;
    while(1)
    {
        cycle_count++;

        // Phase 1: Generate software events for LED control
        // Generate event 0 to toggle LED (off state)
        Cy_PPCA_EPU_PU_T2_Generate_SW_Event(PU_T2_2_GPIO1_HW, PU_T2_2_GPIO1_INDEX, 0);
        Cy_SysLib_Delay(DEEP_SLEEP_DELAY_MS);

        // Generate event 1 to toggle LED (on state)
        Cy_PPCA_EPU_PU_T2_Generate_SW_Event(PU_T2_2_GPIO1_HW, PU_T2_2_GPIO1_INDEX, 1);
        Cy_SysLib_Delay(DEEP_SLEEP_DELAY_MS);

        // Phase 2: Update shared memory for main core monitoring
        // Increment shared variable to show activity
        *shared_var += 2;

        // Phase 3: Check for deep sleep coordination
        // Main core will set shared variable to specific value to request deep sleep
        if(*shared_var >= 10)
        {
            // Signal readiness for deep sleep by setting flag value
            *shared_var = 100;  // Special value indicating ready for deep sleep

            // Brief delay before entering deep sleep
            Cy_SysLib_Delay(LOOP_DELAY_MS);

            // Enter deep sleep - coordinated with main core
            enter_deep_sleep();

            // === Wake-up Phase ===
            // Reset shared variable after wake-up
            *shared_var = 0;
            cycle_count = 0;  // Reset cycle counter
        }

        // Phase 4: Continue normal operation
        // Delay before next cycle
        Cy_SysLib_Delay(LOOP_DELAY_MS);
    }

}

/**
 * @brief Enter deep sleep mode
 *
 * Configures the core to enter deep sleep and waits for wake-up interrupt.
 * This function coordinates with the main CM33 core for system-wide deep sleep.
 */
void enter_deep_sleep(void)
{
    // Configure System Control Register for deep sleep
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

    // Execute Wait For Interrupt - core will sleep until woken by main core
    __WFI();
}

