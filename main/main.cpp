#include <freertos/FreeRTOS.h>

const char* const TAG = "main";

#include "BoardDescriptor.h"
#include "captiveportal/Manager.h"
#include "Chipset.h"
#include "CommandHandler.h"
#include "config/Config.h"
#include "diagnostics/Diagnostics.h"
#include "estop/EStopManager.h"
#include "events/Events.h"
#include "GatewayConnectionManager.h"
#include "Logging.h"
#include "OtaUpdateManager.h"
#include "serial/Serial.h"
#include "serial_console/SerialInputHandler.h"
#include "visual/VisualStateManager.h"
#include "wifi/WiFiManager.h"

#include <esp_err.h>
#include <nvs_flash.h>

// Internal setup function, returns true if setup succeeded, false otherwise.
static bool trySetup()
{
  if (!OpenShock::VisualStateManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize VisualStateManager");
    return false;
  }

  if (!OpenShock::EStopManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize EStopManager");
    return false;
  }

  if (!OpenShock::SerialInputHandler::Init()) {
    OS_LOGE(TAG, "Unable to initialize SerialInputHandler");
    return false;
  }

  if (!OpenShock::CommandHandler::Init()) {
    OS_LOGE(TAG, "Unable to initialize CommandHandler");
    return false;
  }

  // Subscribes to WiFi state events, so it goes before WiFiManager starts producing them
  if (!OpenShock::GatewayConnectionManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize GatewayConnectionManager");
    return false;
  }

  if (!OpenShock::WiFiManager::Init()) {
    OS_LOGE(TAG, "Unable to initialize WiFiManager");
    return false;
  }

  if (!OpenShock::CaptivePortal::Init()) {
    OS_LOGE(TAG, "Unable to initialize CaptivePortal");
    return false;
  }

  return true;
}

// OTA setup is the same as normal setup, but we invalidate the currently running app, and roll back if it fails.
static void otaSetup()
{
  OS_LOGI(TAG, "Validating OTA app");

  if (!OpenShock::OtaUpdateManager::IsPendingImageForThisBoard()) {
    OS_LOGE(TAG, "This image was not built for this board, rolling back");
    OpenShock::OtaUpdateManager::InvalidateAndRollback();
  }

  if (!trySetup()) {
    OS_LOGE(TAG, "Unable to validate OTA app, rolling back");
    OpenShock::VisualStateManager::SetCriticalError();
    OpenShock::OtaUpdateManager::InvalidateAndRollback();
  }

  OS_LOGI(TAG, "Marking OTA app as valid");

  OpenShock::OtaUpdateManager::ValidateApp();

  OS_LOGI(TAG, "Done validating OTA app");
}

// App setup is the same as normal setup, but we restart if it fails.
static void appSetup()
{
  if (!trySetup()) {
    OpenShock::VisualStateManager::SetCriticalError();  // Shown for the 5 s until the restart
    OS_LOGI(TAG, "Restarting in 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(5000));
    esp_restart();
  }
}

// ESP-IDF application entry point. Runs the one-time setup, then stays in the
// update loop below for the lifetime of the firmware (it never returns).
extern "C" void app_main()
{
  // First, while this is the only OpenShock task: installing the console driver switches stdout's VFS over to it,
  // which isn't safe while another task is printing. SerialInputHandler::Init calls it again (a no-op) and fails
  // setup if it still isn't up; until then output takes the pre-driver path.
  if (!OpenShock::Serial::Init()) {
    OS_LOGE(TAG, "Failed to initialize the serial console driver");
  }

  // WiFi persists calibration/config in the default NVS partition, so NVS must be
  // initialized before esp_wifi_init() (or any other NVS user) runs. Arduino used
  // to do this implicitly during startup; under the IDF entry point we do it
  // explicitly. If the partition is from an incompatible/older layout, reformat it.
  esp_err_t nvsResult = nvs_flash_init();
  if (nvsResult == ESP_ERR_NVS_NO_FREE_PAGES || nvsResult == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    OS_LOGW(TAG, "NVS partition unusable (%s), erasing and reformatting", esp_err_to_name(nvsResult));
    if (nvs_flash_erase() != ESP_OK) {
      OS_PANIC(TAG, "Failed to erase NVS partition");
    }
    nvsResult = nvs_flash_init();
  }
  if (nvsResult != ESP_OK) {
    OS_PANIC(TAG, "Failed to initialize NVS: %s", esp_err_to_name(nvsResult));
  }

  // Who we are. The descriptor is stamped into the image per board (BoardDescriptor.h); everything that needs a
  // pin or the board name reads it from there, so it is read and validated once here, before any other task
  // exists and before Config::Init seeds defaults. Every later access is a plain load.
  {
    OpenShock::Board::Init();
    const OpenShock::BoardDescriptor& board = OpenShock::Board::Get();
    OS_LOGI(TAG, "Board: %s (%s)%s", board.board, board.chip, OpenShock::Board::IsStamped() ? "" : " - no valid descriptor in image, running with no pins");
    // An unstamped image already said so in Init(); this line is for a stamped descriptor with bad pins.
    if (!OpenShock::ValidateBoardDescriptor() && OpenShock::Board::IsStamped()) {
      OS_LOGE(TAG, "Board descriptor has problems (see above); affected features will stay disabled");
    }
  }

  // Log capture first, so the rest of the boot is captured; with SD card support this also mounts the card and copies a
  // pending crash dump to it, which belongs before WiFi starts.
  OpenShock::Diagnostics::Init();

  OpenShock::Config::Init();

  if (!OpenShock::Events::Init()) {
    OS_PANIC(TAG, "Unable to initialize Events");
  }

  if (!OpenShock::OtaUpdateManager::Init()) {
    OS_PANIC(TAG, "Unable to initialize OTA Update Manager");
  }

  if (OpenShock::OtaUpdateManager::IsValidatingApp()) {
    otaSetup();
  } else {
    appSetup();
  }

  while (true) {
    OpenShock::GatewayConnectionManager::Update();
    OpenShock::Diagnostics::Update([] { return OpenShock::GatewayConnectionManager::IsConnected(); });

    vTaskDelay(5);  // 5 ticks update interval
  }
}
