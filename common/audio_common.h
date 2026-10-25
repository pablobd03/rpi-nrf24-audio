#ifndef AUDIO_COMMON_H
#define AUDIO_COMMON_H

#include <alsa/asoundlib.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "packet.h"

/*
  Audio usado para cumplir el proyecto:
    - PCM
    - mono
    - 8000 Hz
    - 16 bits
    - 5 segundos

  El transmisor guarda captura_tx.wav.
  El receptor reconstruye recibido_rx.wav y luego lo reproduce.
*/

inline void write_little_endian_u16(std::ofstream& f, uint16_t value) {
    f.put(static_cast<char>(value & 0xFF));
    f.put(static_cast<char>((value >> 8) & 0xFF));
}

inline void write_little_endian_u32(std::ofstream& f, uint32_t value) {
    f.put(static_cast<char>(value & 0xFF));
    f.put(static_cast<char>((value >> 8) & 0xFF));
    f.put(static_cast<char>((value >> 16) & 0xFF));
    f.put(static_cast<char>((value >> 24) & 0xFF));
}

inline void write_wav_file(const std::string& filename,
                           const std::vector<int16_t>& samples,
                           uint32_t sample_rate_hz = SAMPLE_RATE_HZ) {
    std::ofstream f(filename, std::ios::binary);
    if (!f) {
        throw std::runtime_error("No se pudo crear archivo WAV: " + filename);
    }

    const uint16_t audio_format = 1; // PCM
    const uint16_t channels = CHANNELS;
    const uint16_t bits_per_sample = BITS_PER_SAMPLE;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t byte_rate = sample_rate_hz * block_align;
    const uint32_t data_bytes = samples.size() * sizeof(int16_t);
    const uint32_t riff_size = 36 + data_bytes;

    f.write("RIFF", 4);
    write_little_endian_u32(f, riff_size);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    write_little_endian_u32(f, 16);
    write_little_endian_u16(f, audio_format);
    write_little_endian_u16(f, channels);
    write_little_endian_u32(f, sample_rate_hz);
    write_little_endian_u32(f, byte_rate);
    write_little_endian_u16(f, block_align);
    write_little_endian_u16(f, bits_per_sample);

    f.write("data", 4);
    write_little_endian_u32(f, data_bytes);
    f.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
}

inline std::vector<int16_t> capture_audio_alsa(const std::string& device) {
    snd_pcm_t* handle = nullptr;

    int err = snd_pcm_open(&handle, device.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        throw std::runtime_error("No se pudo abrir dispositivo de captura ALSA: " +
                                 std::string(snd_strerror(err)));
    }

    err = snd_pcm_set_params(handle,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS,
                             SAMPLE_RATE_HZ,
                             1,
                             500000); // latencia: 0.5 s
    if (err < 0) {
        snd_pcm_close(handle);
        throw std::runtime_error("No se pudo configurar captura ALSA: " +
                                 std::string(snd_strerror(err)));
    }

    std::vector<int16_t> samples(TOTAL_SAMPLES);
    uint32_t frames_read_total = 0;

    while (frames_read_total < TOTAL_SAMPLES) {
        const uint32_t remaining = TOTAL_SAMPLES - frames_read_total;
        const snd_pcm_uframes_t frames_to_read = remaining > 256 ? 256 : remaining;

        snd_pcm_sframes_t frames_read =
            snd_pcm_readi(handle,
                          samples.data() + frames_read_total,
                          frames_to_read);

        if (frames_read == -EPIPE) {
            snd_pcm_prepare(handle);
            continue;
        }

        if (frames_read < 0) {
            snd_pcm_close(handle);
            throw std::runtime_error("Error leyendo audio ALSA: " +
                                     std::string(snd_strerror(frames_read)));
        }

        frames_read_total += static_cast<uint32_t>(frames_read);
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    return samples;
}

inline void play_audio_alsa(const std::vector<int16_t>& samples,
                            const std::string& device) {
    snd_pcm_t* handle = nullptr;

    int err = snd_pcm_open(&handle, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        throw std::runtime_error("No se pudo abrir dispositivo de reproducción ALSA: " +
                                 std::string(snd_strerror(err)));
    }

    err = snd_pcm_set_params(handle,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS,
                             SAMPLE_RATE_HZ,
                             1,
                             500000);
    if (err < 0) {
        snd_pcm_close(handle);
        throw std::runtime_error("No se pudo configurar reproducción ALSA: " +
                                 std::string(snd_strerror(err)));
    }

    uint32_t frames_written_total = 0;
    const uint32_t total_frames = samples.size();

    while (frames_written_total < total_frames) {
        const uint32_t remaining = total_frames - frames_written_total;
        const snd_pcm_uframes_t frames_to_write = remaining > 256 ? 256 : remaining;

        snd_pcm_sframes_t frames_written =
            snd_pcm_writei(handle,
                           samples.data() + frames_written_total,
                           frames_to_write);

        if (frames_written == -EPIPE) {
            snd_pcm_prepare(handle);
            continue;
        }

        if (frames_written < 0) {
            snd_pcm_close(handle);
            throw std::runtime_error("Error reproduciendo audio ALSA: " +
                                     std::string(snd_strerror(frames_written)));
        }

        frames_written_total += static_cast<uint32_t>(frames_written);
    }

    snd_pcm_drain(handle);
    snd_pcm_close(handle);
}

#endif
