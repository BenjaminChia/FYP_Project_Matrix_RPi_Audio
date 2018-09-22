/*#pragma once
#include "../matrix/cpp/driver/direction_of_arrival.h"
#include "../matrix/cpp/driver/everloop.h"
#include "../matrix/cpp/driver/everloop_image.h"
#include "../matrix/cpp/driver/matrixio_bus.h"
#include "../matrix/cpp/driver/microphone_array.h"
#include "../matrix/cpp/driver/microphone_core.h"

namespace matrixCreator = matrix_hal;
using namespace std;

extern std::unique_ptr<libsocket::inet_stream> tcpConnection;
extern LedController *LedCon;
extern matrixCreator::MicrophoneArray microphoneArray;

namespace GoogleSpeech {
	void setup();
	void *run(void *null);
	void stop();
	extern int channelToSend;
}*/