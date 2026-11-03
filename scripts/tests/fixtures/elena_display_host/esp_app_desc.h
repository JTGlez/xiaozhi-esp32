/* Host stub for esp_app_desc.h. */
#ifndef HOST_ESP_APP_DESC_H
#define HOST_ESP_APP_DESC_H

typedef struct {
    char version[32];
} esp_app_desc_t;

const esp_app_desc_t* esp_app_get_description(void);

#endif /* HOST_ESP_APP_DESC_H */
