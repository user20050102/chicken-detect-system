/*
 * 智能养殖监控系统 - ESP32S3主控程序
 * 传感器清单：
 * 1. DHT11 - 温度
 * 2. DHT11 - 湿度  
 * 3. MQ137 - 氨气浓度
 * 4. TVOC-CO2 - 二氧化碳浓度
 * 5. GP2Y1014AUOF - PM2.5浓度
 * 6. BMP280 - 气压
 * 7. OLED 0.96寸I2C - 显示屏
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/inet.h"

// WiFi配置
#define WIFI_SSID      "您的WiFi名称"
#define WIFI_PASSWORD  "您的WiFi密码"

// static const char *TAG = "CHICKEN_MONITOR";  // 暂时注释掉，避免未使用警告

// 传感器数据结构（根据您的传感器清单）
typedef struct {
    float temperature;      // 温度 (°C) - DHT11
    float humidity;        // 湿度 (%) - DHT11
    float nh3_concentration; // 氨气浓度 (ppm) - MQ137
    float co2_concentration; // 二氧化碳浓度 (ppm) - TVOC-CO2
    float pm25_concentration; // PM2.5浓度 (μg/m³) - GP2Y1014AUOF
    float pressure;        // 气压 (hPa) - BMP280
    uint32_t timestamp;    // 时间戳
} sensor_data_t;

// 全局传感器数据
static sensor_data_t sensor_data = {
    .temperature = 25.5,
    .humidity = 60.0,
    .nh3_concentration = 10.5,
    .co2_concentration = 800.0,
    .pm25_concentration = 35.0,
    .pressure = 1013.2,
    .timestamp = 0
};

// HTTP请求处理 - 传感器数据API
static esp_err_t sensor_api_handler(httpd_req_t *req)
{
    // 更新数据时间戳
    sensor_data.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 构建JSON响应
    char response[400];
    snprintf(response, sizeof(response),
             "{\"temperature\":%.1f,\"humidity\":%.1f,\"nh3_concentration\":%.1f,"
             "\"co2_concentration\":%.1f,\"pm25_concentration\":%.1f,\"pressure\":%.1f,\"timestamp\":%lu}",
             sensor_data.temperature,
             sensor_data.humidity,
             sensor_data.nh3_concentration,
             sensor_data.co2_concentration,
             sensor_data.pm25_concentration,
             sensor_data.pressure,
             sensor_data.timestamp);
    
    // 设置HTTP响应头
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    
    // 发送响应
    httpd_resp_send(req, response, strlen(response));
    
    printf("API请求处理完成，发送数据: %s\n", response);
    return ESP_OK;
}

// HTTP路由配置
static const httpd_uri_t sensor_api = {
    .uri       = "/api/sensor",
    .method    = HTTP_GET,
    .handler   = sensor_api_handler,
    .user_ctx  = NULL
};

// 启动HTTP服务器
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // 启动服务器
    if (httpd_start(&server, &config) == ESP_OK) {
        // 注册路由
        httpd_register_uri_handler(server, &sensor_api);
        printf("HTTP服务器启动成功\n");
        return server;
    }
    
    printf("HTTP服务器启动失败\n");
    return NULL;
}

// WiFi事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        printf("WiFi连接断开，尝试重连...\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        // 直接打印IP地址的四个字节（使用显式类型转换）
        printf("获取到IP地址: %d.%d.%d.%d\n",
               (int)((event->ip_info.ip.addr >> 0) & 0xFF),
               (int)((event->ip_info.ip.addr >> 8) & 0xFF),
               (int)((event->ip_info.ip.addr >> 16) & 0xFF),
               (int)((event->ip_info.ip.addr >> 24) & 0xFF));
        
        // 启动HTTP服务器
        start_webserver();
    }
}

// 初始化WiFi
static void wifi_init_sta(void)
{
    // 初始化网络接口
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // 注册事件处理
    esp_event_handler_instance_register(WIFI_EVENT,
                                        ESP_EVENT_ANY_ID,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);
    esp_event_handler_instance_register(IP_EVENT,
                                        IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler,
                                        NULL,
                                        NULL);

    // 设置WiFi配置
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    printf("WiFi初始化完成，正在连接: %s\n", WIFI_SSID);
}

// 传感器数据采集任务（模拟数据）
static void sensor_task(void *pvParameter)
{
    // 初始化随机数种子
    srand((unsigned int)time(NULL));
    
    while (1) {
        // 模拟传感器数据变化（实际应从真实传感器读取）
        // 使用简单的计数器生成伪随机数
        static uint32_t counter = 0;
        counter++;
        
        // 基于计数器生成伪随机数
        uint32_t rand_val = (counter * 1103515245 + 12345) & 0x7FFFFFFF;
        
        // 更新传感器数据（模拟真实传感器读数）
        sensor_data.temperature = 20.0 + ((rand_val % 150) / 10.0);  // 20-35°C
        sensor_data.humidity = 40.0 + (((rand_val + 1000) % 400) / 10.0);      // 40-80%
        sensor_data.nh3_concentration = ((rand_val + 2000) % 300) / 10.0;      // 0-30ppm
        sensor_data.co2_concentration = 500.0 + ((rand_val + 3000) % 2000);    // 500-2500ppm
        sensor_data.pm25_concentration = ((rand_val + 4000) % 100);            // 0-100μg/m³
        sensor_data.pressure = 980.0 + ((rand_val + 5000) % 100);              // 980-1080hPa
        
        printf("传感器数据更新:\n");
        printf("  温度: %.1f°C, 湿度: %.1f%%\n", sensor_data.temperature, sensor_data.humidity);
        printf("  氨气浓度: %.1fppm, CO2浓度: %.1fppm\n", sensor_data.nh3_concentration, sensor_data.co2_concentration);
        printf("  PM2.5浓度: %.1fμg/m³, 气压: %.1fhPa\n", sensor_data.pm25_concentration, sensor_data.pressure);
        
        // 每5秒更新一次数据
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 智能养殖监控系统主函数
 */
void app_main(void)
{
    printf("=== 智能养殖监控系统启动 ===\n");
    printf("ESP32S3主控芯片初始化...\n");
    
    // 初始化NVS存储
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    
    // 初始化WiFi
    wifi_init_sta();
    
    // 创建传感器数据采集任务
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
    
    printf("系统初始化完成，传感器数据采集任务已启动...\n");
    printf("等待WiFi连接...\n");
}