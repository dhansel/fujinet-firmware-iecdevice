/*
 * IEC with DolphinDos support
 */
#ifdef PINMAP_FUJIAPPLE_IEC_DD

/* SD Card */
#define PIN_CARD_DETECT         GPIO_NUM_35 // fnSystem.h
#define PIN_CARD_DETECT_FIX     GPIO_NUM_35 // fnSystem.h
#define PIN_SD_HOST_CS          GPIO_NUM_12 // (shared with PIN_LED_BUS) fnFsSD.cpp
#define PIN_SD_HOST_MISO        GPIO_NUM_19
#define PIN_SD_HOST_MOSI        GPIO_NUM_23
#define PIN_SD_HOST_SCK         GPIO_NUM_18
#define PIN_CARD_DETECT_INV     1           // if 1 then a HIGH input on CD pin means "card present", otherwise inverted

/* UART */
#define PIN_UART0_RX            GPIO_NUM_3  // fnUART.cpp
#define PIN_UART0_TX            GPIO_NUM_1
#define PIN_UART1_RX            GPIO_NUM_NC
#define PIN_UART1_TX            GPIO_NUM_NC
#define PIN_UART2_RX            GPIO_NUM_NC
#define PIN_UART2_TX            GPIO_NUM_NC

/* Buttons */
#define PIN_BUTTON_A            GPIO_NUM_0  // keys.cpp
#define PIN_BUTTON_B            GPIO_NUM_NC // No Button B
#define PIN_BUTTON_C            GPIO_NUM_NC // GPIO_NUM_14

/* LEDs */
#define PIN_LED_WIFI            GPIO_NUM_2  // led.cpp
#define PIN_LED_BUS             GPIO_NUM_12 // (shared with PIN_SD_HOST_CS), 4 FN
#define PIN_LED_BT              GPIO_NUM_NC // No BT LED

/* Audio Output */
#define PIN_DAC1                GPIO_NUM_NC // samlib.h

// Reset line is available
#define IEC_HAS_RESET

#define PIN_IEC_RESET           GPIO_NUM_39
#define PIN_IEC_ATN             GPIO_NUM_34
#define PIN_IEC_CLK_IN          GPIO_NUM_33
#define PIN_IEC_CLK_OUT         GPIO_NUM_33
#define PIN_IEC_DATA_IN         GPIO_NUM_32
#define PIN_IEC_DATA_OUT        GPIO_NUM_32
#define PIN_IEC_SRQ             GPIO_NUM_5

// iec.c, pins for DolphinDos parallel cable
#define PIN_IEC_PARALLEL_HT     GPIO_NUM_4  // handhake transmit (FLAG2 on C64 user port)
#define PIN_IEC_PARALLEL_HR     GPIO_NUM_36 // handshake receive (PC2 on C64 user port)
#define PIN_IEC_PARALLEL_D0     GPIO_NUM_21 // D0 (PB0 on C64 user port)
#define PIN_IEC_PARALLEL_D1     GPIO_NUM_22 // D1 (PB1 on C64 user port)
#define PIN_IEC_PARALLEL_D2     GPIO_NUM_13 // D2 (PB2 on C64 user port)
#define PIN_IEC_PARALLEL_D3     GPIO_NUM_14 // D3 (PB3 on C64 user port)
#define PIN_IEC_PARALLEL_D4     GPIO_NUM_15 // D4 (PB4 on C64 user port)
#define PIN_IEC_PARALLEL_D5     GPIO_NUM_25 // D5 (PB5 on C64 user port)
#define PIN_IEC_PARALLEL_D6     GPIO_NUM_26 // D6 (PB6 on C64 user port)
#define PIN_IEC_PARALLEL_D7     GPIO_NUM_27 // D7 (PB7 on C64 user port)

#endif // PINMAP_FUIAPPLE_IEC
