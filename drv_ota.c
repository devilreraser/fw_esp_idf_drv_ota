/* *****************************************************************************
 * File:   drv_ota.c
 * Author: DL
 *
 * Created on 2023 11 01
 * 
 * Description: esp-idf driver for ota
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "drv_ota.h"
#include "cmd_ota.h"

#include <sdkconfig.h>
#include <stdint.h>
#include <stdlib.h>

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_app_format.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_ota"

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */
#define OTA_URL_SIZE            256
#define WRITE_DATA_BUFFSIZE     1024
#define HASH_LEN                32 /* SHA-256 digest length */

#define CONFIG_EXAMPLE_SKIP_VERSION_CHECK

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */
extern const uint8_t server_cert_pem_start[] asm("_binary_ota_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ota_ca_cert_pem_end");

TaskHandle_t xHandleOTA = NULL;
char cURLOTA[OTA_URL_SIZE] = CONFIG_DRV_OTA_FIRMWARE_UPG_URL;
static char ota_write_data[WRITE_DATA_BUFFSIZE + 1] = { 0 };

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    static char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

static void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}

void print_esp_app_desc(const esp_app_desc_t *desc) 
{
    if (desc == NULL) 
    {
        printf("Null pointer provided\n");
        return;
    }

    printf("Magic Word: 0x%08X\n", desc->magic_word);
    printf("Secure Version: %u\n", desc->secure_version);
    printf("reserv1[0]: 0x%08X\n", desc->reserv1[0]);
    printf("reserv1[1]: 0x%08X\n", desc->reserv1[1]);

    printf("Version: %s\n", desc->version);
    printf("Project Name: %s\n", desc->project_name);
    printf("Compile Time: %s\n", desc->time);
    printf("Compile Date: %s\n", desc->date);
    printf("IDF Version: %s\n", desc->idf_ver);

    for (int i = 0; i < 32; i++) {
        printf("reserv2[%d]: 0x%08X\n", i, desc->reserv2[i]);
    }

    printf("App ELF SHA256: ");
    for (int i = 0; i < 32; i++) {
        printf("%02X", desc->app_elf_sha256[i]);
    }
    printf("\n");
}




void drv_ota_print_info(void)
{
    get_sha256_of_partitions(); 

    //Print app description
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    print_esp_app_desc(app_desc);
}


void drv_ota_init(void)
{
    cmd_ota_register();

    drv_ota_print_info();
}



static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

//static void __attribute__((noreturn)) task_fatal_error(void)
static void task_fatal_error(void)
{
    ESP_LOGE(TAG, "Exiting task due to fatal error...");
    xHandleOTA = NULL;
    (void)vTaskDelete(NULL);
    //while (1) {;}
}


static void ota_task(void *pvParameter)
{
    esp_err_t err;
    /* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
    esp_ota_handle_t update_handle = 0 ;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "Starting OTA");

    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();

    if (configured != running) 
    {
        ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
                 (unsigned int)configured->address, (unsigned int)running->address);
        ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }
    ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, (unsigned int)running->address);

    esp_http_client_config_t config = 
    {
        .url = (char*)pvParameter,
        //#if CONFIG_ESP_BOARD_HC00 != 1
        .cert_pem = (char *)server_cert_pem_start,
        //#endif
        .timeout_ms = CONFIG_DRV_OTA_RECV_TIMEOUT,
        .keep_alive_enable = true,
    };

    #ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
    char url_buf[OTA_URL_SIZE];
    if (strcmp(config.url, "FROM_STDIN") == 0) {
        example_configure_stdin_stdout();
        fgets(url_buf, OTA_URL_SIZE, stdin);
        int len = strlen(url_buf);
        url_buf[len - 1] = '\0';
        config.url = url_buf;
    } else {
        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
        abort();
    }
    #endif

    #ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
    config.skip_cert_common_name_check = true;
    #endif

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) 
    {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        task_fatal_error();
    }
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        task_fatal_error();
    }

    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
             update_partition->subtype, (unsigned int)update_partition->address);

    int binary_file_length = 0;
    /*deal with all receive packet*/
    bool image_header_was_checked = false;
    while (1) 
    {
        int data_read = esp_http_client_read(client, ota_write_data, WRITE_DATA_BUFFSIZE);
        if (data_read < 0) 
        {
            ESP_LOGE(TAG, "Error: SSL data read error");
            http_cleanup(client);
            task_fatal_error();
        } 
        else if (data_read > 0) 
        {
            if (image_header_was_checked == false) 
            {
                esp_app_desc_t new_app_info;
                if (data_read > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) 
                {
                    // check current version with downloading
                    memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                    ESP_LOGI(TAG, "New firmware version: %s", new_app_info.version);

                    esp_app_desc_t running_app_info;
                    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) 
                    {
                        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
                    }

                    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
                    esp_app_desc_t invalid_app_info;
                    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) 
                    {
                        ESP_LOGI(TAG, "Last invalid firmware version: %s", invalid_app_info.version);
                    }

                    // check current version with last invalid partition
                    if (last_invalid_app != NULL) 
                    {
                        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) 
                        {
                            ESP_LOGW(TAG, "New version is the same as invalid version.");
                            ESP_LOGW(TAG, "Previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
                            ESP_LOGW(TAG, "The firmware has been rolled back to the previous version.");
                            http_cleanup(client);
                            //infinite_loop();
                            task_fatal_error();
                        }
                    }
                    #ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
                    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) 
                    {
                        ESP_LOGW(TAG, "Current running version is the same as a new. We will not continue the update.");
                        http_cleanup(client);
                        //infinite_loop();
                        task_fatal_error();
                    }
                    #endif

                    image_header_was_checked = true;

                    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
                        http_cleanup(client);
                        esp_ota_abort(update_handle);
                        task_fatal_error();
                    }
                    ESP_LOGI(TAG, "esp_ota_begin succeeded");
                } 
                else 
                {
                    ESP_LOGE(TAG, "received package is not fit len");
                    http_cleanup(client);
                    esp_ota_abort(update_handle);
                    task_fatal_error();
                }
            }
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) 
            {
                http_cleanup(client);
                esp_ota_abort(update_handle);
                task_fatal_error();
            }
            binary_file_length += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_length);
        } 
        else if (data_read == 0) 
        {
           /*
            * As esp_http_client_read never returns negative error code, we rely on
            * `errno` to check for underlying transport connectivity closure if any
            */
            if (errno == ECONNRESET || errno == ENOTCONN) 
            {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                break;
            }
            if (esp_http_client_is_complete_data_received(client) == true) 
            {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Total Write binary data length: %d", binary_file_length);
    if (esp_http_client_is_complete_data_received(client) != true) 
    {
        ESP_LOGE(TAG, "Error in receiving complete file");
        http_cleanup(client);
        esp_ota_abort(update_handle);
        task_fatal_error();
    }

    err = esp_ota_end(update_handle);

    if (err != ESP_OK) 
    {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) 
        {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } 
        else 
        {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        http_cleanup(client);
        task_fatal_error();
    }


    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        http_cleanup(client);
        task_fatal_error();
    }


    ESP_LOGI(TAG, "Prepare to restart system!");
    esp_restart();
    
    xHandleOTA = NULL;
    vTaskDelete(NULL);

    return ;
}




void drv_ota_create_task(const char *url)
{
    const char* upgradeURL = url;
    if (url == NULL) 
    {
        upgradeURL = cURLOTA;
        ESP_LOGI(TAG, "Using default firmware: %s", upgradeURL);
    }

    if (xHandleOTA == NULL)
    {
        ESP_LOGI(TAG, "Creating OTA Task...");
        xTaskCreate(&ota_task, "ota_task", 8192, (void *)upgradeURL, configMAX_PRIORITIES - 0, &xHandleOTA);
        ESP_LOGI(TAG, "Created OTA Task...");
        configASSERT(xHandleOTA);
    }
    else
    {
        ESP_LOGI(TAG, "Error OTA Task already started...");
    }
}

