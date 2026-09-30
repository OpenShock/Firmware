# OpenShock RF shocker protocol encoders (RMT-based).
#
# Public surface:
#   radio/rmt/Sequence.h            — RMT sequence builder
#   radio/rmt/CaiXianlinEncoder.h   — CaiXianlin protocol
#   radio/rmt/PetrainerEncoder.h    — Petrainer protocol
#   radio/rmt/Petrainer998DREncoder.h
#   radio/rmt/D80Encoder.h
#   radio/rmt/T330Encoder.h

idf_component_register(
  SRCS          "src/CaiXianlinEncoder.cpp"
                "src/D80Encoder.cpp"
                "src/Petrainer998DREncoder.cpp"
                "src/PetrainerEncoder.cpp"
                "src/Sequence.cpp"
                "src/T330Encoder.cpp"
  INCLUDE_DIRS  "include"
  PRIV_INCLUDE_DIRS "src"                    # radio/rmt/internal/Shared.h (private)
  REQUIRES      common                       # enums/*, OpenShock.h in public headers
                esp_driver_rmt               # driver/rmt_types.h (rmt_symbol_word_t) in public headers
  PRIV_REQUIRES common                       # Checksum.h (internal)
                logging                      # OS_LOG* (internal)
)
