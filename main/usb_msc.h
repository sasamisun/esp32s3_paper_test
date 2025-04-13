/**
 * @file usb_msc.h
 * @brief USB Mass Storage Class (MSC) over SPI SD Card implementation
 */
#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize the SD card over SPI
 * 
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t usb_msc_init_sd_card(void);

/**
 * @brief Initialize the USB MSC function with the SD card as storage
 * 
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t usb_msc_init(void);

/**
 * @brief Unmount the SD card from the application to allow USB host access
 * 
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t usb_msc_unmount_card(void);

/**
 * @brief Mount the SD card to the application
 * 
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t usb_msc_mount_card(void);

/**
 * @brief Check if the USB host is currently using the storage
 * 
 * @return true if the USB host is using the storage, false otherwise
 */
bool usb_msc_host_using_storage(void);

/**
 * @brief Shutdown and cleanup USB MSC resources
 * 
 * @return esp_err_t ESP_OK on success, or an error code
 */
esp_err_t usb_msc_deinit(void);

/**
 * @brief Get the SD card mount point path
 * 
 * @return const char* Mount point path
 */
const char* usb_msc_get_mount_point(void);

#endif /* USB_MSC_H */