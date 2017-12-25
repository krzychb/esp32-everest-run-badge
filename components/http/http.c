/*
 http.c - HTTP request routines

 File based on https://github.com/espressif/esp-idf/tree/master/examples/03_http_request

 This file is part of the ESP32 Everest Run project
 https://github.com/krzychb/esp32-everest-run

 Copyright (c) 2016 Krzysztof Budzynski <krzychb@gazeta.pl>
 This work is licensed under the Apache License, Version 2.0, January 2004
 See the file LICENSE for details.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include <string.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "http.h"

#define RECV_BUFFER_SIZE 64
static const char* TAG = "HTTP";


void http_client_on_connected(http_client_data *client, http_callback http_on_connected_cb)
{
    client->http_connected_cb = http_on_connected_cb;
}

void http_client_on_process_chunk(http_client_data *client, http_callback http_process_chunk_cb)
{
    client->http_process_chunk_cb = http_process_chunk_cb;
}

void http_client_on_disconnected(http_client_data *client, http_callback http_disconnected_cb)
{
    client->http_disconnected_cb = http_disconnected_cb;
}

esp_err_t http_client_request(http_client_data *client, const char *web_server, const char *request_string)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[RECV_BUFFER_SIZE];

    client->recv_buf = recv_buf;
    client->recv_buf_size = sizeof(recv_buf);

    int err = getaddrinfo(web_server, "80", &hints, &res);

    if (err != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
        vTaskDelay(1000 / portTICK_RATE_MS);
        return ESP_ERR_HTTP_DNS_LOOKUP_FAILED;
    }

    /* Code to print the resolved IP.
       Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code
     */
    addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

    s = socket(res->ai_family, res->ai_socktype, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "... Failed to allocate socket.");
        freeaddrinfo(res);
        vTaskDelay(1000 / portTICK_RATE_MS);
        return ESP_ERR_HTTP_FAILED_TO_ALLOCATE_SOCKET;
    }
    ESP_LOGI(TAG, "... allocated socket");

    if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
        ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
        close(s);
        freeaddrinfo(res);
        vTaskDelay(4000 / portTICK_RATE_MS);
        return ESP_ERR_HTTP_SOCKET_CONNECT_FAILED;
    }

    ESP_LOGI(TAG, "... connected");
    freeaddrinfo(res);
    if (client->http_connected_cb) {
        client->http_connected_cb((uint32_t*) client);
    }

    if (write(s, request_string, strlen(request_string)) < 0) {
        ESP_LOGE(TAG, "... socket send failed");
        close(s);
        vTaskDelay(4000 / portTICK_RATE_MS);
        return ESP_ERR_HTTP_SOCKET_SEND_FAILED;
    }
    ESP_LOGI(TAG, "... socket send success");

    struct timeval receiving_timeout;
    receiving_timeout.tv_sec = 5;
    receiving_timeout.tv_usec = 0;
    if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
            sizeof(receiving_timeout)) < 0) {
        ESP_LOGE(TAG, "... failed to set socket receiving timeout");
        close(s);
        vTaskDelay(4000 / portTICK_PERIOD_MS);
        return ESP_ERR_HTTP_SET_REC_TIMEOUT_FAILED;
    }
    ESP_LOGI(TAG, "... set socket receiving timeout success");

    /* Read HTTP response */
    do {
        bzero(recv_buf, sizeof(recv_buf));
        r = read(s, recv_buf, sizeof(recv_buf)-1);
        if (client->http_process_chunk_cb) {
            client->http_process_chunk_cb((uint32_t*) client);
        }
    } while (r > 0);

    ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d", r, errno);
    close(s);
    if (client->http_disconnected_cb) {
        client->http_disconnected_cb((uint32_t*) client);
    }
    return ESP_OK;
}

/* Out of HTTP response return pointer to response body
   Function return NULL if end of header cannot be identified
 */
const char* find_response_body(char * response)
{
    // Identify end of the response headers
    // http://stackoverflow.com/questions/11254037/how-to-know-when-the-http-headers-part-is-ended
    char * eol; // end of line
    char * bol; // beginning of line
    bool nheaderfound = false; // end of response headers has been found

    bol = response;
    while ((eol = index(bol, '\n')) != NULL) {
        // update bol based upon the value of eol
        bol = eol + 1;
        // test if end of headers has been reached
        if ( (!(strncmp(bol, "\r\n", 2))) || (!(strncmp(bol, "\n", 1))) )
        {
           // note that end of headers has been found
            nheaderfound = true;
           // update the value of bol to reflect the beginning of the line
           // immediately after the headers
           if (bol[0] != '\n') {
              bol += 1;
           }
           bol += 1;
           break;
        }
    }
    if (nheaderfound) {
        return bol;
    } else {
        return NULL;
    }
}


