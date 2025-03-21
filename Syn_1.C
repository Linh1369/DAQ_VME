#include <CAENVMElib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>  // for usleep

#define BASE_ADC 0x10000000  // Replace with actual base address
#define BASE_TDC 0x20000000  // Replace with actual base address

#define STATUS_REG 0x100E    // Status register offset
#define BUSY_MASK  0x4       // Bit 2 = BUSY

CAENVMEDeviceHandle handle;

// Read status register and check BUSY bit
int is_module_busy(uint32_t baseAddr) {
    uint16_t status = 0;
    int ret = CAENVME_ReadCycle(handle, baseAddr + STATUS_REG, &status, cvA32_U_DATA, cvD16);
    if (ret != cvSuccess) {
        printf("Error reading status from module at 0x%08X\n", baseAddr);
        return 1; // Treat error as busy to be safe
    }
    return (status & BUSY_MASK) ? 1 : 0;
}

// Wait until both V775 and V1785 are not busy
void wait_for_modules_ready() {
    while (1) {
        int adc_busy = is_module_busy(BASE_ADC);
        int tdc_busy = is_module_busy(BASE_TDC);

        if (!adc_busy && !tdc_busy) {
            break; // Both are ready
        }
        usleep(10); // Wait 10 µs to avoid tight loop
    }
}

// Dummy read function — replace with real readout
void read_event_data() {
    printf("Reading ADC and TDC event data...\n");
    // Read buffers, clear data-ready bits, etc.
}

int main() {
    // Initialize the VME controller
    if (CAENVME_Init(cvV2718, 0, 0, &handle) != cvSuccess) {
        printf("Failed to initialize VME device.\n");
        return 1;
    }

    printf("DAQ started. Waiting for modules...\n");

    while (1) {
        wait_for_modules_ready();

        // (Optional) Send software trigger if needed
        // e.g., CAENVME_WriteCycle(...)

        // Read event data
        read_event_data();

        // Insert condition to break loop or add event counting logic
    }

    CAENVME_End(handle);
    return 0;
}
