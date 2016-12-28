/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "mqtt.h"
#include "gpio.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"

char test_mode = 4;
uint8 dht_t1=0, dht_t2=0;
uint8 dht_d1=0, dht_d2=0;
uint8 dht_chk=0, dht_num=0;

MQTT_Client mqttClient;

const char *data_led_on = {"breaker:open"};
const char *data_led_off = {"breaker:close"};

/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 user_rf_cal_sector_set(void)
{
    flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

uint8 dht_readat()
{
    uint8 i=0, dat=0;


    for(i=0; i<8; i++)
    {
        dht_num = 2;
        while((GPIO_INPUT_GET(12) == 0) && (dht_num ++));
        os_delay_us(40);
        dat = dat << 1;
        if(GPIO_INPUT_GET(12) == 1)
        {
            dht_num = 2;
            dat = dat | 0x01;
            while((GPIO_INPUT_GET(12) == 1) && (dht_num ++));
        }
    }

    return dat;
}

void dht_getdat()
{
    uint8 i=0;
    
    gpio_output_set(0, BIT12, BIT12,0);
    os_delay_us(40000);//40000
    gpio_output_set(BIT12, 0, 0,BIT12);

    os_delay_us(40);
    gpio_output_set(BIT12, 0, 0,BIT12);

    if(GPIO_INPUT_GET(14) == 0)
    {
        dht_num = 2;
        while((GPIO_INPUT_GET(12) == 0) && (dht_num ++));
        dht_num = 2;
        while((GPIO_INPUT_GET(12) == 1) && (dht_num ++));
        dht_d1 = dht_readat();
        dht_d2 = dht_readat();
        dht_t1 = dht_readat();
        dht_t2 = dht_readat();
        dht_chk = dht_readat();
    }
    gpio_output_set(BIT12, 0, 0,BIT12);
    //os_delay_us(50000);

}

void task2(void *pvParameters)
{
    uint8 num;
    char str[4];

    portTickType xLastWakeTime;
    MQTT_Client* client = pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    for(;;)
    {
        dht_getdat();
        num = dht_d1 + dht_d2 + dht_t1 + dht_t2;
        if(num == dht_chk)
        {
            sprintf(str, "%d", dht_t1);
            os_printf("the string is %s\n", str);
            MQTT_Publish(client, "OID/hdw", str, strlen(str), 0, 1);
        }
        // system_print_meminfo();
        vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_RATE_MS);
    }
    vTaskDelete(NULL);
}

void mqttConnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    os_printf("MQTT: Connected\r\n");

    MQTT_Publish(client, "OID/app", "hello2", 6, 0, 1);
    MQTT_Subscribe(client, "OID/app", 0);

    // xTaskCreate(task2, "Task2", 512, client, 2, NULL);
}

void mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    os_printf("MQTT: Disconnected\r\n");
}

void mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    os_printf("MQTT: Published\r\n");
}

void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    // int n;

    // char *topicBuf = (char*)zalloc(topic_len+1),
    //      *dataBuf = (char*)zalloc(data_len+1);

    MQTT_Client* client = (MQTT_Client*)args;

    // memcpy(topicBuf, topic, topic_len);
    // topicBuf[topic_len] = 0;

    // memcpy(dataBuf, data, data_len);
    // dataBuf[data_len] = 0;

    // n = atoi(dataBuf);
//  pwm_set_duty(n,0);
//  pwm_start();

    if(strncmp(data, data_led_on, 12) == 0)
    {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(4), 1);
        GPIO_OUTPUT_SET(GPIO_ID_PIN(5), 1);
    }
    else if(strncmp(data, data_led_off, 13) == 0)
    {
        GPIO_OUTPUT_SET(GPIO_ID_PIN(4), 0);
        GPIO_OUTPUT_SET(GPIO_ID_PIN(5), 0);
    }

    // os_printf("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
    // os_printf("data len is : %d , data is :%d\r\n",data_len, (int)*data);

    // free(topicBuf);
    // free(dataBuf);
}

// void wifiConnectCb(uint8_t status)
// {
//     if((status == STATION_GOT_IP)){
// /*************************************************
//  *ssl connect
//  ************************************************/
// //      espconn_secure_ca_enable(0x01,0x3B);
// //      espconn_secure_cert_req_enable(0x01,0x3A);
//         printf("got ip!\n");
//         MQTT_Connect(&mqttClient);
//     } else {
//         MQTT_Disconnect(&mqttClient);
//     }
// }


void task1(void *pvParameters)
{
    int ret=0;
    struct ip_info ipconfig;

    do{
        wifi_get_ip_info(STATION_IF, &ipconfig);
        vTaskDelay(500 / portTICK_RATE_MS);
    }while(ipconfig.ip.addr == 0);
    
    //xTaskCreate(task2, "Task2", 512, NULL, 3, NULL);

    MQTT_InitConnection(&mqttClient, "101.200.207.137", 1883, 0);
    MQTT_InitClient(&mqttClient, "10066", "10088", "123456", 30, 1);
    MQTT_InitLWT(&mqttClient, "/lwt", "offline", 2, 1);
    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);
    MQTT_OnData(&mqttClient, mqttDataCb);

    // espconn_secure_ca_enable(0x01,0x3B);
    // espconn_secure_cert_req_enable(0x01,0x3A);
    MQTT_Connect(&mqttClient);
    while(1);

    vTaskDelete(NULL);
}


/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
    struct station_config *sta_config = (struct station_config *)malloc(sizeof(struct station_config));

    os_printf("meiSDK version:%s\n", system_get_sdk_version());

    wifi_set_opmode(STATION_MODE);
    bzero(sta_config, sizeof(struct station_config));
    sprintf(sta_config->ssid, "xtyk");
    sprintf(sta_config->password, "xtyk88888");
    wifi_station_set_config(sta_config);
    free(sta_config);

    //DHT11 -- gpio12
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    gpio_output_set(BIT12, 0, 0,BIT12);//1
    os_delay_us(50000);

    //led -- gpio4,gpio5
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(4), 0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
    GPIO_OUTPUT_SET(GPIO_ID_PIN(5), 0);

    xTaskCreate(task1, "task1", 512, NULL, 1, NULL);
    //xTaskCreate(task2, "Task2", 512, NULL, 1, NULL);
}
