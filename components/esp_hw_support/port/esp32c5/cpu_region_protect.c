/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "soc/soc.h"
#include "esp_cpu.h"
#include "esp_fault.h"
#include "esp32c5/rom/rom_layout.h"

#ifdef BOOTLOADER_BUILD
// Without L bit set
#define CONDITIONAL_NONE        0x0
#define CONDITIONAL_RX          PMP_R | PMP_X
#define CONDITIONAL_RW          PMP_R | PMP_W
#define CONDITIONAL_RWX         PMP_R | PMP_W | PMP_X
#else
// With L bit set
#define CONDITIONAL_NONE        NONE
#define CONDITIONAL_RX          RX
#define CONDITIONAL_RW          RW
#define CONDITIONAL_RWX         RWX
#endif

#define ALIGN_UP_TO_MMU_PAGE_SIZE(addr) (((addr) + (SOC_MMU_PAGE_SIZE) - 1) & ~((SOC_MMU_PAGE_SIZE) - 1))
#define ALIGN_DOWN_TO_MMU_PAGE_SIZE(addr)  ((addr) & ~((SOC_MMU_PAGE_SIZE) - 1))

static void esp_cpu_configure_invalid_regions(void)
{
    const unsigned PMA_NONE                            = PMA_L | PMA_EN;
    __attribute__((unused)) const unsigned PMA_RW      = PMA_L | PMA_EN | PMA_R | PMA_W;
    __attribute__((unused)) const unsigned PMA_RX      = PMA_L | PMA_EN | PMA_R | PMA_X;
    __attribute__((unused)) const unsigned PMA_RWX     = PMA_L | PMA_EN | PMA_R | PMA_W | PMA_X;

    // 0. Gap at bottom of address space
    PMA_ENTRY_SET_NAPOT(0, 0, SOC_CPU_SUBSYSTEM_LOW, PMA_NAPOT | PMA_NONE);

    // 1. Gap between debug region & IROM
    PMA_ENTRY_SET_TOR(1, SOC_CPU_SUBSYSTEM_HIGH, PMA_NONE);
    PMA_ENTRY_SET_TOR(2, SOC_IROM_MASK_LOW, PMA_TOR | PMA_NONE);

    // 2. ROM has configured the ROM region to be cacheable, so we just need to lock the configuration
    PMA_ENTRY_SET_TOR(3, SOC_IROM_MASK_LOW, PMA_NONE);
    PMA_ENTRY_SET_TOR(4, SOC_DROM_MASK_HIGH, PMA_TOR | PMA_RX);

    // 3. Gap between ROM & RAM
    PMA_ENTRY_SET_TOR(5, SOC_DROM_MASK_HIGH, PMA_NONE);
    PMA_ENTRY_SET_TOR(6, SOC_IRAM_LOW, PMA_TOR | PMA_NONE);

    // 4. Gap between DRAM and I_Cache
    PMA_ENTRY_SET_TOR(7, SOC_IRAM_HIGH, PMA_NONE);
    PMA_ENTRY_SET_TOR(8, SOC_IROM_LOW, PMA_TOR | PMA_NONE);

    // 5. ROM has configured the MSPI region with RX permission, we should add W attribute for psram and lock the configuration
    // This function sets invalid regions but this is a valid memory region configuration that could have
    // been configured using PMP as well, but due to insufficient PMP entries we are configuring this using PMA.
    // This entry is also required to be set using PMA because the region needs to be configured as cacheable.
    PMA_ENTRY_SET_NAPOT(9, SOC_IROM_LOW, (SOC_IROM_HIGH - SOC_IROM_LOW), PMA_NAPOT | PMA_RWX);

    // 6. Gap between D_Cache & LP_RAM
    PMA_ENTRY_SET_TOR(10, SOC_DROM_HIGH, PMA_NONE);
    PMA_ENTRY_SET_TOR(11, SOC_RTC_IRAM_LOW, PMA_TOR | PMA_NONE);

    // 7. Gap between LP memory & peripheral addresses
    PMA_ENTRY_SET_TOR(12, SOC_RTC_IRAM_HIGH, PMA_NONE);
    PMA_ENTRY_SET_TOR(13, SOC_PERIPHERAL_LOW, PMA_TOR | PMA_NONE);

    // 8. End of address space
    PMA_ENTRY_SET_TOR(14, SOC_PERIPHERAL_HIGH, PMA_NONE);

    PMA_ENTRY_CFG_RESET(15);
    PMA_ENTRY_SET_TOR(15, UINT32_MAX, PMA_TOR | PMA_NONE);
}

void esp_cpu_configure_region_protection(void)
{
    /* Notes on implementation:
     *
     * 1) Note: ESP32-C5 CPU support overlapping PMP regions
     *
     * 2) ESP32-C5 supports 16 PMA regions so we use this feature to block all the invalid address ranges
     *
     * 3) We use combination of NAPOT (Naturally Aligned Power Of Two) and TOR (top of range)
     * entries to map all the valid address space, bottom to top. This leaves us with some extra PMP entries
     * which can be used to provide more granular access
     *
     * 4) Entries are grouped in order with some static asserts to try and verify everything is
     * correct.
     */

    /* There are 4 configuration scenarios for SRAM
     *
     * 1. Bootloader build:
     *    - We cannot set the lock bit as we need to reconfigure it again for the application.
     *      We configure PMP to cover entire valid IRAM and DRAM range.
     *
     * 2. Application build with CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT enabled
     *    - We split the SRAM into IRAM and DRAM such that IRAM region cannot be written to
     *      and DRAM region cannot be executed. We use _iram_text_end and _data_start markers to set the boundaries.
     *      We also lock these entries so the R/W/X permissions are enforced even for machine mode
     *
     * 3. Application build with CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT disabled
     *    - The IRAM-DRAM split is not enabled so we just need to ensure that access to only valid address ranges are successful
     *      so for that we set PMP to cover entire valid IRAM and DRAM region.
     *      We also lock these entries so the R/W/X permissions are enforced even for machine mode
     *
     * 4. CPU is in OCD debug mode
     *    - The IRAM-DRAM split is not enabled so that OpenOCD can write and execute from IRAM.
     *      We set PMP to cover entire valid IRAM and DRAM region.
     *      We also lock these entries so the R/W/X permissions are enforced even for machine mode
     */
    const unsigned NONE    = PMP_L;
    __attribute__((unused)) const unsigned R       = PMP_L | PMP_R;
    __attribute__((unused)) const unsigned RW      = PMP_L | PMP_R | PMP_W;
    __attribute__((unused)) const unsigned RX      = PMP_L | PMP_R | PMP_X;
    __attribute__((unused)) const unsigned RWX     = PMP_L | PMP_R | PMP_W | PMP_X;

    //
    // Configure all the invalid address regions using PMA
    //
    esp_cpu_configure_invalid_regions();

    //
    // Configure all the valid address regions using PMP
    //

    // 1. CPU Subsystem region - contains interrupt config registers
    const uint32_t pmpaddr0 = PMPADDR_NAPOT(SOC_CPU_SUBSYSTEM_LOW, SOC_CPU_SUBSYSTEM_HIGH);
    PMP_ENTRY_SET(0, pmpaddr0, PMP_NAPOT | RWX);
    _Static_assert(SOC_CPU_SUBSYSTEM_LOW < SOC_CPU_SUBSYSTEM_HIGH, "Invalid CPU subsystem region");

    // 2. I/D-ROM
    const uint32_t drom_start = (uint32_t) (ets_rom_layout_p->drom_start);
    if ((drom_start & (SOC_CPU_PMP_REGION_GRANULARITY - 1)) == 0) {
        // We can skip configuring the PMP entry for the [SOC_IROM_MASK_LOW - drom_start]
        // region as RX, as we already have configured a PMA entry with RX permissions for the
        // [SOC_IROM_MASK_LOW - SOC_DROM_MASK_HIGH] region that helps us to also configure
        // the region as cacheable. Thus, we save on one PMP entry.
        PMP_ENTRY_SET(1, drom_start, NONE);
        PMP_ENTRY_SET(2, SOC_DROM_MASK_HIGH, PMP_TOR | R);
    } else {
        PMP_ENTRY_SET(1, SOC_IROM_MASK_LOW, NONE);
        PMP_ENTRY_SET(2, SOC_IROM_MASK_HIGH, PMP_TOR | RX);
        _Static_assert(SOC_IROM_MASK_LOW < SOC_IROM_MASK_HIGH, "Invalid I/D-ROM region");
    }

    // 3. IRAM and DRAM
    if (esp_cpu_dbgr_is_attached()) {
        // Anti-FI check that cpu is really in ocd mode
        ESP_FAULT_ASSERT(esp_cpu_dbgr_is_attached());

        PMP_ENTRY_SET(3, SOC_IRAM_LOW, NONE);
        PMP_ENTRY_SET(4, SOC_IRAM_HIGH, PMP_TOR | RWX);
        _Static_assert(SOC_IRAM_LOW < SOC_IRAM_HIGH, "Invalid RAM region");
    } else {
#if CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT && !BOOTLOADER_BUILD
        extern int _iram_text_end;
        /* Reset the corresponding PMP config because PMP_ENTRY_SET only sets the given bits
         * Bootloader might have given extra permissions and those won't be cleared
         */
        PMP_ENTRY_CFG_RESET(3);
        PMP_ENTRY_CFG_RESET(4);
        PMP_ENTRY_CFG_RESET(5);
        PMP_ENTRY_SET(3, SOC_IRAM_LOW, NONE);
        PMP_ENTRY_SET(4, (int)&_iram_text_end, PMP_TOR | RX);
        PMP_ENTRY_SET(5, SOC_DRAM_HIGH, PMP_TOR | RW);
#else
        PMP_ENTRY_SET(3, SOC_IRAM_LOW, CONDITIONAL_NONE);
        PMP_ENTRY_SET(4, SOC_IRAM_HIGH, PMP_TOR | CONDITIONAL_RWX);
        _Static_assert(SOC_IRAM_LOW < SOC_IRAM_HIGH, "Invalid RAM region");
#endif
    }

    // 4. I_Cache / D_Cache (flash)
#if CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT && !BOOTLOADER_BUILD
    extern int _instruction_reserved_end;
    extern int _rodata_reserved_end;

    const uint32_t irom_resv_end = ALIGN_UP_TO_MMU_PAGE_SIZE((uint32_t)(&_instruction_reserved_end));
    const uint32_t drom_resv_end = ALIGN_UP_TO_MMU_PAGE_SIZE((uint32_t)(&_rodata_reserved_end));

    PMP_ENTRY_CFG_RESET(6);
    PMP_ENTRY_CFG_RESET(7);
    PMP_ENTRY_CFG_RESET(8);
    PMP_ENTRY_SET(6, SOC_IROM_LOW, NONE);
    PMP_ENTRY_SET(7, irom_resv_end, PMP_TOR | RX);
    PMP_ENTRY_SET(8, drom_resv_end, PMP_TOR | R);
#else
    const uint32_t pmpaddr6 = PMPADDR_NAPOT(SOC_IROM_LOW, SOC_IROM_HIGH);
    // Add the W attribute in the case of PSRAM
    PMP_ENTRY_SET(6, pmpaddr6, PMP_NAPOT | CONDITIONAL_RWX);
    _Static_assert(SOC_IROM_LOW < SOC_IROM_HIGH, "Invalid I/D_Cache region");
#endif

    // 5. LP memory
#if CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT && !BOOTLOADER_BUILD
    extern int _rtc_text_start;
    extern int _rtc_text_end;
    /* Reset the corresponding PMP config because PMP_ENTRY_SET only sets the given bits
     * Bootloader might have given extra permissions and those won't be cleared
     */
    PMP_ENTRY_CFG_RESET(9);
    PMP_ENTRY_CFG_RESET(10);
    PMP_ENTRY_CFG_RESET(11);
    PMP_ENTRY_CFG_RESET(12);
    PMP_ENTRY_SET(9, SOC_RTC_IRAM_LOW, NONE);

    // First part of LP mem is reserved for ULP coprocessor
    PMP_ENTRY_SET(10, (int)&_rtc_text_start, PMP_TOR | RW);

    PMP_ENTRY_SET(11, (int)&_rtc_text_end, PMP_TOR | RX);
    PMP_ENTRY_SET(12, SOC_RTC_IRAM_HIGH, PMP_TOR | RW);
#else
    const uint32_t pmpaddr9 = PMPADDR_NAPOT(SOC_RTC_IRAM_LOW, SOC_RTC_IRAM_HIGH);
    PMP_ENTRY_SET(9, pmpaddr9, PMP_NAPOT | CONDITIONAL_RWX);
    _Static_assert(SOC_RTC_IRAM_LOW < SOC_RTC_IRAM_HIGH, "Invalid RTC IRAM region");
#endif

    // 6. Peripheral addresses
    const uint32_t pmpaddr13 = PMPADDR_NAPOT(SOC_PERIPHERAL_LOW, SOC_PERIPHERAL_HIGH);
    PMP_ENTRY_SET(13, pmpaddr13, PMP_NAPOT | RW);
    _Static_assert(SOC_PERIPHERAL_LOW < SOC_PERIPHERAL_HIGH, "Invalid peripheral region");
}
