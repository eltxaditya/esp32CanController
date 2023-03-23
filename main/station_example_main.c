/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <sys/param.h>
#include <stdio.h>
#include "esp_netif.h"
#include "lwip/inet.h"
#include "esp_http_server.h"

#include "driver/gpio.h"
#include "driver/twai.h"

//Task Handle Declarations
TaskHandle_t TaskHandleReceiveDataFromTWAI = NULL;
static httpd_handle_t server = NULL;


//TWAI config Declarations 
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NO_ACK);
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

uint32_t filterId, maskId = 0x7FF;

//WIFI Credentials
#define EXAMPLE_ESP_WIFI_SSID      "A"
#define EXAMPLE_ESP_WIFI_PASS      "0987654321"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

//Authorisation Options Available for WIFI
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;

//WIFI Event Handler
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {// Connect to WIFI after Stations mode initialized 
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {// Retry connecting to wifi on disconnection
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {// Print IP after successful connection to wifi
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

//WIFI Initialization
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init()); //Initialize netif for TCP/IP Stack

    ESP_ERROR_CHECK(esp_event_loop_create_default());// Create event loop to add handlers to execution queue
    esp_netif_create_default_wifi_sta(); // Create Wifi Station 

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); //Create default wifi initialization config
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));//Init Wifi Config

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    //Register Event Handlers for WIFI and IP
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
	     .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
	     .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    //Start WIFI
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

//Send packet to all web socket clients connected
esp_err_t httpd_ws_send_frame_to_all_clients(httpd_ws_frame_t *ws_pkt) {
    size_t fds = 4; // Max clients to find
    int client_fds[4] ;
    for(uint8_t local8bitCounter = 0;local8bitCounter < 4; local8bitCounter++){
        client_fds[local8bitCounter] = 0;
    }
    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds); //Fetch client list from server handle

    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "ret = %d",ret);
        return ret;
    }

    for (int i = 0; i < fds; i++) { // send packet to client if it is a websocket connection
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, client_fds[i], ws_pkt);
        }
    }

    return ESP_OK;
}

void sendDataToWS(uint8_t * buf){// Make string into packet to be sent to all clients
        httpd_ws_frame_t ws_pkt2; // declare packet frame
        memset(&ws_pkt2, 0, sizeof(httpd_ws_frame_t)); // Init packet frame as 0
        ws_pkt2.type = HTTPD_WS_TYPE_TEXT; // set type as text
        ws_pkt2.payload = buf;// pass string as payload
        ws_pkt2.len = strlen((char *)buf)+1;// define length of packet as string length plus 1
        httpd_ws_send_frame_to_all_clients(&ws_pkt2);// pass packet to function to send to all clients
}


void parseWsPacket(uint8_t *buf){// Function to parse data received from WIFI websocket
    if(strstr((const char *)buf,"SET")){ // For command SETMASK:500, 500H is set as receive mask
        if(strstr((const char *)buf,"MASK:")){
            maskId = strtol((const char *)&buf[8], NULL,16);
            ESP_LOGI(TAG, "SET MASK = %x\n", maskId);
        }
        else if(strstr((const char *)buf,"FILTER:")){// For command SETFILTER:700, 700H is set as receive filter
            filterId = strtol((const char *)&buf[10], NULL,16);
            ESP_LOGI(TAG, "SET FILTER = %x\n", filterId);
            vTaskSuspend(TaskHandleReceiveDataFromTWAI);
            //Stop the TWAI Driver
            ESP_ERROR_CHECK(twai_stop());               
            ESP_LOGI(TAG, "Driver stopped");
            //Uninstall TWAI driver
            ESP_ERROR_CHECK(twai_driver_uninstall());
            ESP_LOGI(TAG, "Driver uninstalled");
            
            f_config.acceptance_code = (filterId << 21);
            f_config.acceptance_mask = ~(maskId << 21);
            f_config.single_filter = true;

            //Install TWAI driver
            if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
                printf("Driver installed\n");
            } else {
                printf("Failed to install driver\n");
                return;
            }

            //Start TWAI driver
            if (twai_start() == ESP_OK) {
                printf("Driver started\n");
            } else {
                printf("Failed to start driver\n");
                return;
            }
            vTaskResume(TaskHandleReceiveDataFromTWAI);
        }

    }
    else if(strstr((const char *)buf,"GET")){ 
        if(strstr((const char *)buf,"MASK")){// GETMASK gets mask value
            char localStr[15];
            memset(localStr,0,strlen(localStr));
            sprintf(localStr,"MASK = %x",maskId);
            sendDataToWS((uint8_t*)localStr);
            ESP_LOGI(TAG, "GET MASK = %x\n", maskId);
        }
        else if(strstr((const char *)buf,"FILTER")){ // GETFILTER gets filter value
            char localStr[15];
            memset(localStr,0,strlen(localStr));
            sprintf(localStr,"FILTER = %x",filterId);
            sendDataToWS((uint8_t*)localStr);
            ESP_LOGI(TAG, "GET FILTER = %x\n", filterId);
        }
    }
    else{// 500 2 31 32 formats id as 500H, dlc 2 and data as 31H ,32H to be sent over TWAI 
        char *ptr = strchr((const char *)buf,' ') + 1;
        uint32_t id = strtol((const char *)buf,NULL,16);
        ESP_LOGI(TAG, "id is = %xH\n",id);
        
        uint8_t dsize = strtol((const char *)ptr,NULL,16);
        ptr = strchr((const char *)ptr,' ') + 1;
        ESP_LOGI(TAG, "ptr=%s, ds=%u",ptr,dsize);

        uint8_t data[8];
        memset(data,0,strlen((char *)data));
        uint8_t local8bitCounter = 0;
        
        while( local8bitCounter < dsize && local8bitCounter < 8){
            data[local8bitCounter] = strtol((const char *)ptr,NULL,16);
            if(strchr((const char *)ptr,' ') != NULL){
                ptr = strchr((const char *)ptr,' ') + 1;
            }
            ESP_LOGI(TAG, "data[%u] = %x\n",local8bitCounter, data[local8bitCounter]);
            local8bitCounter++;
        }
        
        //Configure message to transmit
        twai_message_t message;
        message.identifier = id;
        message.data_length_code = dsize;
        for (int i = 0; i < dsize; i++) {
            message.data[i] = data[i];
        }

        //Queue message for transmission
        if (twai_transmit(&message, pdMS_TO_TICKS(1000)) == ESP_OK) {
            printf("Message queued for transmission\n");
        } else {
            printf("Failed to queue message for transmission\n");
        }
        ESP_LOGI(TAG, "Sending Data Over TWAI");
        // ESP_LOGI(TAG, "data = %u %u %u %u %u %u %u %u \n",data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7]);
    }
}

// Handler for websocket
static esp_err_t http_ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);// get length of data packet
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len) {
        /* ws_pkt.len + 1 is for NULL termination as we are expecting a string */
        buf = calloc(1, ws_pkt.len + 6);// Allocate memory for data packet
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        /* Set max_len = ws_pkt.len to get the frame payload */
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);// Receive data packet
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    // strcat((char *)buf,echo);
    // ws_pkt.len += 5;
    ret = httpd_ws_send_frame(req, &ws_pkt);// Send data packet back to ws server for connection confirmation
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_send_frame failed with %d", ret);
    }
    parseWsPacket(buf);// Parse packet received for commands
    free(buf);// free alocated memory
    return ret;
}

// Initialize web socket data struct
static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = http_ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};

// Start webserver
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();// Init http daemon config to default

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ws);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

// stop server function
static void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

// FreeRTOS Task to receive TWAI data continuously
void receiveDataFromTWAI(){
    ESP_LOGI(TAG, "TWAI Receive Task Started");
    // esp_err_t err_msg ;
    while(1){
        //Wait for message to be received
        twai_message_t messageR;
        char dataToWs[40];
        memset(dataToWs, 0, strlen(dataToWs));
        ESP_ERROR_CHECK(twai_receive(&messageR, portMAX_DELAY));// wait for TWAI reception 
        ESP_LOGI(TAG, "Received Msg over TWAI from Id = %xH",messageR.identifier);
        //memset(dataToWs, 0, strlen(dataToWs));
	// format data to be sent to ws server which was received over TWAI
        sprintf(dataToWs,"%x %x %x %x %x %x %x %x %x %x CANRx", messageR.identifier, messageR.data_length_code, messageR.data[0], messageR.data[1], messageR.data[2], messageR.data[3], messageR.data[4], messageR.data[5], messageR.data[6], messageR.data[7]);
        // sprintf(dataToWs,"%x %x", messageR.identifier, messageR.data_length_code);
        // for(uint8_t local8bitCounter = 0; local8bitCounter < messageR.data_length_code; local8bitCounter++){
        //     char dataX[4];
        //     memset(dataX,0,strlen(dataX));
        //     sprintf(dataX," %2x",messageR.data[local8bitCounter]);
        //     strcat(dataToWs,(const char *)dataX);
        // }
        // strcat(dataToWs,(const char *)" CANRx");
        sendDataToWS((uint8_t *)dataToWs);// send string to all WS connection
        vTaskDelay(pdMS_TO_TICKS(10));// wait 10 ticks after every iteraion
    }
}
void app_main(void)
{
    //Initialize NVS memory
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();// Start WIFI in station mode

    // Start the web socket server for the first time
    server = start_webserver();


    //Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        printf("Driver installed\n");
    } else {
        printf("Failed to install driver\n");
        return;
    }

    //Start TWAI driver
    if (twai_start() == ESP_OK) {
        printf("Driver started\n");
    } else {
        printf("Failed to start driver\n");
        return;
    }

    //Create a task to receive TWAI messages and pin to core 1
    xTaskCreatePinnedToCore(
                            receiveDataFromTWAI,
                            "Receive Data From TWAI",
                            4096,
                            NULL,
                            1,
                            &TaskHandleReceiveDataFromTWAI,
                            1
    );
}
