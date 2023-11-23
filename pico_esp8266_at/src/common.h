#ifndef MAGB_COMMON_H
#define MAGB_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hardware/uart.h"
#include "hardware/irq.h"
#include <mobile.h>
#include <mobile_inet.h>

//Flash Config
#define FLASH_DATA_SIZE (FLASH_PAGE_SIZE * 3)
#define MOBILE_MAX_DATA_SIZE 0xFF

// SPI pins
#define SPI_PORT        spi0
#define SPI_BAUDRATE_512    64 * 1024 * 8
#define SPI_BAUDRATE_256    32 * 1024 * 8
#define PIN_SPI_SIN     16
#define PIN_SPI_SCK     18
#define PIN_SPI_SOUT    19 
volatile bool spiLock = false;
uint8_t buff32[4];
volatile int8_t buff32_pointer = 0;

//UART pins
#define UART_ID uart1
#define BAUD_RATE 115200
#define UART_TX_PIN 4
#define UART_RX_PIN 5

// Control the configs on Flash
bool haveWifiConfig = false;

//LED Config
#define LED_PIN       		  	25
#define LED_SET(A)    		  	(gpio_put(LED_PIN, (A)))
#define LED_ON        		  	LED_SET(true)
#define LED_OFF       		  	LED_SET(false)
#define LED_TOGGLE    		  	(gpio_put(LED_PIN, !gpio_get(LED_PIN)))

//Time Config
#define MKS(A)                  (A)
#define MS(A)                   ((A) * 1000)
#define SEC(A)                  ((A) * 1000 * 1000)
volatile uint64_t time_us_now = 0;
uint64_t last_readable = 0;

//Control Bools
bool isESPDetected = false;
bool haveConfigToWrite = false;
bool isServerOpened = false;
bool is32bitsMode = false;

//UART RX Buffer Config
#define BUFF_AT_SIZE 9000 //2048 is the maximun you can receive from esp01
uint8_t buffATrx[BUFF_AT_SIZE+64] = {0}; // + extra bytes to hold the AT command answer echo
int buffATrx_pointer = 0;
uint8_t buffRecData[BUFF_AT_SIZE] = {0};
int buffRecData_pointer = 0;
int ipdVal[5] = {0,0,0,0,0};

//Wifi and Flash Configs Default
bool isConnectedWiFi = false;
char WiFiSSID[28] = "WiFi_Network";
char WiFiPASS[28] = "P@$$w0rd";

struct esp_sock_config {
    int host_id;
    uint8_t host_type; //0=NONE, 1=TCP or 2=UDP
    enum mobile_addrtype host_iptype; //IPV4, IPV6 or NONE
    int local_port;
    bool sock_status;
};

struct mobile_user {
    struct mobile_adapter *adapter;
    enum mobile_action action;
	unsigned long esp_clock_latch[MOBILE_MAX_TIMERS];
    uint8_t config_eeprom[FLASH_DATA_SIZE];
    struct esp_sock_config esp_sockets[MOBILE_MAX_CONNECTIONS];
    char number_user[MOBILE_MAX_NUMBER_SIZE + 1];
    char number_peer[MOBILE_MAX_NUMBER_SIZE + 1];
};
struct mobile_user *mobile;

// C Funciton to replace strcmp. Necessary to compare strings if the buffer have a 0x00 byte.
void *memmem(const void *l, size_t l_len, const void *s, size_t s_len){
	register char *cur, *last;
	const char *cl = (const char *)l;
	const char *cs = (const char *)s;

	/* we need something to compare */
	if (l_len == 0 || s_len == 0)
		return NULL;

	/* "s" must be smaller or equal to "l" */
	if (l_len < s_len)
		return NULL;

	/* special case where s_len == 1 */
	if (s_len == 1)
		return memchr(l, (int)*cs, l_len);

	/* the last position where its possible to find "s" in "l" */
	last = (char *)cl + l_len - s_len;

	for (cur = (char *)cl; cur <= last; cur++)
		if (cur[0] == cs[0] && memcmp(cur, cs, s_len) == 0)
			return cur;

	return NULL;
}

// Find a string into a buffer
bool FindCommand(char * buf, char * target){
    if(strstr(buf, target) != NULL){
        return true;
    }
    return false;
} 

void parse_addr_string(struct mobile_addr *src, char *dest){
    struct mobile_addr4 *addr4 = (struct mobile_addr4 *)src;
    struct mobile_addr6 *addr6 = (struct mobile_addr6 *)src;

    char tmpaddr[60] = {0};

    switch (src->type) {
        case MOBILE_ADDRTYPE_IPV4:            
            sprintf(tmpaddr,"%i.%i.%i.%i:%i\0", addr4->host[0], addr4->host[1], addr4->host[2], addr4->host[3], addr4->port);
            break;
        case MOBILE_ADDRTYPE_IPV6:
            sprintf(tmpaddr,"[%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx:%02hhx%02hhx]:%i\0",
                    addr6->host[0],addr6->host[1], 
                    addr6->host[2],addr6->host[3], 
                    addr6->host[4],addr6->host[5], 
                    addr6->host[6],addr6->host[7], 
                    addr6->host[8],addr6->host[9], 
                    addr6->host[10],addr6->host[11], 
                    addr6->host[12],addr6->host[13], 
                    addr6->host[14],addr6->host[15], 
                    addr6->port);
            break;
        default:
            sprintf(tmpaddr,"No server defined.");
            break;
    }
    memcpy(dest,tmpaddr,sizeof(tmpaddr));
}

#endif