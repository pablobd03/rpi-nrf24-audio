#ifndef PACKET_H
#define PACKET_H

#include <cstdint>
#include <cstddef>

/*
  Protocolo digital simple sobre NRF24L01.
  El NRF24L01 permite payloads de hasta 32 bytes,
  por eso cada paquete de radio mide exactamente 32 bytes.

  type:
    1 = START : anuncia inicio de transmisión y metadatos del audio.
    2 = DATA  : contiene bytes de audio PCM.
    3 = END   : anuncia fin de transmisión.
    4 = ABORT : transmisión cancelada.
*/

static constexpr uint8_t PKT_START = 1;
static constexpr uint8_t PKT_DATA  = 2;
static constexpr uint8_t PKT_END   = 3;
static constexpr uint8_t PKT_ABORT = 4;

static constexpr uint8_t AUDIO_PAYLOAD_BYTES = 28;
static constexpr uint8_t NRF_PACKET_BYTES = 32;

static constexpr uint32_t SAMPLE_RATE_HZ = 8000;
static constexpr uint16_t RECORD_SECONDS = 5;
static constexpr uint8_t CHANNELS = 1;
static constexpr uint8_t BITS_PER_SAMPLE = 16;

static constexpr uint32_t TOTAL_SAMPLES = SAMPLE_RATE_HZ * RECORD_SECONDS;
static constexpr uint32_t TOTAL_AUDIO_BYTES = TOTAL_SAMPLES * sizeof(int16_t);
static constexpr uint16_t TOTAL_DATA_PACKETS =
    (TOTAL_AUDIO_BYTES + AUDIO_PAYLOAD_BYTES - 1) / AUDIO_PAYLOAD_BYTES;

#pragma pack(push, 1)
struct RadioPacket {
    uint8_t type;                         // START, DATA, END, ABORT
    uint16_t seq;                         // número de secuencia
    uint8_t len;                          // bytes válidos en payload
    uint8_t payload[AUDIO_PAYLOAD_BYTES]; // datos o metadatos
};

struct StartInfo {
    uint32_t sample_rate_hz;
    uint16_t record_seconds;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint32_t total_audio_bytes;
    uint16_t total_data_packets;
};
#pragma pack(pop)

static_assert(sizeof(RadioPacket) == NRF_PACKET_BYTES, "RadioPacket debe medir 32 bytes");
static_assert(sizeof(StartInfo) <= AUDIO_PAYLOAD_BYTES, "StartInfo no debe superar el payload");

#endif
