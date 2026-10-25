#ifndef GPIO_SYSFS_H
#define GPIO_SYSFS_H

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

/*
  Control GPIO simple mediante /sys/class/gpio.
  Ejecutar los programas con sudo.
*/

class GPIOPin {
public:
    GPIOPin(int gpio_number, const std::string& direction)
        : gpio(gpio_number), gpio_path("/sys/class/gpio/gpio" + std::to_string(gpio_number)) {
        export_pin();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        set_direction(direction);
    }

    void write_value(int value) const {
        std::ofstream value_file(gpio_path + "/value");
        if (!value_file) {
            std::cerr << "No se pudo escribir GPIO" << gpio << "\n";
            return;
        }
        value_file << (value ? "1" : "0");
    }

    int read_value() const {
        std::ifstream value_file(gpio_path + "/value");
        if (!value_file) {
            std::cerr << "No se pudo leer GPIO" << gpio << "\n";
            return 1;
        }
        int value = 1;
        value_file >> value;
        return value;
    }

private:
    int gpio;
    std::string gpio_path;

    void export_pin() const {
        if (std::ifstream(gpio_path + "/value")) {
            return; // ya estaba exportado
        }

        std::ofstream export_file("/sys/class/gpio/export");
        if (!export_file) {
            std::cerr << "No se pudo exportar GPIO" << gpio
                      << ". Ejecute con sudo.\n";
            return;
        }
        export_file << gpio;
    }

    void set_direction(const std::string& direction) const {
        std::ofstream direction_file(gpio_path + "/direction");
        if (!direction_file) {
            std::cerr << "No se pudo configurar dirección GPIO" << gpio << "\n";
            return;
        }
        direction_file << direction;
    }
};

inline void blink(GPIOPin& led, int times, int delay_ms) {
    for (int i = 0; i < times; ++i) {
        led.write_value(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        led.write_value(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
}

#endif
