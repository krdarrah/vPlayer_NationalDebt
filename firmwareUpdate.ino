
#include "esp_ota_ops.h"
// Define firmware file path
const char* firmwareFilePath = "/firmware.bin";

// Function to check and update firmware from SD card
void checkAndUpdateFirmware() {
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println(F("ERROR: SD card mount failed!"));
    return;
  }

  // Check for firmware file
  if (SD_MMC.exists(firmwareFilePath)) {
    Serial.println(F("Firmware update file found! Starting update..."));

    File updateFile = SD_MMC.open(firmwareFilePath);
    if (!updateFile) {
      Serial.println(F("ERROR: Failed to open firmware file"));
      return;
    }

    // Start OTA
    esp_ota_handle_t otaHandle;
    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(NULL);
    if (esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle) != ESP_OK) {
      Serial.println(F("ERROR: OTA begin failed"));
      updateFile.close();
      return;
    }

    // Write firmware file to flash
    while (updateFile.available()) {
      uint8_t buffer[1024];
      int bytesRead = updateFile.read(buffer, sizeof(buffer));
      if (esp_ota_write(otaHandle, buffer, bytesRead) != ESP_OK) {
        Serial.println(F("ERROR: OTA write failed"));
        esp_ota_end(otaHandle);
        updateFile.close();
        return;
      }
    }

    // Finalize OTA update
    if (esp_ota_end(otaHandle) == ESP_OK) {
      if (esp_ota_set_boot_partition(updatePartition) == ESP_OK) {
        Serial.println(F("Firmware update completed successfully!"));
        updateFile.close();
        SD_MMC.remove(firmwareFilePath);  // Delete firmware file
        esp_restart();                    // Restart to apply the update
      } else {
        Serial.println(F("ERROR: OTA set boot partition failed"));
      }
    } else {
      Serial.println(F("ERROR: OTA end failed"));
    }
  } else {
    Serial.println(F("No firmware update file found."));
  }
}