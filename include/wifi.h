/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
//#include "os_type.h"
#include "stdint.h"
#include "c_types.h"

typedef void (*WifiCallback)(uint8_t);
void WIFI_Connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb);


#endif /* USER_WIFI_H_ */
