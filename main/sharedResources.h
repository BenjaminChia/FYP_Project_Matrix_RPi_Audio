#pragma once

#include "../matrix/cpp/driver/direction_of_arrival.h"
#include "../matrix/cpp/driver/everloop.h"
#include "../matrix/cpp/driver/everloop_image.h"
#include "../matrix/cpp/driver/matrixio_bus.h"
#include "../matrix/cpp/driver/microphone_array.h"
#include "../matrix/cpp/driver/microphone_core.h"

namespace hal = matrix_hal;
using namespace std;

//extern std::unique_ptr<libsocket::inet_stream> tcpConnection;
//extern LedController *LedCon;
extern hal::MicrophoneArray microphoneArray;
extern hal::MatrixIOBus bus;
extern hal::Everloop everloop;
extern hal::EverloopImage image1d(bus.MatrixLeds());
extern hal::MicrophoneArray mics;
extern hal::MicrophoneCore mic_core(mics);
extern hal::DirectionOfArrival doa(mics);

/*
namespace GoogleSpeech {
void setup();
void *run(void *null);
void stop();
extern int channelToSend;
}*/