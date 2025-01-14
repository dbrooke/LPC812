/**************************************************************************/
/*!
 @file     main.c
 
 @section LICENSE
 
 Software License Agreement (BSD License)
 
 Copyright (c) 2013, K. Townsend (microBuilder.eu)
 All rights reserved.
 
 Modified James Coxon (jacoxon@googlemail.com) 2014-2015
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 1. Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. Neither the name of the copyright holders nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**************************************************************************/
//Node settings have been moved seperate file "settings.h"
#include "settings.h"

#include <stdio.h>
#include "LPC8xx.h"
#include "mrt.h"
#include "spi.h"
#include "rfm69.h"

#if defined(GATEWAY) || defined(DEBUG) || defined(GPS)
#include "uart.h"
#endif


char data_temp[66] = "test";

uint8_t data_count = 96; // 'a' - 1 (as the first function will at 1 to make it 'a'
unsigned int rx_packets = 0, random_output = 0, rx_restarts = 0;
int16_t rx_rssi, floor_rssi, rssi_threshold, adc_result = 0;

uint8_t gsm_buf[80]; //GSM receive buffer

#define GSM_PWR    (17)

/**
 * Setup all pins in the switch matrix of the LPC812
 */
void configurePins() {
    /* Enable SWM clock */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1<<7);
    
    /* Pin Assign 8 bit Configuration */
    /* U0_TXD */
    /* U0_RXD */
    LPC_SWM->PINASSIGN0 = 0xffff0004UL;
    /* U1_TXD */
    /* U1_RXD */
    LPC_SWM->PINASSIGN1 = 0xff0c0dffUL;
    /* SPI0_SCK */
    LPC_SWM->PINASSIGN3 = 0x01ffffffUL;
    /* SPI0_MOSI */
    /* SPI0_MISO */
    /* SPI0_SSEL */
    LPC_SWM->PINASSIGN4 = 0xff0f0809UL;
    
    /* Pin Assign 1 bit Configuration */
    /* SWCLK */
    /* SWDIO */
    /* RESET */
    LPC_SWM->PINENABLE0 = 0xffffffb3UL;
    


    
}

/**
 * Packet data transmission
 * @param Packet length
 */
void transmitData(uint8_t i) {

#ifdef GATEWAY
        printf("rx: %s|0\r\n", data_temp);
    
#endif

    
    // Transmit the data (need to include the length of the packet and power in dbmW)
    RFM69_send(data_temp, i, POWER_OUTPUT);
    
    //Ensure we are in RX mode
    RFM69_setMode(RFM69_MODE_RX);

}

/**
 * This function is called when a packet is received by the radio. It will
 * process the packet.
 */
inline void processData(uint32_t len) {
    uint8_t i, packet_len;
    
    for(i=0; i<len; i++) {
        //finds the end of the packet
        if(data_temp[i] != ']')
            continue;
        
        //then terminates the string, ignore everything afterwards
        data_temp[i+1] = '\0';
        
        //Check validity of string
        // 1) is the first position in array a number
        //printf("%d\r\n", data_temp[0]);
        if((int)data_temp[0] <= 48 || (int)data_temp[0] > 57) {
            //printf("Error1\r\n");
            break;
        }
        
        // 2) is the second position in array a letter
        //      < 'a' or > 'z' then break
        //printf("%d\r\n", data_temp[1]);
        if((int)data_temp[1] < 97 || (int)data_temp[1] > 122){
            //printf("Error2\r\n");
            break;
        }
        
#ifdef GATEWAY
        printf("rx: %s|%d\r\n",data_temp, RFM69_lastRssi());
#endif
        //Reduce the repeat value
        data_temp[0] = data_temp[0] - 1;
        //Now add , and end line and let string functions do the rest
        data_temp[i] = ',';
        data_temp[i+1] = '\0';
        
        if(strstr(data_temp, NODE_ID) != 0)
            break;
        
        strcat(data_temp, NODE_ID); // Add ID
        strcat(data_temp, "]"); // Add ending
        
        packet_len = strlen(data_temp);
        mrtDelay(random_output); // Random delay to try and avoid packet collision
        
        rx_packets++;
        
        transmitData(packet_len);
        break;
    }
}

/**
 * It processing incoming data by the radio or serial connection. This function
 * has to be continously called to keep the node running. This function also
 * adds a delay which is specified as 100ms per unit. Therefore 1000 = 100 sec
 * @param countdown delay added to this function
 */
void awaitData(int countdown) {

    uint8_t rx_len, flags1, old_flags1 = 0x90;

    //Clear buffer
    data_temp[0] = '\0';

    RFM69_setMode(RFM69_MODE_RX);

    rx_restarts = 0;

    while(countdown > 0) {

        flags1 = spiRead(RFM69_REG_27_IRQ_FLAGS1);
#ifdef DEBUG
        if (flags1 != old_flags1) {
            printf("f1: %02x\r\n", flags1);
            old_flags1 = flags1;
        }
#endif
        if (flags1 & RF_IRQFLAGS1_TIMEOUT) {
            // restart the Rx process
            spiWrite(RFM69_REG_3D_PACKET_CONFIG2, spiRead(RFM69_REG_3D_PACKET_CONFIG2) | RF_PACKET2_RXRESTART);
            rx_restarts++;
            // reset the RSSI threshold
            floor_rssi = RFM69_sampleRssi();
#ifdef DEBUG
            // and print threshold
            printf("Restart Rx %d\r\n", RFM69_lastRssiThreshold());
#endif
        }
        // Check rx buffer
        if(RFM69_checkRx() == 1) {
            RFM69_recv(data_temp,  &rx_len);
            data_temp[rx_len - 1] = '\0';
            processData(rx_len);
        }

        countdown--;
        mrtDelay(100);
    }
}

/**
 * Increments packet count which is transmitted in the beginning of each
 * packet. This function has to be called every packet which is initially
 * transmitted by this node.
 * Packet count is starting with 'a' and continues up to 'z', thereafter
 * it's continuing with 'b'. 'a' is only transmitted at the very first
 * transmission!
 */
void incrementPacketCount(void) {
    data_count++;
    // 98 = 'b' up to 122 = 'z'
    if(data_count > 122) {
        data_count = 98; //'b'
    }
}

//GSM Code **********************************************
void GSM_get_data(uint16_t timeout){
    uint8_t i;
    uint16_t x = 0;
    
    while (x < timeout){
        if(UART1_available() > 0){
            for(i=0; i<serial1Buffer_write; i++){
                //gsm_buf[i] = serial1Buffer[i];
                uart0SendChar(serial1Buffer[i]);
                
                //if (gsm_buf[i] == '\r'){
                //    printf("%s", gsm_buf);
                //    return;
                //}
            }
            serial1Buffer_write = 0;
        }
        mrtDelay(1);
        x++;
    }
}

void GSM_send_data(char *command, uint8_t uartport){
    //printf("%s", command);
    uint8_t data_length = strlen(command);
    
    uartSend(command, data_length, uartport);
    
    GSM_get_data(3000);
}

uint8_t GSM_AT(){
    GSM_send_data("AT\r", 1);
    GSM_get_data(2000);
    if (gsm_buf[0] == 'O' && gsm_buf[1] == 'K'){
        return 1;
    }
    else {
        return 0;
    }
}

void GSM_On(){
    //Check if modem is on
    //uint8_t x = 0;
    
    //while((GSM_AT() == 0) && (x < 3)){
    
        LPC_GPIO_PORT->SET0 = 1 << GSM_PWR;
        mrtDelay(2000); //Wait for modem to boot
        LPC_GPIO_PORT->CLR0 = 1 << GSM_PWR;
        mrtDelay(2000);
        
      //  x++;
    //}
    //printf("GSM Booted");
}


void GSM_upload(){
    //Turn on Modem and check has booted
    //GSM_On();
    
    //Setup Modem
    
    //Check whether GPRS/3G
    
    //If GSM send as a SMS
    GSM_send_data("AT+CMGF=1\r", 1); //
    GSM_send_data("AT+CMGS=\"+XXXXXXXXXX\"\r", 1);
    //mrtDelay(1000);
    GSM_send_data(data_temp, 1);
    GSM_send_data("\r\n", 1);
    //mrtDelay(500);
    uart1SendByte(0x1A);
    
    
    //Else GPRS + then send as data
    
    //Disconnect and turn off Modem
    //LPC_GPIO_PORT->CLR0 = 1 << GSM_PWR;
    
    //
}


int main(void)
{

#if defined(GATEWAY) || defined(DEBUG) || defined(GPS)
    // Initialise the UART0 block for printf output
    uart0Init(115200);
#endif
    
    // Configure the multi-rate timer for 1ms ticks
    mrtInit(__SYSTEM_CLOCK/1000);

    /* Enable AHB clock to the Switch Matrix , UART0 , GPIO , IOCON, MRT , ACMP */
    LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 7) | (1 << 14) /*| (1 << 6)*//* | (1 << 18)*/
    | (1 << 10) | (1 << 19);
    
    // Configure the switch matrix (setup pins for UART0 and SPI)
    configurePins();
    
    LPC_GPIO_PORT->DIR0 |= (1 << GSM_PWR);
    
    RFM69_init();
    
#if defined(GATEWAY) || defined(DEBUG) || defined(GPS)
    mrtDelay(100);
    printf("Node Booted\r\n");
    mrtDelay(100);
#endif
    
    uart1Init(115200);
    
    GSM_On();
    
    //GSM_AT();

    
#if defined(GATEWAY) || defined(DEBUG) || defined(GPS)
    printf("Node initialized, version %s\r\n",GIT_VER);
#endif
    
    //Seed random number generator, we can use our 'unique' ID
    random_output = NODE_ID[0] + NODE_ID[1] + NODE_ID[2];

    mrtDelay(5000);
    
    GSM_AT();
    mrtDelay(5000);
    
    GSM_upload();

    while(1) {



        GSM_AT();
        
        incrementPacketCount();
        
        //Clear buffer
        data_temp[0] = '\0';
        uint8_t n;
        
        //Create the packet
        int int_temp;

        int_temp = RFM69_readTemp(); // Read transmitter temperature
        rx_rssi = RFM69_lastRssi();
        // read the rssi threshold before re-sampling noise floor which will change it
        rssi_threshold = RFM69_lastRssiThreshold();
        floor_rssi = RFM69_sampleRssi();

        if(data_count == 97) {
            n = sprintf(data_temp, "%d%cL%s[%s]", NUM_REPEATS, data_count, LOCATION_STRING, NODE_ID);
        }
        else {
            
#ifdef DEBUG
            n = sprintf(data_temp, "%d%cT%dR%d,%dC%dX%d,%dV%d[%s]", NUM_REPEATS, data_count, int_temp, rx_rssi, floor_rssi, rx_packets, rx_restarts, rssi_threshold, adc_result, NODE_ID);
#else
            n = sprintf(data_temp, "%d%cT%dR%dX%d[%s]", NUM_REPEATS, data_count, int_temp, rx_rssi, rx_packets, NODE_ID);
#endif
        }
        
        transmitData(n);
        GSM_upload();

        awaitData(TX_GAP);
    
         }
    
}
