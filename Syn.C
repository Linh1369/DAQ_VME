#include <CAENVMElib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>  // for usleep

#define BASE_ADC 0x10000000  // Example base address of V1785
#define BASE_TDC 0x20000000  // Example base address of V775

#define STATUS_REG 0x100E    // Status register offset
#define BUSY_MASK 0x4        // Bit 2 = BUSY

CAENVMEDeviceHandle handle;

// Function to check if a module is busy
int is_module_busy(uint32_t baseAddr) {
    uint16_t status = 0;
    CAENVME_ReadCycle(handle, baseAddr + STATUS_REG, &status, cvA32_U_DATA, cvD16);
    return (status & BUSY_MASK) ? 1 : 0;
}

void wait_for_modules_ready() {
    while (1) {
        int adc_busy = is_module_busy(BASE_ADC);
        int tdc_busy = is_module_busy(BASE_TDC);

        if (!adc_busy && !tdc_busy) {
            break; // both modules are ready
        }
        usleep(10); // wait 10 µs before rechecking
    }
}

void read_event_data() {
    // Add your VME readout logic here
    // e.g., read ADC and TDC buffers, event counters, etc.
}

int main() {
    // Open VME device
    if (CAENVME_Init(cvV2718, 0, 0, &handle) != cvSuccess) {
        printf("Failed to initialize VME device\n");
        return 1;
    }

    while (1) {
        // Wait until both modules are ready
        wait_for_modules_ready();

        // (Optional) Send software trigger here, or wait for hardware trigger

        // Read data
        read_event_data();
    }

    CAENVME_End(handle);
    return 0;
}
