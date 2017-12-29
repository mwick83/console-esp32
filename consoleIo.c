// Console IO is a wrapper between the actual in and output and the console code
// In an embedded system, this might interface to a UART driver.

#include "consoleIo.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"

#include "user_config.h"

// prototype from console.c to prevent circular include in the first place
extern void ConsoleProcess(void);


// log tag
static const char* LOG_TAG_CONSOLEIO = "console_io";

// private prototypes
static void ConsoleIoTask(void* params);

// private globals
static bool consoleIoEchoEn;
QueueHandle_t consoleIoRxDriverQueue;

// processing task state+buffers
//#define consoleIoTaskStackSize (configMINIMAL_STACK_SIZE)
#define consoleIoTaskStackSize 3072
static const UBaseType_t consoleIoTaskPrio = tskIDLE_PRIORITY + 1;
StackType_t consoleIoTaskStack[consoleIoTaskStackSize];
StaticTask_t consoleIoTaskBuf;
TaskHandle_t consoleIoTaskHandle;


eConsoleError ConsoleIoInit(void)
{
    consoleIoEchoEn = false;

#ifndef NO_CONSOLE_IO_LL_INIT
    uart_config_t cfg;
    cfg.baud_rate = consolePortBaud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.rx_flow_ctrl_thresh = 1;

    ESP_ERROR_CHECK(uart_param_config(portNum, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(consolePortNum, consoleTxPin, consoleRxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
#endif

    ESP_ERROR_CHECK(uart_driver_install(consolePortNum, UART_FIFO_LEN*2, UART_FIFO_LEN*2, 2, &consoleIoRxDriverQueue, 0));
    ESP_ERROR_CHECK(uart_flush(consolePortNum));

    consoleIoTaskHandle = xTaskCreateStatic(ConsoleIoTask, "console_io_task", consoleIoTaskStackSize, (void*) NULL, consoleIoTaskPrio, consoleIoTaskStack, &consoleIoTaskBuf);
    if(NULL != consoleIoTaskHandle) {
        ESP_LOGI(LOG_TAG_CONSOLEIO, "ConsoleIo task created. Starting.");
        return CONSOLE_SUCCESS;
    } else {
        ESP_LOGE(LOG_TAG_CONSOLEIO, "ConsoleIo task creation failed!");
        return CONSOLE_ERROR;
    }
}

static void ConsoleIoTask(void* params)
{
    static uart_event_t uart_event;

    while(1) {
        if(pdTRUE == xQueueReceive(consoleIoRxDriverQueue, &uart_event, 200 / portTICK_PERIOD_MS)) {
            if(uart_event.type == UART_DATA) {
                ConsoleProcess();
            } else {
                ESP_LOGW(LOG_TAG_CONSOLEIO, "Unhandled UART event reveived: type = %d", (uint32_t) uart_event.type);
            }
        }
    }

    vTaskDelete(NULL);
}

eConsoleError ConsoleIoSetEcho(bool enable)
{
    consoleIoEchoEn = enable;
    return CONSOLE_SUCCESS;
}

eConsoleError ConsoleIoReady(void)
{
    size_t bytes = 0;

    if((ESP_OK == uart_get_buffered_data_len(consolePortNum, &bytes)) && (bytes > 0)) {
        return CONSOLE_SUCCESS;
    } else {
        return CONSOLE_ERROR;
    }
}

eConsoleError ConsoleIoReceive(uint8_t *buffer, const uint32_t bufferLength, uint32_t *readLength)
{
    uint8_t i = 0;
    size_t bytes = 0;

    while ((ESP_OK == uart_get_buffered_data_len(consolePortNum, &bytes)) && (bytes > 0) && ( i < bufferLength ) )
    {
        if(bytes > (bufferLength-i)) {
            bytes = bufferLength-i;
        }

        //ESP_LOGD("console", "Reading %d bytes from UART", bytes);

        if(ESP_FAIL != uart_read_bytes(consolePortNum, &buffer[i], bytes, 1)) {
            if(consoleIoEchoEn) {
                uart_write_bytes(consolePortNum, (const char*) &buffer[i], bytes);
            }
            i += bytes;
        }
    }
    *readLength = i;
    return CONSOLE_SUCCESS;
}

eConsoleError ConsoleIoSend(const uint8_t *buffer, const uint32_t bufferLength, uint32_t *sentLength)
{
    eConsoleError ret = CONSOLE_SUCCESS;
    int stat;

    stat = uart_write_bytes(consolePortNum, (const char*) buffer, bufferLength);
    if(ESP_FAIL != stat) {
        *sentLength = stat;
    } else {
        *sentLength = 0;
        ret = CONSOLE_ERROR;
    }

    return ret;
}

eConsoleError ConsoleIoSendString(const char *buffer)
{
    uint32_t sentLen = 0;
    return ConsoleIoSend((const uint8_t*) buffer, strlen(buffer), &sentLen);
}
