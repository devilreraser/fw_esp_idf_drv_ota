/* *****************************************************************************
 * File:   drv_ota.h
 * Author: DL
 *
 * Created on 2023 11 01
 * 
 * Description: esp-idf driver for ota
 * 
 **************************************************************************** */
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */


/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
//#include <stddef.h>
    
/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */

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
 * Function-Like Macro
 **************************************************************************** */

/* *****************************************************************************
 * Variables External Usage
 **************************************************************************** */ 

/* *****************************************************************************
 * Function Prototypes
 **************************************************************************** */
void drv_ota_print_info(void);
void drv_ota_init(void);
void drv_ota_create_task(const char *url);

#ifdef __cplusplus
}
#endif /* __cplusplus */


