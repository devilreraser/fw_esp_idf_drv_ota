/* *****************************************************************************
 * File:   cmd_ota.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "cmd_ota.h"
#include "drv_ota.h"

#include <string.h>

#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"

#include "argtable3/argtable3.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "cmd_ota"

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

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

static struct {
    struct arg_str *command;
    struct arg_end *end;
} ota_args;

char null_string_ota[] = "";

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */

/* *****************************************************************************
 * Functions
 **************************************************************************** */
static int update_firmware(int argc, char **argv)
{
    ESP_LOGI(__func__, "argc=%d", argc);
    for (int i = 0; i < argc; i++)
    {
        ESP_LOGI(__func__, "argv[%d]=%s", i, argv[i]);
    }

    int nerrors = arg_parse(argc, argv, (void **)&ota_args);
    if (nerrors != ESP_OK)
    {
        arg_print_errors(stderr, ota_args.end, argv[0]);
        return ESP_FAIL;
    }

    const char* url = ota_args.command->sval[0];
    if (strlen(url) > 0)
    {
        ESP_LOGI(TAG, "Starting Firmware Update from URL: %s", url);
        drv_ota_create_task(url);
    }
    else
    {
        ESP_LOGI(TAG, "Starting Firmware Update from default URL");
        drv_ota_create_task(NULL);
    }

    return 0;
}

static void register_ota(void)
{
    ota_args.command = arg_strn(NULL, NULL, "<url>", 0, 1, "Command can be : ota [url]");
    ota_args.end = arg_end(1);

    const esp_console_cmd_t cmd_ota = {
        .command = "ota",
        .help = "Firmware Update Request",
        .hint = NULL,
        .func = &update_firmware,
        .argtable = &ota_args,
    };

    ota_args.command->sval[0] = null_string_ota;

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd_ota));
}


void cmd_ota_register(void)
{
    register_ota();
}