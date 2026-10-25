# Audio inalámbrico — Raspberry Pi Zero 2 W + INMP441 + nRF24L01

Proyecto de telecomunicaciones (Laboratorio 201 IE, UCR).  
Captura voz en un nodo, la transmite por radio en banda ISM 2.4 GHz y la reproduce en el otro nodo.

---

## Hardware por nodo

| Componente | Función |
|---|---|
| Raspberry Pi Zero 2 W | Microprocesador único del nodo |
| nRF24L01 | Enlace de radio ISM 2.4 GHz (SPI) |
| INMP441 | Micrófono digital I2S (solo emisor) |
| PCM5102 + amplificador clase D | DAC I2S + salida al parlante (solo receptor) |
| Botón en GPIO17 | Inicia captura (solo emisor) |
| LED en GPIO27 | Indicador de estado (ambos nodos) |

---

## Conexiones

### nRF24L01 (ambos nodos)

| nRF24L01 | Raspberry Pi Zero 2 W |
|---|---|
| VCC | Pin 1 — 3.3 V |
| GND | Pin 9 — GND |
| CE | Pin 15 — GPIO22 |
| CSN | Pin 24 — GPIO8 / CE0 |
| SCK | Pin 23 — GPIO11 / SCLK |
| MOSI | Pin 19 — GPIO10 / MOSI |
| MISO | Pin 21 — GPIO9 / MISO |
| IRQ | Sin conectar |

> Agregar un capacitor de 10 µF a 47 µF entre VCC y GND del nRF24L01 para estabilizar la alimentación.

### INMP441 (solo emisor)

| INMP441 | Raspberry Pi Zero 2 W |
|---|---|
| VDD | Pin 1 — 3.3 V |
| GND | Pin 9 — GND |
| SCK / BCLK | Pin 12 — GPIO18 |
| WS / LRCLK | Pin 35 — GPIO19 |
| SD | Pin 38 — GPIO20 |
| L/R | GND (canal izquierdo) |

### Botón (solo emisor)

```
GPIO17 (pin 11) ──── botón ──── GND
GPIO17 (pin 11) ──── resistencia 10 kΩ ──── 3.3 V (pin 1)
```

Botón activo en bajo: presionado = 0, no presionado = 1.

### LED (ambos nodos)

```
GPIO27 (pin 13) ──── resistencia 330 Ω ──── LED ──── GND
```

---

## Protocolo de transmisión

Cada paquete mide exactamente 32 bytes sobre el Enhanced ShockBurst del nRF24L01.

```
[ type: 1B ][ seq: 2B ][ len: 1B ][ payload: 28B ]
```

| Tipo | Valor | Contenido del payload |
|---|---|---|
| START | 1 | sample_rate_hz, record_seconds, channels, bits_per_sample, total_audio_bytes, total_data_packets |
| DATA | 2 | Hasta 28 bytes de audio PCM (int16_t, little-endian) |
| END | 3 | Vacío — indica fin de transmisión |
| ABORT | 4 | Vacío — emisor canceló la transmisión |

**Parámetros de audio:** 8000 Hz, mono, 16 bits, 5 segundos → 80 000 bytes → 2858 paquetes DATA.

**Configuración de radio:** canal 76 (2476 MHz), 2 Mbps, CRC-16, AutoACK habilitado, hasta 10 reintentos hardware + 8 reintentos por software por paquete.

---

## Señalización LED

| Estado | Patrón |
|---|---|
| En espera de botón / START | Parpadeo lento (1 s de ciclo) |
| Capturando audio | Encendido fijo |
| Transmitiendo / recibiendo | Encendido fijo |
| Inicio de captura | 3 parpadeos rápidos |
| Transmisión o reproducción finalizada | 5 parpadeos rápidos |
| Reproduciendo audio | Encendido fijo |

---

## Instalación

### Dependencias del sistema

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y git cmake make g++ build-essential libasound2-dev alsa-utils
```

### Activar SPI

```bash
sudo raspi-config
# Interface Options → SPI → Enable
sudo reboot
```

### Instalar librería RF24

```bash
wget https://raw.githubusercontent.com/nRF24/.github/main/installer/install.sh
chmod +x install.sh
./install.sh
```

---

## Compilar y ejecutar

### Emisor

```bash
cd emisor
make
sudo ./transmitter plughw:0,0
```

Si el micrófono aparece en otro dispositivo ALSA:

```bash
arecord -l        # lista dispositivos de captura disponibles
sudo ./transmitter plughw:1,0   # ajustar según la lista
```

### Receptor

```bash
cd receptor
make
sudo ./receiver default
```

Si la salida de audio usa otro dispositivo ALSA:

```bash
aplay -l          # lista dispositivos de reproducción disponibles
sudo ./receiver plughw:1,0
```

---

## Estructura del repositorio

```
rpi-nrf24-audio/
├── common/
│   ├── packet.h          # Definición del protocolo y constantes de audio
│   ├── audio_common.h    # Captura y reproducción ALSA, escritura WAV
│   └── gpio_sysfs.h      # Control de GPIO vía /sys/class/gpio
├── emisor/
│   ├── transmitter.cpp   # Lógica del emisor
│   └── Makefile
├── receptor/
│   ├── receiver.cpp      # Lógica del receptor
│   └── Makefile
└── README.md
```

---

## Archivos generados en ejecución

| Archivo | Generado por | Contenido |
|---|---|---|
| `captura_tx.wav` | Emisor | Audio capturado antes de transmitir |
| `recibido_rx.wav` | Receptor | Audio reconstruido a partir de los paquetes recibidos |

---

## Limitaciones conocidas

- El GPIO se controla vía `/sys/class/gpio` (interfaz legacy). Se requiere ejecutar con `sudo`.
- La duración de la grabación es fija en 5 segundos (constante `RECORD_SECONDS` en `packet.h`).