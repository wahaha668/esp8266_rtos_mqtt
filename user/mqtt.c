/* mqtt.c
*  Protocol: http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

//#include "user_interface.h"
//#include "osapi.h"
#include "espressif/espconn.h"
#include "espressif/esp_wifi.h"
#include "espressif/esp_common.h"
#include "espressif/c_types.h"
#include "user_config.h"
#include "mqtt.h"
#include "queue.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "gpio.h"

#define MQTT_TASK_PRIO        		1
#define MQTT_TASK_QUEUE_SIZE    	5
#define MQTT_SEND_TIMOUT			5

#ifndef QUEUE_BUFFER_SIZE
#define QUEUE_BUFFER_SIZE		 	1024//2048
#endif

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;

// uint8 dht_t1=0, dht_t2=0;
// uint8 dht_d1=0, dht_d2=0;
// uint8 dht_chk=0, dht_num=0;

//os_event_t mqtt_procTaskQueue[MQTT_TASK_QUEUE_SIZE];
xQueueHandle mqtt_procTaskQueue;
xTaskHandle  mqtt_task_hdl;

// uint8 dht_readat()
// {
//     uint8 i=0, dat=0;


//     for(i=0; i<8; i++)
//     {
//         dht_num = 2;
//         while((GPIO_INPUT_GET(12) == 0) && (dht_num ++));
//         os_delay_us(40);
//         dat = dat << 1;
//         if(GPIO_INPUT_GET(12) == 1)
//         {
//             dht_num = 2;
//             dat = dat | 0x01;
//             while((GPIO_INPUT_GET(12) == 1) && (dht_num ++));
//         }
//     }

//     return dat;
// }

// void dht_getdat()
// {
//     uint8 i=0;
    
//     gpio_output_set(0, BIT12, BIT12,0);
//     os_delay_us(40000);
//     gpio_output_set(BIT12, 0, 0,BIT12);

//     os_delay_us(40);
//     gpio_output_set(BIT12, 0, 0,BIT12);

//     if(GPIO_INPUT_GET(14) == 0)
//     {
//         dht_num = 2;
//         while((GPIO_INPUT_GET(12) == 0) && (dht_num ++));
//         dht_num = 2;
//         while((GPIO_INPUT_GET(12) == 1) && (dht_num ++));
//         dht_d1 = dht_readat();
//         dht_d2 = dht_readat();
//         dht_t1 = dht_readat();
//         dht_t2 = dht_readat();
//         dht_chk = dht_readat();
//     }
//     gpio_output_set(BIT12, 0, 0,BIT12);
//     os_delay_us(50000);

// }

// void task2(void *pvParameters)
// {
//     uint8 num;
//     char *str;

//     MQTT_Client* client = pvParameters;

//     os_printf("task2 coming !\n");
//     for(;;)
//     {
//         dht_getdat();
//         os_printf("task2!\n");
//         num = dht_d1 + dht_d2 + dht_t1 + dht_t2;
//         if(num == dht_chk)
//         {
//             strcpy(str, (char *)&dht_t1);
//             os_printf("the string is %s\n", str);
//             MQTT_Publish(client, "OID/app", str, strlen(str), 0, 1);
//         }
//         vTaskDelay(1000 / portTICK_RATE_MS);
//     }
//     vTaskDelete(NULL);
// }

LOCAL void
mqtt_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
	//int *evt = (int*)malloc(sizeof(int));
	struct espconn *pConn = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pConn->reserve;


	if(ipaddr == NULL)
	{
		os_printf("DNS: Found, but got no ip, try to reconnect\r\n");
		client->connState = TCP_RECONNECT_REQ;
		return;
	}

	os_printf("DNS: found ip %d.%d.%d.%d\n",
			*((uint8 *) &ipaddr->addr),
			*((uint8 *) &ipaddr->addr + 1),
			*((uint8 *) &ipaddr->addr + 2),
			*((uint8 *) &ipaddr->addr + 3));

	if(client->ip.addr == 0 && ipaddr->addr != 0)
	{
		memcpy(client->pCon->proto.tcp->remote_ip, &ipaddr->addr, 4);
		if(client->security){
			//espconn_secure_connect(client->pCon);
		}
		else {
			espconn_connect(client->pCon);
		}

		client->connState = TCP_CONNECTING;
		os_printf("TCP: connecting...\r\n");
	}

	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdPASS)//pdTRUE
		os_printf("mqtt_queue is full\n");
}



LOCAL void
deliver_publish(MQTT_Client* client, uint8_t* message, int length)
{
	mqtt_event_data_t event_data;

	event_data.topic_length = length;
	event_data.topic = mqtt_get_publish_topic(message, &event_data.topic_length);
	event_data.data_length = length;
	event_data.data = mqtt_get_publish_data(message, &event_data.data_length);

	if(client->dataCb)
		client->dataCb((uint32_t*)client, event_data.topic, event_data.topic_length, event_data.data, event_data.data_length);

}


/**
  * @brief  Client received callback function.
  * @param  arg: contain the ip link information
  * @param  pdata: received data
  * @param  len: the lenght of received data
  * @retval None
  */
void 
mqtt_tcpclient_recv(void *arg, char *pdata, unsigned short len)
{
	//int *evt = (int*)malloc(sizeof(int));
	uint8_t msg_type;
	uint8_t msg_qos;
	uint16_t msg_id;

	struct espconn *pCon = (struct espconn*)arg;
	MQTT_Client *client = (MQTT_Client *)pCon->reserve;

READPACKET:
	os_printf("TCP: data received %d bytes\r\n", len);
	if(len < MQTT_BUF_SIZE && len > 0){
		memcpy(client->mqtt_state.in_buffer, pdata, len);

		msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
		msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
		msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
		switch(client->connState){
		case MQTT_CONNECT_SENDING:
			if(msg_type == MQTT_MSG_TYPE_CONNACK){
				if(client->mqtt_state.pending_msg_type != MQTT_MSG_TYPE_CONNECT){
					os_printf("MQTT: Invalid packet\r\n");
					if(client->security){
						//espconn_secure_disconnect(client->pCon);
					}
					else {
						espconn_disconnect(client->pCon);
					}
				} else {
					os_printf("MQTT: Connected to %s:%d\r\n", client->host, client->port);
					client->connState = MQTT_DATA;
					if(client->connectedCb)
						client->connectedCb((uint32_t*)client);
				}

			}
			break;
		case MQTT_DATA:
			client->mqtt_state.message_length_read = len;
			client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);


			switch(msg_type)
			{

			  case MQTT_MSG_TYPE_SUBACK:
				if(client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
				  os_printf("MQTT: Subscribe successful\r\n");
				break;
			  case MQTT_MSG_TYPE_UNSUBACK:
				if(client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
				  os_printf("MQTT: UnSubscribe successful\r\n");
				break;
			  case MQTT_MSG_TYPE_PUBLISH:
				if(msg_qos == 1)
					client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
				else if(msg_qos == 2)
					client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
				if(msg_qos == 1 || msg_qos == 2){
					os_printf("MQTT: Queue response QoS: %d\r\n", msg_qos);
					if(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
						os_printf("MQTT: Queue full\r\n");
					}
				}

				deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
				break;
			  case MQTT_MSG_TYPE_PUBACK:
				if(client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id){
				  os_printf("MQTT: received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish\r\n");
				}

				break;
			  case MQTT_MSG_TYPE_PUBREC:
				  client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
				  if(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
				  	os_printf("MQTT: Queue full\r\n");
				  }
				break;
			  case MQTT_MSG_TYPE_PUBREL:
				  client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
				  if(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
					os_printf("MQTT: Queue full\r\n");
				  }
				break;
			  case MQTT_MSG_TYPE_PUBCOMP:
				if(client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id){
				  os_printf("MQTT: receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish\r\n");
				}
				break;
			  case MQTT_MSG_TYPE_PINGREQ:
				  client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
				  if(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
					os_printf("MQTT: Queue full\r\n");
				  }
				break;
			  case MQTT_MSG_TYPE_PINGRESP:
				// Ignore
				break;
			}
			// NOTE: this is done down here and not in the switch case above
			// because the PSOCK_READBUF_LEN() won't work inside a switch
			// statement due to the way protothreads resume.
			if(msg_type == MQTT_MSG_TYPE_PUBLISH)
			{
			  len = client->mqtt_state.message_length_read;

			  if(client->mqtt_state.message_length < client->mqtt_state.message_length_read)
			  {
				  //client->connState = MQTT_PUBLISH_RECV;
				  //Not Implement yet
				  len -= client->mqtt_state.message_length;
				  pdata += client->mqtt_state.message_length;

				  os_printf("Get another published message\r\n");
				  goto READPACKET;
			  }

			}
			break;
		}
	} else {
		os_printf("ERROR: Message too long\r\n");
	}
	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
}

/**
  * @brief  Client send over callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void 
mqtt_tcpclient_sent_cb(void *arg)
{
	//int *evt = (int*)malloc(sizeof(int));
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reserve;
	os_printf("TCP: Sent\r\n");
	client->sendTimeout = 0;
	if(client->connState == MQTT_DATA && client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH){
		if(client->publishedCb)
			client->publishedCb((uint32_t*)client);
	}
	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
}

void
mqtt_timer(void *pvParameters)
{
	//int *evt = (int*)malloc(sizeof(int));
	//MQTT_Client* client = (MQTT_Client*)arg;
	MQTT_Client* client = (MQTT_Client*)pvParameters;
	portTickType xLastWakeTime;

	xLastWakeTime = xTaskGetTickCount();

	for(;;)
	{
		if(client->connState == MQTT_DATA){
			client->keepAliveTick ++;
			if(client->keepAliveTick > client->mqtt_state.connect_info->keepalive){

				os_printf("\r\nMQTT: Send keepalive packet to %s:%d!\r\n", client->host, client->port);
				client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
				client->mqtt_state.pending_msg_type = MQTT_MSG_TYPE_PINGREQ;
				client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
				client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);


				client->sendTimeout = MQTT_SEND_TIMOUT;
				os_printf("MQTT: Sending, type: %d, id: %04X\r\n",client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
				if(client->security){
					//espconn_secure_sent(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
				}
				else{
					espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
				}

				client->mqtt_state.outbound_message = NULL;

				client->keepAliveTick = 0;
				//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
				//evt = (int *)client;
				if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
					os_printf("mqtt_queue is full\n");
			}

		}else if(client->connState == TCP_RECONNECT_REQ){
			client->reconnectTick ++;
			if(client->reconnectTick > MQTT_RECONNECT_TIMEOUT) {
				client->reconnectTick = 0;
				client->connState = TCP_RECONNECT;
				//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
				//evt = (int *)client;
				if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
					os_printf("mqtt_queue is full\n");
			}
		}
		if(client->sendTimeout > 0)
			client->sendTimeout --;

		vTaskDelayUntil(&xLastWakeTime, 1000 / portTICK_RATE_MS);
	}
	
}

void 
mqtt_tcpclient_discon_cb(void *arg)
{
	//int *evt = (int*)malloc(sizeof(int));

	struct espconn *pespconn = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pespconn->reserve;
	os_printf("TCP: Disconnected callback\r\n");
	client->connState = TCP_RECONNECT_REQ;
	if(client->disconnectedCb)
		client->disconnectedCb((uint32_t*)client);

	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
}



/**
  * @brief  Tcp client connect success callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void 
mqtt_tcpclient_connect_cb(void *arg)
{
	//int *evt = (int*)malloc(sizeof(int));
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reserve;

	espconn_regist_disconcb(client->pCon, mqtt_tcpclient_discon_cb);
	espconn_regist_recvcb(client->pCon, mqtt_tcpclient_recv);
	espconn_regist_sentcb(client->pCon, mqtt_tcpclient_sent_cb);
	os_printf("MQTT: Connected to broker %s:%d\r\n", client->host, client->port);

	mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer, client->mqtt_state.out_buffer_length);
	client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection, client->mqtt_state.connect_info);
	client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
	client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);


	client->sendTimeout = MQTT_SEND_TIMOUT;
	os_printf("MQTT: Sending, type: %d, id: %04X\r\n",client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
	if(client->security){
		//espconn_secure_sent(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
	}
	else{
		espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
	}

	client->mqtt_state.outbound_message = NULL;
	client->connState = MQTT_CONNECT_SENDING;
	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
}

/**
  * @brief  Tcp client connect repeat callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void 
mqtt_tcpclient_recon_cb(void *arg, sint8 errType)
{
	//int *evt = (int*)malloc(sizeof(int));
	struct ip_info *info;
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reserve;

	if(wifi_get_ip_info(STATION_IF, info))
		os_printf("get ip success!\n");
	else
	{
		os_printf("get ip fail!\n");
		wifi_station_disconnect();
		os_delay_us(50000);
		wifi_station_connect();
		while(!wifi_get_ip_info(STATION_IF, info))
		{
			os_delay_us(10000);
		}
	}



	os_printf("TCP: Reconnect to %s:%d\r\n", client->host, client->port);

	client->connState = TCP_RECONNECT_REQ;

	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");

}

/**
  * @brief  MQTT publish function.
  * @param  client: 	MQTT_Client reference
  * @param  topic: 		string topic will publish to
  * @param  data: 		buffer data send point to
  * @param  data_length: length of data
  * @param  qos:		qos
  * @param  retain:		retain
  * @retval TRUE if success queue
  */
BOOL 
MQTT_Publish(MQTT_Client *client, const char* topic, const char* data, int data_length, int qos, int retain)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
	//int *evt = (int*)malloc(sizeof(int));

	client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
										 topic, data, data_length,
										 qos, retain,
										 &client->mqtt_state.pending_msg_id);
	if(client->mqtt_state.outbound_message->length == 0){
		os_printf("MQTT: Queuing publish failed\r\n");
		return FALSE;
	}
	os_printf("MQTT: queuing publish, length: %d, queue size(%d/%d)\r\n", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size);
	// while(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
	// 	os_printf("MQTT: Queue full\r\n");
	// 	if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
	// 		os_printf("MQTT: Serious buffer error\r\n");
	// 		return FALSE;
	// 	}
	// }

	while(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
		os_printf("MQTT: Queue full\r\n");
		if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			os_printf("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	
	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");

	return TRUE;
}

/**
  * @brief  MQTT subscibe function.
  * @param  client: 	MQTT_Client reference
  * @param  topic: 		string topic will subscribe
  * @param  qos:		qos
  * @retval TRUE if success queue
  */
BOOL 
MQTT_Subscribe(MQTT_Client *client, char* topic, uint8_t qos)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
	//int *evt = (int*)malloc(sizeof(int));

	client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
											topic, 0,
											&client->mqtt_state.pending_msg_id);
	os_printf("MQTT: queue subscribe, topic\"%s\", id: %d\r\n",topic, client->mqtt_state.pending_msg_id);
	while(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
		os_printf("MQTT: Queue full\r\n");
		if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			os_printf("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	//system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);

	//evt = (int *)client;
	if(xQueueSend(mqtt_procTaskQueue, client, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
	return TRUE;
}

void 
MQTT_Task(void *pvParameters)
{
	uint32_t *sig;
	MQTT_Client* client = (MQTT_Client *)pvParameters;
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
	os_printf("mqtt task coming!\n");

	//sig = (uint32_t *)malloc(sizeof(MQTT_Client));
	//client = (MQTT_Client *)malloc(sizeof(MQTT_Client));

	for(;;)
	{
		if(pdTRUE == xQueueReceive(mqtt_procTaskQueue, client, 0))//(portTickType)portMAX_DELAY)
		{
			//printf("suspend all! client->connState is : %d\n", client->connState);
			//vTaskSuspendAll();
			
			if(client == NULL)
				return;
			//printf("mqtt task coming!\n");
			//MQTT_Client* client = (MQTT_Client*)m_client;

			switch(client->connState){
				case TCP_RECONNECT_REQ:
					break;
				case TCP_RECONNECT:
					MQTT_Connect(client);
					os_printf("TCP: Reconnect to: %s:%d\r\n", client->host, client->port);
					client->connState = TCP_CONNECTING;
					break;
				case MQTT_DATA:
					if(QUEUE_IsEmpty(&client->msgQueue) || client->sendTimeout != 0) {
						break;
					}
					if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == 0){
						client->mqtt_state.pending_msg_type = mqtt_get_type(dataBuffer);
						client->mqtt_state.pending_msg_id = mqtt_get_id(dataBuffer, dataLen);


						client->sendTimeout = MQTT_SEND_TIMOUT;
						os_printf("MQTT: Sending, type: %d, id: %04X\r\n",client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
						if(client->security){
							//espconn_secure_sent(client->pCon, dataBuffer, dataLen);
						}
						else{
							espconn_send(client->pCon, dataBuffer, dataLen);
						}

						client->mqtt_state.outbound_message = NULL;
						break;
					}
					break;
			}
			//free(sig);
			//xTaskResumeAll();
		}
		vTaskDelay(100 / portTICK_RATE_MS);
	}

	free(client);
	vTaskDelete(NULL);
}

/**
  * @brief  MQTT initialization connection function
  * @param  client: 	MQTT_Client reference
  * @param  host: 	Domain or IP string
  * @param  port: 	Port to connect
  * @param  security:		1 for ssl, 0 for none
  * @retval None
  */
void 
MQTT_InitConnection(MQTT_Client *mqttClient, uint8_t* host, uint32 port, uint8_t security)
{
	uint32_t temp;
	os_printf("MQTT_InitConnection\r\n");
	memset(mqttClient, 0, sizeof(MQTT_Client));
	temp = strlen(host);
	mqttClient->host = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->host, host);
	mqttClient->host[temp] = 0;
	mqttClient->port = port;
	mqttClient->security = security;

}

/**
  * @brief  MQTT initialization mqtt client function
  * @param  client: 	MQTT_Client reference
  * @param  clientid: 	MQTT client id
  * @param  client_user:MQTT client user
  * @param  client_pass:MQTT client password
  * @param  client_pass:MQTT keep alive timer, in second
  * @retval None
  */
void 
MQTT_InitClient(MQTT_Client *mqttClient, uint8_t* client_id, uint8_t* client_user, uint8_t* client_pass, uint32_t keepAliveTime, uint8_t cleanSession)
{
	//int *evt = (int*)malloc(10*sizeof(int));
	uint32_t temp;
	os_printf("MQTT_InitClient\r\n");

	memset(&mqttClient->connect_info, 0, sizeof(mqtt_connect_info_t));

	temp = strlen(client_id);
	mqttClient->connect_info.client_id = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->connect_info.client_id, client_id);
	mqttClient->connect_info.client_id[temp] = 0;

	temp = strlen(client_user);
	mqttClient->connect_info.username = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->connect_info.username, client_user);
	mqttClient->connect_info.username[temp] = 0;

	temp = strlen(client_pass);
	mqttClient->connect_info.password = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->connect_info.password, client_pass);
	mqttClient->connect_info.password[temp] = 0;


	mqttClient->connect_info.keepalive = keepAliveTime;
	mqttClient->connect_info.clean_session = cleanSession;

	mqttClient->mqtt_state.in_buffer = (uint8_t *)zalloc(MQTT_BUF_SIZE);
	mqttClient->mqtt_state.in_buffer_length = MQTT_BUF_SIZE;
	mqttClient->mqtt_state.out_buffer =  (uint8_t *)zalloc(MQTT_BUF_SIZE);
	mqttClient->mqtt_state.out_buffer_length = MQTT_BUF_SIZE;
	mqttClient->mqtt_state.connect_info = &mqttClient->connect_info;

	mqtt_msg_init(&mqttClient->mqtt_state.mqtt_connection, mqttClient->mqtt_state.out_buffer, mqttClient->mqtt_state.out_buffer_length);

	//QUEUE_Init(&mqttClient->msgQueue, QUEUE_BUFFER_SIZE);

//	system_os_task(MQTT_Task, MQTT_TASK_PRIO, mqtt_procTaskQueue, MQTT_TASK_QUEUE_SIZE);
//	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
	mqtt_procTaskQueue = xQueueCreate(MQTT_TASK_QUEUE_SIZE, sizeof(MQTT_Client)); // sizeof(void *)
	if(mqtt_procTaskQueue != NULL)
		os_printf("tast queue create ok!\n");

	//if(xTaskCreate(MQTT_Task, "MQTT_Task", 512, NULL, MQTT_TASK_PRIO, NULL) == pdTRUE)
	//	printf("mqtt task create ok!\n");
	xTaskCreate(MQTT_Task, "MQTT_Task", 512, mqttClient, 4, NULL);

	//evt = (int *)mqttClient;
	if(xQueueSend(mqtt_procTaskQueue, mqttClient, 0) != pdTRUE)
		os_printf("mqtt_queue is full\n");
}

void 
MQTT_InitLWT(MQTT_Client *mqttClient, uint8_t* will_topic, uint8_t* will_msg, uint8_t will_qos, uint8_t will_retain)
{
	uint32_t temp;
	temp = strlen(will_topic);
	mqttClient->connect_info.will_topic = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->connect_info.will_topic, will_topic);
	mqttClient->connect_info.will_topic[temp] = 0;

	temp = strlen(will_msg);
	mqttClient->connect_info.will_message = (uint8_t*)zalloc(temp + 1);
	strcpy(mqttClient->connect_info.will_message, will_msg);
	mqttClient->connect_info.will_message[temp] = 0;


	mqttClient->connect_info.will_qos = will_qos;
	mqttClient->connect_info.will_retain = will_retain;
}
/**
  * @brief  Begin connect to MQTT broker
  * @param  client: MQTT_Client reference
  * @retval None
  */
void 
MQTT_Connect(MQTT_Client *mqttClient)
{
	sint8 ret=2;
	MQTT_Disconnect(mqttClient);
	mqttClient->pCon = (struct espconn *)zalloc(sizeof(struct espconn));
	mqttClient->pCon->type = ESPCONN_TCP;
	mqttClient->pCon->state = ESPCONN_NONE;
	mqttClient->pCon->proto.tcp = (esp_tcp *)zalloc(sizeof(esp_tcp));
	mqttClient->pCon->proto.tcp->local_port = espconn_port();
	mqttClient->pCon->proto.tcp->remote_port = mqttClient->port;
	mqttClient->pCon->reserve = mqttClient;
	espconn_regist_connectcb(mqttClient->pCon, mqtt_tcpclient_connect_cb);
	espconn_regist_reconcb(mqttClient->pCon, mqtt_tcpclient_recon_cb);

	mqttClient->keepAliveTick = 0;
	mqttClient->reconnectTick = 0;

	// os_timer_disarm(&mqttClient->mqttTimer);
	// os_timer_setfn(&mqttClient->mqttTimer, (os_timer_func_t *)mqtt_timer, mqttClient);
	// os_timer_arm(&mqttClient->mqttTimer, 1000, 1);
	xTaskCreate(mqtt_timer, "mqtt_timer", 512, mqttClient, 5, NULL);

	if(UTILS_StrToIP(mqttClient->host, &mqttClient->pCon->proto.tcp->remote_ip)) {
		os_printf("TCP: Connect to ip  %s:%d\r\n", mqttClient->host, mqttClient->port);
		if(mqttClient->security){
//			ret = espconn_secure_connect(mqttClient->pCon);
//			if(ret == 0)
//				printf("ret is 0!!!OK : %d\r\n",ret);
//			else
//				printf("ret is %d\r\n",ret);

//			ret = espconn_secure_send(mqttClient->pCon,"mei",4);
//			if(ret == 0)
//				printf("sen is OK!\r\n");
//			else
//				printf("send fail!\r\n");
		}
		else {
			espconn_connect(mqttClient->pCon);
		}
	}
	else {
		os_printf("TCP: Connect to domain %s:%d\r\n", mqttClient->host, mqttClient->port);
		espconn_gethostbyname(mqttClient->pCon, mqttClient->host, &mqttClient->ip, mqtt_dns_found);
	}
	mqttClient->connState = TCP_CONNECTING;
}

void 
MQTT_Disconnect(MQTT_Client *mqttClient)
{
	if(mqttClient->pCon){
		os_printf("Free memory\r\n");
		if(mqttClient->pCon->proto.tcp)
			free(mqttClient->pCon->proto.tcp);
		free(mqttClient->pCon);
		mqttClient->pCon = NULL;
	}

	os_timer_disarm(&mqttClient->mqttTimer);
}
void 
MQTT_OnConnected(MQTT_Client *mqttClient, MqttCallback connectedCb)
{
	mqttClient->connectedCb = connectedCb;
}

void 
MQTT_OnDisconnected(MQTT_Client *mqttClient, MqttCallback disconnectedCb)
{
	mqttClient->disconnectedCb = disconnectedCb;
}

void 
MQTT_OnData(MQTT_Client *mqttClient, MqttDataCallback dataCb)
{
	mqttClient->dataCb = dataCb;
}

void 
MQTT_OnPublished(MQTT_Client *mqttClient, MqttCallback publishedCb)
{
	mqttClient->publishedCb = publishedCb;
}
