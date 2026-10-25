#include <RF24/RF24.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "../common/audio_common.h"
#include "../common/gpio_sysfs.h"
#include "../common/packet.h"

/*
  EMISOR:
    - Espera botón de inicio.
    - Enciende/parpadea LED como señalización.
    - Captura 5 segundos de audio desde INMP441 usando ALSA.
    - Guarda captura_tx.wav.
    - Transmite START + DATA + END por NRF24L01.

  Conexiones sugeridas:
    NRF24L01:
      CE  -> GPIO22
      CSN -> CE0 / GPIO8
    Botón:
      GPIO17 con resistencia pull-up externa a 3.3 V y botón a GND.
    LED:
      GPIO27 -> resistencia 330 ohm -> LED -> GND.
*/

static constexpr int GPIO_BUTTON = 17;
static constexpr int GPIO_LED = 27;

static const uint8_t ADDRESS[6] = "AUDIO";

// RF24(CE, CSN).
RF24 radio(22, 0);

void configure_radio_tx() {
    if (!radio.begin()) {
        throw std::runtime_error("No se detectó el NRF24L01 en el emisor");
    }

    radio.setChannel(76);              // canal dentro de 2.4 GHz ISM
    radio.setDataRate(RF24_2MBPS);     // mayor velocidad para audio
    radio.setPALevel(RF24_PA_LOW);     // subir a RF24_PA_HIGH si hace falta alcance
    radio.setRetries(3, 10);           // delay, cantidad de reintentos por AutoACK
    radio.setAutoAck(true);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioPacket));

    radio.openWritingPipe(ADDRESS);
    radio.stopListening();
}

bool send_packet_reliable(const RadioPacket& packet) {
    constexpr int MAX_APP_RETRIES = 8;

    for (int attempt = 0; attempt < MAX_APP_RETRIES; ++attempt) {
        bool ok = radio.write(&packet, sizeof(packet));
        if (ok) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    return false;
}

void wait_for_button(GPIOPin& led, GPIOPin& button) {
    std::cout << "Emisor en espera. Presione el botón para capturar 5 s de audio.\n";

    while (true) {
        // Botón activo en bajo  (0 = presionado).
        if (button.read_value() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (button.read_value() == 0) {
                while (button.read_value() == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                return;
            }
        }

        // Parpadeo lento: sistema listo/en espera.
        led.write_value(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        led.write_value(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(900));
    }
}

void send_start_packet() {
    RadioPacket packet{};
    packet.type = PKT_START;
    packet.seq = 0;
    packet.len = sizeof(StartInfo);

    StartInfo info{};
    info.sample_rate_hz = SAMPLE_RATE_HZ;
    info.record_seconds = RECORD_SECONDS;
    info.channels = CHANNELS;
    info.bits_per_sample = BITS_PER_SAMPLE;
    info.total_audio_bytes = TOTAL_AUDIO_BYTES;
    info.total_data_packets = TOTAL_DATA_PACKETS;

    std::memcpy(packet.payload, &info, sizeof(info));

    if (!send_packet_reliable(packet)) {
        throw std::runtime_error("No se pudo enviar START");
    }
}

void send_audio_data(const std::vector<int16_t>& samples) {
    const uint8_t* audio_bytes = reinterpret_cast<const uint8_t*>(samples.data());
    uint32_t offset = 0;
    uint16_t seq = 1;

    while (offset < TOTAL_AUDIO_BYTES) {
        RadioPacket packet{};
        packet.type = PKT_DATA;
        packet.seq = seq++;

        uint32_t remaining = TOTAL_AUDIO_BYTES - offset;
        packet.len = remaining > AUDIO_PAYLOAD_BYTES ? AUDIO_PAYLOAD_BYTES : remaining;

        std::memcpy(packet.payload, audio_bytes + offset, packet.len);

        if (!send_packet_reliable(packet)) {
            throw std::runtime_error("Fallo enviando paquete DATA seq=" +
                                     std::to_string(packet.seq));
        }

        offset += packet.len;

        if ((packet.seq % 100) == 0) {
            std::cout << "Paquetes enviados: " << packet.seq
                      << " / " << TOTAL_DATA_PACKETS << "\n";
        }
    }
}

void send_end_packet() {
    RadioPacket packet{};
    packet.type = PKT_END;
    packet.seq = TOTAL_DATA_PACKETS + 1;
    packet.len = 0;

    if (!send_packet_reliable(packet)) {
        throw std::runtime_error("No se pudo enviar END");
    }
}

int main(int argc, char** argv) {
    std::string capture_device = "plughw:0,0";

    if (argc >= 2) {
        capture_device = argv[1];
    }

    try {
        GPIOPin led(GPIO_LED, "out");
        GPIOPin button(GPIO_BUTTON, "in");

        configure_radio_tx();

        std::cout << "=== EMISOR AUDIO NRF24L01 ===\n";
        std::cout << "Dispositivo ALSA de captura: " << capture_device << "\n";
        std::cout << "Formato: " << SAMPLE_RATE_HZ << " Hz, "
                  << static_cast<int>(BITS_PER_SAMPLE) << " bits, mono, "
                  << RECORD_SECONDS << " s\n";

        while (true) {
            wait_for_button(led, button);

            std::cout << "Inicio de captura.\n";
            blink(led, 3, 120);

            led.write_value(1);
            std::vector<int16_t> samples = capture_audio_alsa(capture_device);
            led.write_value(0);

            write_wav_file("captura_tx.wav", samples);
            std::cout << "Archivo almacenado: captura_tx.wav\n";

            std::cout << "Inicio de transmisión NRF.\n";
            led.write_value(1);

            send_start_packet();
            send_audio_data(samples);
            send_end_packet();

            led.write_value(0);
            std::cout << "Transmisión finalizada.\n";
            blink(led, 5, 80);
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR EMISOR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
