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
  RECEPTOR:
    - Permanece en espera.
    - Recibe START + DATA + END desde NRF24L01.
    - Reconstruye el audio en memoria.
    - Guarda recibido_rx.wav.
    - Reproduce el audio usando ALSA.

  Conexiones sugeridas:
    NRF24L01:
      CE  -> GPIO22
      CSN -> CE0 / GPIO8
    LED:
      GPIO27 -> resistencia 330 ohm -> LED -> GND.
    Salida de audio:
      HDMI, tarjeta USB de audio o DAC/amplificador I2S.
*/

static constexpr int GPIO_LED = 27;

static const uint8_t ADDRESS[6] = "AUDIO";

// RF24(CE, CSN). En Linux, CSN=0 normalmente equivale a /dev/spidev0.0.
RF24 radio(22, 0);

void configure_radio_rx() {
    if (!radio.begin()) {
        throw std::runtime_error("No se detectó el NRF24L01 en el receptor");
    }

    radio.setChannel(76);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_LOW);
    radio.setRetries(3, 10);
    radio.setAutoAck(true);
    radio.setCRCLength(RF24_CRC_16);
    radio.setPayloadSize(sizeof(RadioPacket));

    radio.openReadingPipe(1, ADDRESS);
    radio.startListening();
}

bool read_packet(RadioPacket& packet) {
    if (!radio.available()) {
        return false;
    }

    radio.read(&packet, sizeof(packet));
    return true;
}

void wait_for_start(GPIOPin& led, StartInfo& info) {
    std::cout << "Receptor en espera de START...\n";

    while (true) {
        RadioPacket packet{};

        if (read_packet(packet)) {
            if (packet.type == PKT_START && packet.len == sizeof(StartInfo)) {
                std::memcpy(&info, packet.payload, sizeof(StartInfo));

                std::cout << "START recibido.\n";
                std::cout << "Frecuencia: " << info.sample_rate_hz << " Hz\n";
                std::cout << "Duración: " << info.record_seconds << " s\n";
                std::cout << "Bytes de audio: " << info.total_audio_bytes << "\n";
                std::cout << "Paquetes DATA esperados: "
                          << info.total_data_packets << "\n";

                return;
            }
        }

        // Parpadeo lento: receptor vivo y en espera.
        led.write_value(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        led.write_value(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(920));
    }
}

std::vector<int16_t> receive_audio(GPIOPin& led, const StartInfo& info) {
    std::vector<uint8_t> audio_bytes(info.total_audio_bytes, 0);

    uint32_t received_bytes = 0;
    uint16_t expected_seq = 1;
    bool finished = false;

    auto last_packet_time = std::chrono::steady_clock::now();

    led.write_value(1);

    while (!finished) {
        RadioPacket packet{};

        if (read_packet(packet)) {
            last_packet_time = std::chrono::steady_clock::now();

            if (packet.type == PKT_DATA) {
                if (packet.seq != expected_seq) {
                    std::cerr << "Aviso: secuencia esperada "
                              << expected_seq << ", recibida "
                              << packet.seq << ". Se continúa.\n";
                    expected_seq = packet.seq;
                }

                uint32_t offset = static_cast<uint32_t>(packet.seq - 1) *
                                  AUDIO_PAYLOAD_BYTES;

                if (offset < audio_bytes.size()) {
                    uint32_t writable = audio_bytes.size() - offset;
                    uint32_t copy_len = packet.len < writable ? packet.len : writable;

                    std::memcpy(audio_bytes.data() + offset,
                                packet.payload,
                                copy_len);

                    received_bytes += copy_len;
                }

                expected_seq = packet.seq + 1;

                if ((packet.seq % 100) == 0) {
                    std::cout << "Paquetes recibidos aprox: "
                              << packet.seq << " / "
                              << info.total_data_packets << "\n";
                }
            } else if (packet.type == PKT_END) {
                std::cout << "END recibido.\n";
                finished = true;
            } else if (packet.type == PKT_ABORT) {
                throw std::runtime_error("El emisor canceló la transmisión");
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto silence_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              now - last_packet_time)
                              .count();

        if (silence_ms > 10000) {
            throw std::runtime_error("Tiempo de espera agotado durante recepción");
        }
    }

    led.write_value(0);

    std::cout << "Bytes recibidos/copied: " << received_bytes
              << " de " << info.total_audio_bytes << "\n";

    std::vector<int16_t> samples(info.total_audio_bytes / sizeof(int16_t));
    std::memcpy(samples.data(), audio_bytes.data(), info.total_audio_bytes);
    return samples;
}

int main(int argc, char** argv) {
    std::string playback_device = "default";

    if (argc >= 2) {
        playback_device = argv[1];
    }

    try {
        GPIOPin led(GPIO_LED, "out");
        configure_radio_rx();

        std::cout << "=== RECEPTOR AUDIO NRF24L01 ===\n";
        std::cout << "Dispositivo ALSA de reproducción: " << playback_device << "\n";

        while (true) {
            StartInfo info{};
            wait_for_start(led, info);

            if (info.sample_rate_hz != SAMPLE_RATE_HZ ||
                info.channels != CHANNELS ||
                info.bits_per_sample != BITS_PER_SAMPLE) {
                std::cerr << "Advertencia: formato recibido distinto al esperado.\n";
            }

            std::vector<int16_t> samples = receive_audio(led, info);

            write_wav_file("recibido_rx.wav", samples, info.sample_rate_hz);
            std::cout << "Archivo reconstruido: recibido_rx.wav\n";

            std::cout << "Reproduciendo audio recibido.\n";
            blink(led, 3, 100);
            led.write_value(1);
            play_audio_alsa(samples, playback_device);
            led.write_value(0);

            std::cout << "Reproducción finalizada. Volviendo a espera.\n";
            blink(led, 5, 80);
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR RECEPTOR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
