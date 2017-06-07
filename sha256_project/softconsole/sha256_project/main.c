/*******************************************************************************
 *
 *
 * Simple SmartFusion2 Microcontroller subsystem (MSS) GPIO example program.
 *
 *
 */
#include "drivers/mss_gpio/mss_gpio.h"
#include "CMSIS/system_m2sxxx.h"

/*==============================================================================
  Private functions.
 */
static void delay(void);

#define PIN_LED MSS_GPIO_0
#define PIN_DATA_IN_READY MSS_GPIO_1
#define PIN_LASTBANK_AVAILABLE0 MSS_GPIO_2
#define PIN_LASTBANK_AVAILABLE1 MSS_GPIO_3
#define PIN_REQ_DATA MSS_GPIO_4
#define PIN_DATA_OUT_READY MSS_GPIO_5
#define PIN_VALID_OUTPUT MSS_GPIO_6
#define PIN_DATA_AVAILABLE MSS_GPIO_7
#define PIN_ERROR_OUTPUT MSS_GPIO_8
#define PIN_RESET MSS_GPIO_9
#define PIN_STATE0 MSS_GPIO_10
#define PIN_STATE1 MSS_GPIO_11
#define PIN_STATE2 MSS_GPIO_12
/*==============================================================================
 * main() function.
 */
int main()
{
    /*
     * Initialize MSS GPIOs.
     */
    MSS_GPIO_init();

    /*
     * Configure MSS GPIOs.
     */
    MSS_GPIO_config( PIN_LED , MSS_GPIO_OUTPUT_MODE ); // LED
    MSS_GPIO_config( PIN_DATA_IN_READY , MSS_GPIO_OUTPUT_MODE ); // Read Enable
    MSS_GPIO_config( PIN_RESET , MSS_GPIO_OUTPUT_MODE ); // Reset

    MSS_GPIO_config( PIN_LASTBANK_AVAILABLE0, MSS_GPIO_INPUT_MODE );
    MSS_GPIO_config( PIN_LASTBANK_AVAILABLE1, MSS_GPIO_INPUT_MODE );
    MSS_GPIO_config( PIN_REQ_DATA, MSS_GPIO_INPUT_MODE ); // Requesting more data
    MSS_GPIO_config( PIN_DATA_OUT_READY, MSS_GPIO_INPUT_MODE ); // Data Out Ready (first bank of registers has read enable=1)
    MSS_GPIO_config( PIN_VALID_OUTPUT, MSS_GPIO_INPUT_MODE ); // Valid Output at the end of the SHA-256 core
    MSS_GPIO_config( PIN_ERROR_OUTPUT, MSS_GPIO_INPUT_MODE ); // Error at the SHA-256 core
    MSS_GPIO_config( PIN_DATA_AVAILABLE, MSS_GPIO_INPUT_MODE ); // Data Available (last register has a value)

    MSS_GPIO_config( PIN_STATE0, MSS_GPIO_INPUT_MODE );
    MSS_GPIO_config( PIN_STATE1, MSS_GPIO_INPUT_MODE );
    MSS_GPIO_config( PIN_STATE2, MSS_GPIO_INPUT_MODE );

    uint32_t inputs = MSS_GPIO_get_inputs();

    MSS_GPIO_set_output( PIN_DATA_IN_READY, 0);
    MSS_GPIO_set_output( PIN_RESET, 0);
    MSS_GPIO_set_output( PIN_RESET, 1);

    inputs = MSS_GPIO_get_inputs();

    volatile uint32_t readv = 0;

    MSS_GPIO_set_output( PIN_LED, 1);
    
    inputs = MSS_GPIO_get_inputs();
    delay();

    // Write to AHB Slave Interface (16 words of 32-bit)
    *(volatile uint32_t *)0x30000000 = 0x00000001;
    *(volatile uint32_t *)0x30000004 = 0x00000000;
    *(volatile uint32_t *)0x30000008 = 0x00000000;
    *(volatile uint32_t *)0x3000000C = 0x00000000;
    *(volatile uint32_t *)0x30000010 = 0x00000000;
    *(volatile uint32_t *)0x30000014 = 0x00000000;
    *(volatile uint32_t *)0x30000018 = 0x00000000;
    *(volatile uint32_t *)0x3000001C = 0x00000000;
    *(volatile uint32_t *)0x30000020 = 0x00000000;
    *(volatile uint32_t *)0x30000024 = 0x00000000;
    *(volatile uint32_t *)0x30000028 = 0x00000000;
    *(volatile uint32_t *)0x3000002C = 0x00000000;
    *(volatile uint32_t *)0x30000030 = 0x00000000;
    *(volatile uint32_t *)0x30000034 = 0x00000000;
    *(volatile uint32_t *)0x30000038 = 0x00000000;
    *(volatile uint32_t *)0x3000003C = 0x00000000;
    *(volatile uint32_t *)0x30000040 = 0x00000003;

    inputs = MSS_GPIO_get_inputs();
    while(!(inputs & 0x80)) // 8th bit is 1 (data_available -> we can enable reading and give the data_out_ready signal)
    {
    	inputs = MSS_GPIO_get_inputs();
    }

    delay();
    delay();
    delay();
    delay();

    MSS_GPIO_set_output( PIN_DATA_IN_READY, 1);

    inputs = MSS_GPIO_get_inputs();
    while(!(inputs & 0x40)) // 7th bit is 1 (valid_output)
    {
    	inputs = MSS_GPIO_get_inputs();
    }

    delay();

    MSS_GPIO_set_output( PIN_LED, 0);
    MSS_GPIO_set_output( PIN_DATA_IN_READY, 0);
    MSS_GPIO_set_output( PIN_RESET, 0);

    ///////////// Now test with two blocks

    MSS_GPIO_set_output( PIN_RESET, 1);

    inputs = MSS_GPIO_get_inputs();

    readv = 0;

    MSS_GPIO_set_output( PIN_LED, 1);

    inputs = MSS_GPIO_get_inputs();
    delay();

    // Write to AHB Slave Interface (16 words of 32-bit)
    *(volatile uint32_t *)0x30000000 = 0x00000002;
    *(volatile uint32_t *)0x30000004 = 0x00000000;
    *(volatile uint32_t *)0x30000008 = 0x00000000;
    *(volatile uint32_t *)0x3000000C = 0x00000000;
    *(volatile uint32_t *)0x30000010 = 0x00000000;
    *(volatile uint32_t *)0x30000014 = 0x00000000;
    *(volatile uint32_t *)0x30000018 = 0x00000000;
    *(volatile uint32_t *)0x3000001C = 0x00000000;
    *(volatile uint32_t *)0x30000020 = 0x00000000;
    *(volatile uint32_t *)0x30000024 = 0x00000000;
    *(volatile uint32_t *)0x30000028 = 0x00000000;
    *(volatile uint32_t *)0x3000002C = 0x00000000;
    *(volatile uint32_t *)0x30000030 = 0x00000000;
    *(volatile uint32_t *)0x30000034 = 0x00000000;
    *(volatile uint32_t *)0x30000038 = 0x00000000;
    *(volatile uint32_t *)0x3000003C = 0x00000000;
    *(volatile uint32_t *)0x30000040 = 0x00000001; // first

    inputs = MSS_GPIO_get_inputs();
	while(!(inputs & 0x80)) // 8th bit is 1 (data_available -> we can enable reading and give the data_out_ready signal)
	{
		inputs = MSS_GPIO_get_inputs();
	}

    MSS_GPIO_set_output( PIN_DATA_IN_READY, 1);

	inputs = MSS_GPIO_get_inputs();
	while(!(inputs & 0x20)) // 5th bit is 1 (req_more)
	{
		inputs = MSS_GPIO_get_inputs();
	}

	MSS_GPIO_set_output( PIN_DATA_IN_READY, 0);

	// Write to AHB Slave Interface (16 words of 32-bit)
	*(volatile uint32_t *)0x30000000 = 0x00000002;
	*(volatile uint32_t *)0x30000004 = 0x00000000;
	*(volatile uint32_t *)0x30000008 = 0x00000000;
	*(volatile uint32_t *)0x3000000C = 0x00000000;
	*(volatile uint32_t *)0x30000010 = 0x00000000;
	*(volatile uint32_t *)0x30000014 = 0x00000000;
	*(volatile uint32_t *)0x30000018 = 0x00000000;
	*(volatile uint32_t *)0x3000001C = 0x00000000;
	*(volatile uint32_t *)0x30000020 = 0x00000000;
	*(volatile uint32_t *)0x30000024 = 0x00000000;
	*(volatile uint32_t *)0x30000028 = 0x00000000;
	*(volatile uint32_t *)0x3000002C = 0x00000000;
	*(volatile uint32_t *)0x30000030 = 0x00000000;
	*(volatile uint32_t *)0x30000034 = 0x00000000;
	*(volatile uint32_t *)0x30000038 = 0x00000000;
	*(volatile uint32_t *)0x3000003C = 0x00000000;
	*(volatile uint32_t *)0x30000040 = 0x00000000;

	inputs = MSS_GPIO_get_inputs();
	while(!(inputs & 0x80)) // 8th bit is 1 (data_available -> we can enable reading and give the data_out_ready signal)
	{
		inputs = MSS_GPIO_get_inputs();
	}

	MSS_GPIO_set_output( PIN_DATA_IN_READY, 1);

	delay();

	inputs = MSS_GPIO_get_inputs();
	while(!(inputs & 0x20)) // 5th bit is 1 (req_more)
	{
		inputs = MSS_GPIO_get_inputs();
	}

    // Write to AHB Slave Interface (16 words of 32-bit)
    *(volatile uint32_t *)0x30000000 = 0x00000003;
    *(volatile uint32_t *)0x30000004 = 0x00000000;
    *(volatile uint32_t *)0x30000008 = 0x00000000;
    *(volatile uint32_t *)0x3000000C = 0x00000000;
    *(volatile uint32_t *)0x30000010 = 0x00000000;
    *(volatile uint32_t *)0x30000014 = 0x00000000;
    *(volatile uint32_t *)0x30000018 = 0x00000000;
    *(volatile uint32_t *)0x3000001C = 0x00000000;
    *(volatile uint32_t *)0x30000020 = 0x00000000;
    *(volatile uint32_t *)0x30000024 = 0x00000000;
    *(volatile uint32_t *)0x30000028 = 0x00000000;
    *(volatile uint32_t *)0x3000002C = 0x00000000;
    *(volatile uint32_t *)0x30000030 = 0x00000000;
    *(volatile uint32_t *)0x30000034 = 0x00000000;
    *(volatile uint32_t *)0x30000038 = 0x00000000;
    *(volatile uint32_t *)0x3000003C = 0x00000000;
    *(volatile uint32_t *)0x30000040 = 0x00000002; // last

	inputs = MSS_GPIO_get_inputs();
	while(!(inputs & 0x80)) // 8th bit is 1 (data_available -> we can enable reading and give the data_out_ready signal)
	{
		inputs = MSS_GPIO_get_inputs();
	}

    MSS_GPIO_set_output( PIN_DATA_IN_READY, 1);

    delay();

    inputs = MSS_GPIO_get_inputs();
    while(!(inputs & 0x40)) // 7th bit is 1 (valid_output)
    {
    	inputs = MSS_GPIO_get_inputs();
    }

    MSS_GPIO_set_output( PIN_LED, 0);
    MSS_GPIO_set_output( PIN_DATA_IN_READY, 0);
    MSS_GPIO_set_output( PIN_RESET, 0);
    
    return 0;
}

/*==============================================================================
  Delay between displays of the watchdog counter value.
 */
static void delay(void)
{
    volatile uint32_t delay_count = SystemCoreClock / 128u;
    
    while(delay_count > 0u)
    {
        --delay_count;
    }
}
