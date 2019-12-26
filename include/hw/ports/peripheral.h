#ifndef HW_AVR_PERIPHERAL_H
#define HW_AVR_PERIPHERAL_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "hw/hw.h"

#define ID_DIGIO 0
#define ID_ADC 8

extern const uint8_t peripheral_msg_lengths[];

#define NUM_PINS 8
#define VCC 5.0

#define TYPE_AVR_PERIPHERAL "avr-peripheral"
#define AVR_PERIPHERAL(obj) \
    OBJECT_CHECK(AVRPeripheralState, (obj), TYPE_AVR_PERIPHERAL)


typedef struct 
{
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    MemoryRegion mmio;

    CharBackend chr;
    
    /* UART START */
	//uint8_t pin;
	uint8_t output_values;
	uint8_t input_values;

    /* Receive Complete */
    qemu_irq rxc_irq;
    /* Transmit Complete */
    qemu_irq txc_irq;
    /* Data Register Empty */
    qemu_irq dre_irq;
    /* UART END */

    /* ADC START */
    uint8_t adcsr;
    uint8_t admux;
    uint16_t adc;   // 10 bit => 2 byte

    double adc_voltages[NUM_PINS];
    /* ADC END */

} AVRPeripheralState;

/* Virtual Functions:
    can_receive
    receive
    read
    write
*/
typedef int (*CanReceive)(void *opaque);
typedef void (*Receive)(void *opaque, const uint8_t *buffer, int size, int pinno);
typedef uint64_t (*Read)(void *opaque, hwaddr addr, unsigned int size);
typedef void (*Write)(void *opaque, hwaddr addr, uint64_t value, unsigned int size);
typedef int (*IsActive)(void * opaque, uint32_t pinno);

#define AVR_PERIPHERAL_GET_CLASS(obj) \
    OBJECT_GET_CLASS(AVRPeripheralClass, obj, TYPE_AVR_PERIPHERAL)
#define AVR_PERIPHERAL_CLASS(klass) \
    OBJECT_CLASS_CHECK(AVRPeripheralClass, klass, TYPE_AVR_PERIPHERAL)

typedef struct AVRPeripheralClass 
{
    SysBusDevice parent_class;

    CanReceive can_receive;
    Receive receive;
    Read read;
    Write write;
    IsActive is_active;
} AVRPeripheralClass;

#endif
