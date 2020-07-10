#include "qemu/osdep.h"
#include "hw/ports/avr_timer_8b.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/ports/avr_port.h"

#define WGM00   1
#define WGM01   2
#define WGM02   8

#define MODE_NORMAL 0
#define MODE_CTC 2
#define MODE_FAST_PWM 3
#define MODE_FAST_PWM2 7
#define MODE_PHASE_PWM 1
#define MODE_PHASE_PWM2 5


/* Clock source values */
#define T16_CLKSRC_STOPPED     0
#define T16_CLKSRC_DIV1        1
#define T16_CLKSRC_DIV8        2
#define T16_CLKSRC_DIV64       3
#define T16_CLKSRC_DIV256      4
#define T16_CLKSRC_DIV1024     5
#define T16_CLKSRC_EXT_FALLING 6
#define T16_CLKSRC_EXT_RISING  7

#define T16_INT_TOV    0x1 /* Timer overflow */
#define T16_INT_OCA    0x2 /* Output compare A */
#define T16_INT_OCB    0x4 /* Output compare B */


                    // fourth bit, set to bit 3 + bits 0 and 1 of cra! == WGM02:0
#define MODE(t16)   ((t16->cra & WGM00) | (t16->cra & WGM01) | ((t16->crb & WGM02) >> 1))
#define CLKSRC(t16) (t16->crb & 7)
#define CNT(t16)    (t16->cnt)
#define OCRA(t16)   (t16->ocra)
#define OCRB(t16)   (t16->ocrb)

//#define dprintf(fmt, args...)    fprintf(stderr, fmt, ## args)
#define dprintf(fmt, args...) 

static int avr_timer_8b_can_receive(void *opaque)
{
    return 0;
}

// if PWM is active or pin somehow other used...
static int avr_timer_8b_is_active(void *opaque, PinID pin)
{
    uint8_t pinno = pin.PinNum;
    AVRPortState * port = (AVRPortState*)pin.pPort;
    port->name = port->name;    // to disable errors due to unused port if dprintf is disabled...
    dprintf("Call Timer is active on Port %c Pin %d\n", port->name, pinno);
    AVRPeripheralState *t16 = opaque;

    if (CLKSRC(t16) == T16_CLKSRC_EXT_FALLING ||
        CLKSRC(t16) == T16_CLKSRC_EXT_RISING ||
        CLKSRC(t16) == T16_CLKSRC_STOPPED) {
        /* Timer is disabled or set to external clock source (unsupported) */
        dprintf("No supported clock set...\n");
        return 0;
    }

    if(MODE(t16) == MODE_NORMAL || MODE(t16) == MODE_CTC)
    {
        return 0;
    }

    uint8_t com_mode = 0;
    // the pin must be set to output port!
    if(pin.pPort == t16->Output_A.pPort && pin.PinNum == t16->Output_A.PinNum)
    {
        AVRPortState* pPort = (AVRPortState*)t16->Output_A.pPort;  
        uint8_t pin_mask = (1 << pinno);
        if(pPort->ddr & pin_mask)
        {
            com_mode = (t16->cra & 0b11000000) >> 6;
            if(com_mode == 0 || (com_mode == 1 && !(t16->crb & WGM02)))
            {
                printf("Output A is disabled due to wrong COM mode\n");
                return 0;
            }
            return 1;
        }

        printf("Warning: Are you trying to use PWM while DDR is set to input?");
    }
    else if(pin.pPort == t16->Output_B.pPort && pin.PinNum == t16->Output_B.PinNum)
    {
        AVRPortState* pPort = (AVRPortState*)t16->Output_B.pPort;  
        uint8_t pin_mask = (1 << pinno);
        if(pPort->ddr & pin_mask)
        {
            com_mode = (t16->cra & 0b00110000) >> 4;
            if(com_mode == 0 || com_mode == 1)
            {
                printf("Output B is disabled due to wrong COM mode\n");
                return 0;
            }
            return 1;
        }

        printf("Warning: Are you trying to use PWM while DDR is set to input?");
    }

    return 0;
}

static void avr_timer_8b_receive(void *opaque, const uint8_t *buffer, int msgid, PinID pin)
{
    assert(false);
}

static uint64_t avr_timer_8b_read_ifr(void *opaque, hwaddr addr, unsigned int size)
{
    assert(size == 1);
    //printf("Reading IFR...\n");
    AVRPeripheralState *t16 = opaque;
    if (addr != 0) 
    {
        printf("Reading IFR that is not implemented!\n");
        return 0;
    }
    //printf("Returning IFR = %d\n", t16->ifr);
    return t16->ifr;
}

static uint64_t avr_timer_8b_read_imsk(void *opaque, hwaddr addr, unsigned int size)
{
    assert(size == 1);
    AVRPeripheralState *t16 = opaque;
    if (addr != 0) 
    {
        printf("Reading IMSK that is not implemented!\n");
        return 0;
    }
    return t16->imsk;
}

static void avr_timer_8b_clksrc_update(AVRPeripheralState *t16)
{
    uint16_t divider = 0;
    switch (CLKSRC(t16)) {
    case T16_CLKSRC_EXT_FALLING:
    case T16_CLKSRC_EXT_RISING:
        printf("external clock source unsupported\n");
        goto end;
    case T16_CLKSRC_STOPPED:
        goto end;
    case T16_CLKSRC_DIV1:
        divider = 1;
        break;
    case T16_CLKSRC_DIV8:
        divider = 8;
        break;
    case T16_CLKSRC_DIV64:
        divider = 64;
        break;
    case T16_CLKSRC_DIV256:
        divider = 256;
        break;
    case T16_CLKSRC_DIV1024:
        divider = 1024;
        break;
    default:
        goto end;
    }
    t16->freq_hz = t16->cpu_freq_hz / divider;
    t16->period_ns = NANOSECONDS_PER_SECOND / t16->freq_hz;
    dprintf("Timer frequency %" PRIu64 " hz, period %" PRIu64 " ns (%f s)\n",
             t16->freq_hz, t16->period_ns, 1 / (double)t16->freq_hz);
end:
    return;
}

//
static void avr_timer_8b_set_alarm(AVRPeripheralState *t16)
{
    return;
    /*if (CLKSRC(t16) == T16_CLKSRC_EXT_FALLING ||
        CLKSRC(t16) == T16_CLKSRC_EXT_RISING ||
        CLKSRC(t16) == T16_CLKSRC_STOPPED) {
        // Timer is disabled or set to external clock source (unsupported) 
        dprintf("No clock set...\n");
        goto end;
    }

    uint64_t alarm_offset = 0xff;
    uint8_t next_interrupt = INTERRUPT_OVERFLOW;

    switch (MODE(t16)) 
    {
        case MODE_NORMAL:
            // Normal mode 
            if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16) &&
                (t16->imsk & T16_INT_OCA)) 
            {
                alarm_offset = OCRA(t16);
                next_interrupt = INTERRUPT_COMPA;
            }
            break;
        case MODE_CTC:
            // CTC mode, top = ocra 
            if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16)) {
                alarm_offset = OCRA(t16);
                next_interrupt = INTERRUPT_COMPA;
            }
        break;
        case MODE_FAST_PWM:
        case MODE_FAST_PWM2:        // TODO: PWM2 TOV
        {
            // COMPA interrupt can occur
            if (OCRA(t16) < alarm_offset && OCRA(t16) > CNT(t16) &&
                (t16->imsk & T16_INT_OCA)) {
                alarm_offset = OCRA(t16);
                next_interrupt = INTERRUPT_COMPA;
            }
        }
        break;
        default:
            dprintf("pwm modes are unsupported\n");
            goto end;
    }

    if (OCRB(t16) < alarm_offset && OCRB(t16) > CNT(t16) &&
        (t16->imsk & T16_INT_OCB)) 
    {
        alarm_offset = OCRB(t16);
        next_interrupt = INTERRUPT_COMPB;
    }

    alarm_offset -= CNT(t16);

    t16->next_interrupt = next_interrupt;
    uint64_t alarm_ns = t16->reset_time_ns + ((CNT(t16) + alarm_offset) * t16->period_ns);
    timer_mod(t16->timer, alarm_ns);

end:
    return;*/
}

static inline void avr_timer_8b_recalc_reset_time(AVRPeripheralState *t16)
{
    t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                         CNT(t16) * t16->period_ns;
}

static uint64_t avr_timer_8b_read(void *opaque, hwaddr addr, unsigned int size)
{
    AVRPeripheralState *p = opaque;

    switch(addr)
    {
        case 0: //CRA
            return p->cra;
        case 1: // CRB
            return p->crb;
        case 2: // TCNT
            return p->cnt;
        case 3: // OCRA
            return p->ocra;
        case 4: // OCRB
            return p->ocrb;
        default:
            printf("Error: Wrong Hardware ID in Counter 8b\n");
    }
    return 0;
}

// only called when pwm is really enabled!
static void avr_timer_8b_toggle_pwm(AVRPeripheralState * t16)
{
    uint8_t curr_pwm = t16->cra & 0b11110011;

    dprintf("Call toggle_pwm\n");

    // PWM mode changed OR one of the compare registers changed OR the clock has been stopped!
    if(curr_pwm != t16->last_pwm || t16->ocra != t16->last_ocra || t16->ocrb != t16->last_ocrb || CLKSRC(t16) == T16_CLKSRC_STOPPED)
    {
        dprintf("Change in PWM detected!\n");
        t16->last_pwm = curr_pwm;
        t16->last_ocra = t16->ocra;
        t16->last_ocrb = t16->ocrb;

        // TODO: Output_B?
        AVRPortState* pPortA = (AVRPortState*)t16->Output_A.pPort;
        pPortA->send_data(pPortA);

        AVRPortState* pPortB = (AVRPortState*)t16->Output_B.pPort;
        if(pPortB != pPortA)
            pPortB->send_data(pPortB);
    }
}

static void avr_timer_8b_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    //printf("AVR Timer Write\n");
    assert(size == 1);
    AVRPeripheralState *t16 = opaque;
    uint8_t val8 = (uint8_t)value;
    uint8_t prev_clk_src = CLKSRC(t16);
    uint8_t last_cra;
    uint8_t last_crb;

    //printf("write %d to addr %d\n", val8, (uint8_t)addr);

    switch (addr) 
    {
    case 0:     // CRA
        last_cra = t16->cra;
        t16->cra = val8;
        //printf("Wrote something to TCCRRnA = %d\n", t16->cra);
        if (t16->cra & 0b11110000) 
        {
            printf("output compare pins unsupported\n");
        }
        if(last_cra != t16->cra)
            avr_timer_8b_toggle_pwm(t16);
        break;
    case 1:     // CRB
        last_crb = t16->crb;
        t16->crb = val8;

        // changing clocko 
        if(t16->crb != last_crb)
            avr_timer_8b_toggle_pwm(t16);

        if (CLKSRC(t16) != prev_clk_src) 
        {
            avr_timer_8b_clksrc_update(t16);
            if (prev_clk_src == T16_CLKSRC_STOPPED) {
                t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            }

            // disabling the clock...
            /*if(CLKSRC(t16) == T16_CLKSRC_STOPPED && prev_clk_src != T16_CLKSRC_STOPPED)
            {
                avr_timer_8b_toggle_pwm(t16);
            }*/
        }
        break;
    case 2:     // CNT
        /*
         * CNT is the 16-bit counter value, it must be read/written via
         * a temporary register (rtmp) to make the read/write atomic.
         */
        /* ICR also has this behaviour, and shares rtmp */
        /*
         * Writing CNT blocks compare matches for one clock cycle.
         * Writing CNT to TOP or to an OCR value (if in use) will
         * skip the relevant interrupt
         */
        t16->cnt = val8;
        //t16->cnth = t16->rtmp;
        avr_timer_8b_recalc_reset_time(t16);
        break;
    case 3:     // OCRA
        /*
         * OCRn cause the relevant output compare flag to be raised, and
         * trigger an interrupt, when CNT is equal to the value here
         */
        t16->ocra = val8;
        avr_timer_8b_toggle_pwm(t16);   // OCRA can also lead to a change! because it changes the frequency!
        break;
    case 4:     // OCRB
        t16->ocrb = val8;
        avr_timer_8b_toggle_pwm(t16);   
        break;
    default:
        printf("Writing to AVR Timer 8b that is not defined??\n");
        break;
    }
    //printf("Set alarm...\n");
    avr_timer_8b_set_alarm(t16);
}

static void avr_timer_8b_write_imsk(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    //printf("Write imsk\n");
    assert(size == 1);
    AVRPeripheralState *t16 = opaque;
    if (addr != 0) 
    {
        printf("Writing IMSK that is not implemented!\n");
        return;
    }
    t16->imsk = (uint8_t)value;
    //dprintf("Write imsk done %d\n", t16->imsk);
}

static void avr_timer_8b_write_ifr(void *opaque, hwaddr addr, uint64_t value,
                                unsigned int size)
{
    dprintf("Write IFR = %ld\n", value);
    assert(size == 1);
    AVRPeripheralState *t16 = opaque;
    if (addr != 0) 
    {
        printf("Writing IFR that is not implemented!\n");
        return;
    }
    t16->ifr = (uint8_t)value;
}

// TODO: Add phase correct PWM
static uint32_t avr_timer_8b_serialize(void * opaque, PinID pin, uint8_t * pData)
{
    AVRPeripheralState *t8 = opaque;
    uint8_t hdr, mode;

    uint8_t match = 0;

    uint8_t pinno = pin.PinNum;
    uint8_t pwm_mode = MODE(t8);
    if(pinno == t8->Output_A.PinNum && pin.pPort == t8->Output_A.pPort)         // OCnA
    {
        hdr = 0b00000100;    //PWM
        hdr |= (pinno << 5);
        mode = (t8->cra & 0b11000000) >> 6;

        //if(pwm_mode == MODE_FAST_PWM || pwm_mode == MODE_FAST_PWM2 || pwm_mode == MODE_PHASE_PWM || pwm_mode == MODE_PHASE_PWM2)
        match = t8->ocra;

        dprintf("Pin3 Mode set to %d\n", mode);
    }
    else if(pinno == t8->Output_B.PinNum && pin.pPort == t8->Output_B.pPort)    // OCnB
    {
        hdr = 0b00000100;    //PWM
        hdr |= (pinno << 5);
        mode = (t8->cra & 0b00110000) >> 4;

        //if(pwm_mode == MODE_FAST_PWM || pwm_mode == MODE_PHASE_PWM) // Output B works only for WGM2 = 0
        match = t8->ocrb;

        dprintf("Pin4 Mode = %d\n", mode);
    }
    else
    {
        printf("Wrong pinno given to avr timer serialize\n");
        return 0;
    }
    
    pData[0] = hdr;
    double val = 0;

    if(pwm_mode == MODE_NORMAL)
    {
        dprintf("PWM Mode disabled\n");
    }
    else if(pwm_mode == MODE_CTC)
    {
        printf("TODO: CTC Mode send\n");
    }
    else if(pwm_mode == MODE_FAST_PWM || pwm_mode == MODE_PHASE_PWM)
    {
        dprintf("MODE: FAST_PWM TOP=0xFF\n");

        // mode = submode
        if(mode == 0)   // Disabled
        {
            //printf("CRA = %d", t8->cra);
            dprintf("This PWM channel is disabled\n");
            //return 0;
        }
        else if(mode == 1)  // 
        {
            printf("Mode 1 not enabled for FAST_PWM with WGMn2 = 0 not enabled\n");
            return 0;
        }
        else if(mode == 3)
            val = (double)(match + 1) / 256.0;
        else if(mode == 2)
            val = (double)(255 - match) / 256.0;

        dprintf("Val = %f with top = 255\n", val);
    }
    else if(pwm_mode == MODE_FAST_PWM2 || pwm_mode == MODE_PHASE_PWM2)
    {
        dprintf("MODE: FAST_PWM TOP=OCRnA\n");

        uint8_t top = t8->ocra;

        // mode = submode
        if(mode == 0)   // Disabled
        {
            //printf("CRA = %d", t8->cra);
            dprintf("This PWM channel is disabled\n");
            //return 0;
        }
        else if(mode == 1)  // 
        {
            if(pinno == t8->Output_A.PinNum && pin.pPort == t8->Output_A.pPort)
                val = 0.5f;
            else
                printf("Caution: PWM Mode(1) with WGMn2=1 only available for Output A\n");
        }
        else if(mode == 3)
            val = (double)(match + 1) / (double)(top+1);
        else if(mode == 2)
            val = (double)(top - match) / (double)(top+1);

        dprintf("Val = %f with top = %d\n", val, top);
    }
    

    memcpy(pData+1, &val, sizeof(double));
    return 9;
}

//
static void avr_timer_8b_clock_reset(AVRPeripheralState *t16)
{
    t16->cnt = 0;
    t16->reset_time_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

//
static inline int64_t avr_timer_8b_ns_to_ticks(AVRPeripheralState *t16, int64_t t)
{
    if (t16->period_ns == 0) {
        return 0;
    }
    return t / t16->period_ns;
}

//
/*static void avr_timer_8b_update_cnt(AVRPeripheralState *t16)
{
    uint16_t cnt;
    cnt = avr_timer_8b_ns_to_ticks(t16, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                                       t16->reset_time_ns);
    t16->cnt = (uint8_t)(cnt & 0xff);
}*/


static void avr_timer_8b_increment(void *opaque)
{
    //printf("ICREMENTING\n");
    AVRPeripheralState *t = opaque;
    uint8_t clksrc = CLKSRC(t);
    if(clksrc == T16_CLKSRC_STOPPED)
        return;

    //printf("OK, es passiert was...\n");

    switch(clksrc)
    {
        case T16_CLKSRC_DIV1:
            t->cnt++;
            break;

        case T16_CLKSRC_DIV8:
            t->prescale_count++;
            if(t->prescale_count == 8)
            {
                t->cnt++;
                t->prescale_count = 0;
            }
            break;

        case T16_CLKSRC_DIV64:
            t->prescale_count++;
            if(t->prescale_count == 64)
            {
                t->cnt++;
                t->prescale_count = 0;
            }
            break;

        case T16_CLKSRC_DIV256:
            t->prescale_count++;
            if(t->prescale_count == 256)
            {
                t->cnt++;
                t->prescale_count = 0;
            }
            break;

        case T16_CLKSRC_DIV1024:
            t->prescale_count++;
            if(t->prescale_count == 1024)
            {
                t->cnt++;
                dprintf("Counter set to %d\n", t->cnt);
                t->prescale_count = 0;
            }
            break;

        default:
            printf("ERROR: Not supported timer clock source chosen!");
    }

    //printf("New counter: %d. IFR = %d\n", t->cnt, t->ifr);

    if(t->cnt == 0)
    {
        if(t->imsk & T16_INT_TOV)
            qemu_set_irq(t->ovf_irq, 1);

        t->ifr |= T16_INT_TOV;
    }

    if (t->cnt == t->ocra) 
    {
        dprintf("Set Compare flag...\n");
        t->ifr |= T16_INT_OCA;
        if(t->imsk & T16_INT_OCA)
        {
            dprintf("Set Compa irq\n");
            qemu_set_irq(t->compa_irq, 1);
        }

        //printf("Mode = %d\n", MODE(t));
        if(MODE(t) == MODE_CTC || MODE(t) == MODE_FAST_PWM2)
        {
            t->cnt = 0;
            dprintf("Counter set to 0\n");
        }
    }

    if (t->imsk & T16_INT_OCB && t->cnt == t->ocrb) 
    {
        dprintf("Set compb irq\n");
        t->ifr |= T16_INT_OCB;
        qemu_set_irq(t->compb_irq, 1);
    }

}

//
/*static void avr_timer_8b_interrupt(void *opaque)
{
    printf("Der interrupt ballert\n");
    AVRPeripheralState *t16 = opaque;
    uint8_t mode = MODE(t16);

    avr_timer_8b_update_cnt(t16);

    if (CLKSRC(t16) == T16_CLKSRC_EXT_FALLING ||
        CLKSRC(t16) == T16_CLKSRC_EXT_RISING ||
        CLKSRC(t16) == T16_CLKSRC_STOPPED) {
        // Timer is disabled or set to external clock source (unsupported) 
        return;
    }

    //printf("interrupt, cnt = %d\n", CNT(t16));

    // Counter overflow 
    if (t16->next_interrupt == INTERRUPT_OVERFLOW) {
        //printf("0xff overflow\n");
        avr_timer_8b_clock_reset(t16);
        //printf("Before interrupt: %d\n", t16->imsk);
        if (t16->imsk & T16_INT_TOV) 
        {
            //printf("Starting overflow...\n");
            t16->ifr |= T16_INT_TOV;
            qemu_set_irq(t16->ovf_irq, 1);
        }
    }
    // Check for ocra overflow in CTC mode 
    if (mode == MODE_CTC && t16->next_interrupt == INTERRUPT_COMPA) 
    {
        dprintf("CTC OCRA overflow\n");
        avr_timer_8b_clock_reset(t16);
    }
    // Check for output compare interrupts 
    if (t16->imsk & T16_INT_OCA && t16->next_interrupt == INTERRUPT_COMPA) 
    {
        dprintf("Set Compa irq\n");
        t16->ifr |= T16_INT_OCA;
        qemu_set_irq(t16->compa_irq, 1);
    }
    if (t16->imsk & T16_INT_OCB && t16->next_interrupt == INTERRUPT_COMPB) 
    {
        dprintf("Set compb irq\n");
        t16->ifr |= T16_INT_OCB;
        qemu_set_irq(t16->compb_irq, 1);
    }

    avr_timer_8b_set_alarm(t16);
}*/

//
static void avr_timer_8b_reset(DeviceState *dev)
{
    AVRPeripheralState *t16 = AVR_TIMER_8b(dev);

    avr_timer_8b_clock_reset(t16);
    avr_timer_8b_clksrc_update(t16);
    avr_timer_8b_set_alarm(t16);

    qemu_set_irq(t16->compa_irq, 0);
    qemu_set_irq(t16->compb_irq, 0);
    qemu_set_irq(t16->ovf_irq, 0);
}

static void avr_timer_8b_pr(void *opaque, int irq, int level)
{
    AVRPeripheralState *s = AVR_TIMER_8b(opaque);

    s->enabled = !level;

    if (!s->enabled) {
        avr_timer_8b_reset(DEVICE(s));
    }
}

/* Class functions below... */
static Property avr_timer_8b_properties[] = {
    DEFINE_PROP_UINT64("cpu-frequency-hz", AVRPeripheralState,
                       cpu_freq_hz, 20000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void avr_timer_8b_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AVRPeripheralClass *pc = AVR_PERIPHERAL_CLASS(klass);
    AVRTimer8bClass * adc = AVR_TIMER_8b_CLASS(klass);

    dc->reset = avr_timer_8b_reset;
    dc->props = avr_timer_8b_properties;
  
    adc->parent_can_receive = pc->can_receive;
    adc->parent_receive = pc->receive;
    adc->parent_read = pc->read;
    adc->parent_write = pc->write;
    adc->parent_is_active = pc->is_active;
    adc->parent_serialize = pc->serialize;

    adc->parent_read_ifr = pc->read_ifr;
    adc->parent_read_imsk = pc->read_imsk;
    adc->parent_write_ifr = pc->write_ifr;
    adc->parent_write_imsk = pc->write_imsk;

    pc->can_receive = avr_timer_8b_can_receive;
    pc->read = avr_timer_8b_read;
    pc->write = avr_timer_8b_write;
    pc->receive = avr_timer_8b_receive;
    pc->is_active = avr_timer_8b_is_active;
    pc->serialize = avr_timer_8b_serialize;

    pc->write_ifr = avr_timer_8b_write_ifr;
    pc->write_imsk = avr_timer_8b_write_imsk;
    pc->read_ifr = avr_timer_8b_read_ifr;
    pc->read_imsk = avr_timer_8b_read_imsk;
    dprintf("Timer 8b initiated\n");
}


static const MemoryRegionOps avr_timer_8b_ops = {
    .read = avr_timer_8b_read,
    .write = avr_timer_8b_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.min_access_size = 1, .max_access_size = 1}
};

static const MemoryRegionOps avr_timer_8b_imsk_ops = {
    .read = avr_timer_8b_read_imsk,
    .write = avr_timer_8b_write_imsk,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static const MemoryRegionOps avr_timer_8b_ifr_ops = {
    .read = avr_timer_8b_read_ifr,
    .write = avr_timer_8b_write_ifr,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {.max_access_size = 1}
};

static void avr_timer_8b_init(Object *obj)
{
    AVRPeripheralState *s = AVR_PERIPHERAL(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->compa_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->compb_irq);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->ovf_irq);


    memory_region_init_io(&s->mmio, obj, &avr_timer_8b_ops, s, TYPE_AVR_TIMER_8b, 5); 

    memory_region_init_io(&s->mmio_imsk, obj, &avr_timer_8b_imsk_ops,
                          s, TYPE_AVR_TIMER_8b, 0x1);
    memory_region_init_io(&s->mmio_ifr, obj, &avr_timer_8b_ifr_ops,
                          s, TYPE_AVR_TIMER_8b, 0x1);

    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio_imsk);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio_ifr);

    qdev_init_gpio_in(DEVICE(s), avr_timer_8b_pr, 1);

    //s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, avr_timer_8b_interrupt, s);
    s->counter = counter_new(avr_timer_8b_increment, s);
    s->enabled = true;
    printf("AVR Timer8b object init\n");
}

static const TypeInfo avr_timer_8b_info = {
    .name          = TYPE_AVR_TIMER_8b,
    .parent        = TYPE_AVR_PERIPHERAL,
    .class_init    = avr_timer_8b_class_init,
    .class_size    = sizeof(AVRTimer8bClass),
    .instance_size = sizeof(AVRPeripheralState),
    .instance_init = avr_timer_8b_init
};

static void avr_timer_8b_register_types(void)
{
    type_register_static(&avr_timer_8b_info);
}

type_init(avr_timer_8b_register_types)