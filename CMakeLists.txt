idf_component_register(SRCS "cmd_ota.c" "drv_ota.c" 
                    INCLUDE_DIRS "." 
                    REQUIRES 
                        "console" 
                        "app_update" 
                        "esp_http_client" 
                        "bootloader_support"
                    EMBED_TXTFILES ota_ca_cert.pem
                                      )
                 

