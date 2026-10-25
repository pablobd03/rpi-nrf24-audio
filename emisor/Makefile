CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LIBS = -lrf24 -lasound

TARGET = transmitter
SRC = transmitter.cpp

all: $(TARGET)

$(TARGET): $(SRC) ../common/packet.h ../common/audio_common.h ../common/gpio_sysfs.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET) *.o captura_tx.wav
