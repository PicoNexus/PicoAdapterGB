#ifndef ESP_AT_H
#define ESP_AT_H

#include "common.h"

bool use_uart0 = true;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Basics AT functions

// Run a little timeout instead sleep (to prevent any block to the RX interrupt)
// void Delay_Timer(int timeout){
//     volatile uint64_t timenow = 0;
//     volatile uint64_t last_read = 0;
//     timenow = time_us_64();
//     last_read = timenow;
//     while ((timenow - last_read) < timeout){
//         timenow = time_us_64();
//     }
// }

// RX interrupt handler
void on_uart_rx(){
    while (uart_is_readable(use_uart0 ? uart0 : uart1)) {
        char ch = uart_getc(use_uart0 ? uart0 : uart1);
        buffATrx[buffATrx_pointer++] = ch;
        if (buffATrx_pointer >= sizeof(buffATrx)){
            buffATrx_pointer = 0;
        }
    }
}

// Clean the RX buffer
void FlushATBuff(){
    //printf("!! test\n");
    buffATrx_pointer=0;
    memset(buffATrx,'\0',sizeof(buffATrx));
}

// Find a string into the RX buffer
bool ESP_SerialFind(char * buf, char * target, int ms_timeout, bool cleanBuff, bool useMemFind){
    volatile uint64_t timenow = time_us_64();
    volatile uint64_t last_read = timenow;

    while ((timenow - last_read) < ms_timeout){
        timenow = time_us_64();
        if(useMemFind){
            if(memmem(buf, sizeof(buffATrx), target, strlen(target)+1) != NULL){
                if(cleanBuff) FlushATBuff();
                return true;
            }
        }else{
            if(strstr(buf, target) != NULL){
                if(cleanBuff) FlushATBuff();
                return true;
            }
        }
    }
    if(cleanBuff) FlushATBuff();
    return false;
} 

// Send a AT command to the ESP
void ESP_SendCmd(uart_inst_t *uart, const uint8_t *command, int datasize){
    char cmd[100] = {};
    int cmdsize = 0;
    memset(cmd,'\0',sizeof(cmd));

    if(datasize == 0){
        sprintf(cmd,"%s\r\n",command);   
        cmdsize = strlen(cmd);
    }else{
        cmdsize = datasize;
        memcpy(cmd,command,cmdsize);
    }

    uart_write_blocking(uart, cmd ,cmdsize);
}

// Return the command index inside the RX buffer (useful to get data from the RX to a variable)
int ESP_GetCmdIndexBuffer(char * rxbuff, char * cmdSearch){
    int offset = 0;
    //char buf_cpy[strlen(buf)];
    char buf_cpy[sizeof(buffATrx)];
    strncpy(buf_cpy, rxbuff+offset, sizeof(buffATrx)-offset);
    char *p = strstr(buf_cpy, cmdSearch);
    if (p)
        return p - buf_cpy+offset;
    return -1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Read the how much data remaining inside ESP buffer (only TCP)
int ESP_ReadBuffSize(uart_inst_t * uart, uint8_t connID){
    int tmp_ipd[5] = {0,0,0,0,0};
    FlushATBuff();
    ESP_SendCmd(uart,"AT+CIPRECVLEN?",0);
    if(ESP_SerialFind(buffATrx,"+CIPRECVLEN:",SEC(2),false,false)){
        busy_wait_us(MS(100));
        uint8_t connID_pointer = 0;
        char resp[50];
        memcpy(resp,buffATrx,strlen(buffATrx));
        // Extract the first token
        char * token = strtok(resp, ":");
        // loop through the string to extract all other tokens
        while(token != NULL) {
            if(strcmp(token, "+CIPRECVLEN") != 0){
                tmp_ipd[connID_pointer++] = atoi(token);
            }
            token = strtok(NULL, ",");
        }
        FlushATBuff();
        printf("ESP-01 Read Buffer %i Lenght: %i bytes\n",connID, tmp_ipd[connID]);
        return tmp_ipd[connID]; 
    }
    printf("ESP-01 Read Buffer Lenght: ERROR\n"); 
    FlushATBuff();
    return 0;
}

// Retrieve data from the ESP buffer (only TCP) and store to the internal buffer (max Data Size = 2048)
int ESP_ReqDataBuff(uart_inst_t * uart, uint8_t connID, int dataSize){
    FlushATBuff();

    //Search on ESP if there is more data to read
    if(ipdVal[connID] <= 0){
        buffRecData_pointer = 0;
        ipdVal[connID] = ESP_ReadBuffSize(uart,connID);
        printf("ESP-01 Read Request: New IPD %i.\n",ipdVal[connID]);
    }
    if(ipdVal[connID] == 0){
        printf("ESP-01 Read Request: You don't have data to read.\n");
        return 0;
    }else{
        if (dataSize < 0){
            printf("ESP-01 Read Request: Size less than 0.\n");
            return -1;
        }        
        if(dataSize > MOBILE_MAX_DATA_SIZE){
            printf("ESP-01 Read Request: The maximum data to read is %i.\n",MOBILE_MAX_DATA_SIZE);
            dataSize = MOBILE_MAX_DATA_SIZE;
        }
        int tmp_idp = ipdVal[connID];
        int tmp_idp_new = (tmp_idp-buffRecData_pointer)-dataSize;
        if(tmp_idp_new < 0){
            printf("ESP-01 Read Request: You request %i bytes, but buffer only have %i bytes. Changing value.\n",dataSize,(tmp_idp-buffRecData_pointer));
            dataSize=(tmp_idp-buffRecData_pointer);
        }

        if(buffRecData_pointer == 0){
            char cmdRead[25]={};
            sprintf(cmdRead,"AT+CIPRECVDATA=%i,%i",connID,ipdVal[connID] < BUFF_AT_SIZE ? ipdVal[connID] : BUFF_AT_SIZE);
            FlushATBuff();
            ESP_SendCmd(uart,cmdRead,0); //Must igonre the OK at the end, and the "+CIPRECVDATA,<size>:" at the beginning
            if(!ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(10),false,true)){
                printf("ESP-01 Read Request: Error on Read %i bytes.\n",ipdVal[connID] <= BUFF_AT_SIZE ? ipdVal[connID] : BUFF_AT_SIZE);
                return -1;
            }
            busy_wait_us(MS(100)); 
            int cmdReadSize = strlen(cmdRead)-1;             
            memcpy(&buffRecData, buffATrx + cmdReadSize, ipdVal[connID] < BUFF_AT_SIZE ? ipdVal[connID] : BUFF_AT_SIZE); //memcpy with offset in source
            FlushATBuff();
        }
        printf("ESP-01 Read Request: Return %i bytes.\n",dataSize);
        return dataSize;
    }
}

// Send data to the Mobile Adapter GB Host
uint8_t ESP_SendData(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, const void * databuff, int datasize){
    // Check if the GET command have less than 2048 bytes to send. This is the ESP limit
    if(datasize > 2048){
        printf("ESP-01 Sending Request: ERROR - The request limit is 2048 bytes. Your request have: %i bytes\n", datasize);
    }else{        
        // Send the ammount of data we will send to ESP (the GET command size)
        char cmdSend[100] = {0};
        if(strcmp(sock_type, "UDP") == 0){
            sprintf(cmdSend,"AT+CIPSEND=%i,%i,\"%s\",%i", connID, datasize, conn_host, conn_port);
        }else{
            sprintf(cmdSend,"AT+CIPSEND=%i,%i", connID, datasize);
        }
        ESP_SendCmd(uart,cmdSend,0);
        if(ESP_SerialFind(buffATrx,">",MS(300),true,false)){
            uint8_t * datasend = (uint8_t *)databuff;
            printf("ESP-01 Sending Data: Sending %i bytes...\n",datasize);
            ESP_SendCmd(uart,datasend,datasize);
            if(ESP_SerialFind(buffATrx,"SEND OK\r\n",SEC(5),true,false)){
                printf("ESP-01 Sending Data: %i bytes OK\n",datasize);
                return datasize;
            }
        }
    }
    printf("ESP-01 Sending Request: ERROR\n");
    return -1;
}

// Establish a connection to the Host (Mobile Adapter GB server)
bool ESP_OpenSockConn(uart_inst_t * uart, uint8_t connID, char * sock_type, char * conn_host, int conn_port, int conn_localport, uint8_t conn_mode){
    if(ipdVal[connID] != 0){
        printf("ESP-01 Start Host Connection: You can't open a connection now.\n");
        return false;
    }

    char cmdSckt[100];
    if (strstr(sock_type,"UDP") != NULL){
        if(conn_localport == 0){
            sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i,,%i", connID, sock_type, conn_host, conn_port, conn_mode);
        }else{
            sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i,%i,%i", connID, sock_type, conn_host, conn_port, conn_localport, conn_mode);
        }
    }else{
        sprintf(cmdSckt,"AT+CIPSTART=%i,\"%s\",\"%s\",%i", connID, sock_type, conn_host, conn_port);
    }
    
    ESP_SendCmd(uart, cmdSckt,0);
    if(ESP_SerialFind(buffATrx,"CONNECT",MS(5000),false,false)){
        if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",MS(5000),true,false)){            
            printf("ESP-01 Host %s Connection: OK\n",sock_type);
        }
    }else if(ESP_SerialFind(buffATrx,"ALREADY CONNECTED",MS(5000),false,false)){        
        printf("ESP-01 Start Host %s Connection: ALREADY CONNECTED\n",sock_type);
    }else{
        printf("ESP-01 Start Host %s Connection: ERROR\n",sock_type);
        return false;
    }

    return true;
}

// Return the status of the desired ESP Socket
bool ESP_GetSockStatus(uart_inst_t * uart, uint8_t connID, void *user){
    struct mobile_user *mobile = (struct mobile_user *)user;

    FlushATBuff();

    mobile->esp_sockets[connID].host_id = -1;
    mobile->esp_sockets[connID].local_port = -1;
    mobile->esp_sockets[connID].host_iptype = MOBILE_ADDRTYPE_NONE;
    mobile->esp_sockets[connID].sock_status = false;

    ESP_SendCmd(uart,"AT+CIPSTATUS",0);

    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(3),false,false)){
        char numstat[2] = {};
        // i starts on 7 because the "STATUS:" lenght is 6, and the status code will be at the position 7
        for(int i = 7; i < sizeof(buffATrx); i++){
            if(buffATrx[i] == '\r' && buffATrx[i+1] == '\n'){
                break;
            }else{
                numstat[i-7] = buffATrx[i];
            }
        }
        uint8_t numstat_val = 0;
        numstat_val = atoi(numstat);
        switch (numstat_val){
        case 0:
            printf("ESP-01 Status: The ESP station is not initialized.\n");
            break;
        case 1:
            printf("ESP-01 Status: The ESP station is initialized, but disconnected from an AP.\n");
            break;
        case 2:
            printf("ESP-01 Status: The ESP station is connected to an AP.\n");
            break;
        case 3:
            printf("ESP-01 Status: TCP/UDP still connected.\n");
            break;
        case 4:
            printf("ESP-01 Status: All TCP/UDP disconnected.\n");
            break;
        case 5:
            printf("ESP-01 Status: The ESP station started a Wi-Fi connection, but was not connected to an AP or disconnected from an AP.\n");
            break;
        default:
            printf("ESP-01 Status: ERROR\n");
            break;
        }

        char cmdCheck[15];
        sprintf(cmdCheck,"+CIPSTATUS:%i,",connID);
        int cmdIndex = ESP_GetCmdIndexBuffer(buffATrx,cmdCheck);
        if (cmdIndex >= 0){
            char returnStatus[60];
            for(int i = cmdIndex+strlen(cmdCheck); i < sizeof(buffATrx); i++){
                if(buffATrx[i] == '\r' && buffATrx[i+1] == '\n'){
                    break;
                }else{
                    returnStatus[i-(cmdIndex+strlen(cmdCheck))] = buffATrx[i];
                }
            }
            // Extract the first token
            char * token = strtok(returnStatus, ",");
            // loop through the string to extract all other tokens
            //Socket Type
            //Remote IP
            //Remote Port
            //Local Port
            mobile->esp_sockets[connID].host_id = connID;
            int infoPointer = 0;
            while(token != NULL) {
                switch(infoPointer){
                    case 0:
                        if(strstr(token,"v6") != NULL){
                            if(strstr(token,"TCP") != NULL){
                                mobile->esp_sockets[connID].host_iptype = MOBILE_ADDRTYPE_IPV6;
                                mobile->esp_sockets[connID].host_type = 1;
                            }else if(strstr(token,"UDP") != NULL){
                                    mobile->esp_sockets[connID].host_iptype = MOBILE_ADDRTYPE_IPV6;
                                    mobile->esp_sockets[connID].host_type = 2;
                            }
                        }else if(strstr(token,"TCP") != NULL){
                                mobile->esp_sockets[connID].host_iptype = MOBILE_ADDRTYPE_IPV4;
                                mobile->esp_sockets[connID].host_type = 1;
                        }else if(strstr(token,"UDP") != NULL){
                                mobile->esp_sockets[connID].host_iptype = MOBILE_ADDRTYPE_IPV4;
                                mobile->esp_sockets[connID].host_type = 2;
                        }
                        break;
                    case 3:
                        mobile->esp_sockets[connID].local_port = atoi(token);
                        break;
                    default:
                        break;
                }
                token = strtok(NULL, ",");
                infoPointer++;
            }
            FlushATBuff();
            mobile->esp_sockets[connID].sock_status = true;
        }else{
            printf("ESP-01 Host Status: SOCKET CLOSED\n");
        }        
    }else{
        printf("ESP-01 Host Status: ERROR\n");
    }
    return mobile->esp_sockets[connID].sock_status;
}

// Close the desired ESP Socket
bool ESP_CloseSockConn(uart_inst_t * uart, uint8_t connID){
    char cmd[15];
    sprintf(cmd,"AT+CIPCLOSE=%i",connID);
    ESP_SendCmd(uart,cmd,0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(2),false,false)){
        printf("ESP-01 Close Socket %i: OK\n",connID);
        return true;
    }else{
        printf("ESP-01 Close Socket %i: ERROR\n",connID); 
        return false;
    }
}

//Open an ESP server to accept new connections
bool ESP_OpenServer(uart_inst_t * uart, uint8_t connID, int remotePort){
    char cmd[25];
    sprintf(cmd,"AT+CIPSERVER=1,%i",remotePort);
    ESP_SendCmd(uart,cmd,0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(2),true,false)){
        printf("ESP-01 Open Server: OK\n");
        return true;
    }else{
        printf("ESP-01 Open Server: ERROR\n"); 
        return false;
    }
}

//Close an ESP server and all opened sockets
bool ESP_CloseServer(uart_inst_t * uart){
    ESP_SendCmd(uart,"AT+CIPSERVER=0",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(2),true,false)){
        printf("ESP-01 Close Server: OK\n");
        return true;
    }else{
        printf("ESP-01 Close Server: ERROR\n"); 
        return false;
    }
}
bool ESP_CheckIncommingConn(uart_inst_t * uart, uint8_t connID){
    char cmd[15];
    sprintf(cmd,"%i,CONNECT",connID);
    if(ESP_SerialFind(buffATrx,cmd,MS(100),false,false)){
        FlushATBuff();
        printf("ESP-01 New Connection: OK\n");
        return true;
    }else{
        printf("ESP-01 New Connection: Waiting...\n"); 
        return false;
    }
}

// Initialize the ESP-01 UART communication
bool EspAT_Init(uart_inst_t * uart, int baudrate, int txpin, int rxpin){
    // Set up our UART with the required speed.
    int baud = uart_init(uart, baudrate);
    bool isenabled = uart_is_enabled(uart);

    use_uart0 = (uart == uart0 ? true : false);
    
    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(txpin, GPIO_FUNC_UART);
    gpio_set_function(rxpin, GPIO_FUNC_UART);

    uart_set_fifo_enabled(uart,false);
    uart_set_format(uart,8,1,0);
    uart_set_hw_flow(uart,false,false);     

    // Set up a RX interrupt
    // Select correct interrupt for the UART we are using
    int UART_IRQ = uart == uart0 ? UART0_IRQ : UART1_IRQ;

    // Set up and enable the interrupt handlers
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);

    // Enable the UART to send interrupts - RX only
    uart_set_irq_enables(uart, true, false);

    if(isenabled){
        printf("Uart Baudrate: %i \n",baud);
        printf("Uart Enabled: %i \n",(uint8_t)isenabled);
    }
    return isenabled;
}

// Provides the necessary commands to connect the ESP to a WiFi network
bool ESP_ConnectWifi(uart_inst_t * uart, char * SSID_WiFi, char * Pass_WiFi, int timeout){
    
    //AT+CIPSTO=0 -- set Server timeout (0~7200)
    
    //Test hardware connection
    busy_wait_us(SEC(5));
    FlushATBuff(); // Reset RX Buffer
    
    ESP_SendCmd(uart,"AT",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(2),true,false)){
        printf("ESP-01 Connectivity: Checking...");
        ESP_SendCmd(uart,"AT+RST",0);
        if(ESP_SerialFind(buffATrx,"ready",SEC(10),true,true)){
            printf(" OK\n");
            if(ESP_SerialFind(buffATrx,"\r\nWIFI GOT IP\r\n",SEC(10),true,true)){
                printf(" Wifi Connected\n");
                    // Disable auto-connect during the boot
                    ESP_SendCmd(uart,"AT+CWAUTOCONN=0",0);
                    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
                        printf("ESP-01 Auto Connect on Boot: OK\n");
                    }else{
                    printf("ESP-01 Auto Connect on Boot: ERROR\n"); 
                    }  
            }
        }else{
            printf(" ERROR\n");
        }
    }else{
       printf("ESP-01 Connectivity: ERROR\n"); 
    }
 
    // Disable echo 
    ESP_SendCmd(uart,"ATE0",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 Disable Echo: OK\n");
    }else{
       printf("ESP-01 Disable Echo: ERROR\n"); 
    }
    
    // Only 1 P2P Connection
    ESP_SendCmd(uart,"AT+CIPSERVERMAXCONN=1",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 Only 1 P2P Connection: OK\n");
    }else{
       printf("ESP-01 Only 1 P2P Connection: ERROR\n"); 
    }

    // Shows the origin info on IPD 
    ESP_SendCmd(uart,"AT+CIPDINFO=1",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 UDP IDP info: OK\n");
    }else{
       printf("ESP-01 UDP IDP info: ERROR\n"); 
    }
    
    // Set to Passive Mode to receive TCP info
    ESP_SendCmd(uart, "AT+CIPRECVMODE=1",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 Passive Mode: OK\n");
    }else{
       printf("ESP-01 Passive Mode: ERROR\n"); 
    }

    ESP_SendCmd(uart, "AT+CIPMUX=1",0);
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 Multi Connections: OK\n");
    }else{
       printf("ESP-01 Multi Connections: ERROR\n"); 
    }

    // Set WiFi Mode to Station mode Only
    ESP_SendCmd(uart,"AT+CWMODE=1",0);    
    if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(1),true,false)){
         printf("ESP-01 Station Mode: OK\n");
         // Prepare the command to send
        char espComm[100] = {};
        memset(espComm,'\0',sizeof(espComm));
        sprintf(espComm,"AT+CWJAP=\"%s\",\"%s\"",SSID_WiFi,Pass_WiFi);
        ESP_SendCmd(uart, espComm,0);
        
        if(ESP_SerialFind(buffATrx,"\r\nOK\r\n",SEC(10),true,true)){
            printf("ESP-01 Connecting Wifi: OK\n");
            return true;
        }else{
            printf("ESP-01 Connecting Wifi: ERROR\n");
            return false;
        }
    }else{
       printf("ESP-01 Station Mode: ERROR\n"); 
       return false;
    }
}
#endif