#include "serial/command_handlers/common.h"

#include <cJSON.h>

using namespace OpenShock;

void Serial::Util::respError(bool isAutomated, const char* format, ...)
{
  if (isAutomated) {
    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
      ::Serial.printf(CLEAR_LINE "Failed to create JSON object\n");
      return;
    }

    cJSON_AddStringToObject(root, "error", msg);

    char* out = cJSON_PrintUnformatted(root);

    if (out != nullptr) {
      ::Serial.printf("$SYS$%s\n", out);
      cJSON_free(out);
    }

    cJSON_Delete(root);
    return;
  }

  ::Serial.printf(CLEAR_LINE "Error: %s\n", msg);
}
