// Check if Arduino.h exists, if not instruct the developer to remove "arduino-esp32" from the useragent and replace it with "ESP-IDF", after which the developer may remove this warning.
#if defined(__has_include) && !__has_include("Arduino.h")
#warning \
  "Let it be known that Arduino hath finally been cast aside in favor of the noble ESP-IDF! I beseech thee, kind sir or madam, wouldst thou kindly partake in the honors of expunging 'arduino-esp32' from yonder useragent aloft, and in its stead, bestow the illustrious 'ESP-IDF'?"
#endif