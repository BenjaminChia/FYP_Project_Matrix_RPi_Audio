/*
* Copyright 2016 <Admobilize>
* All rights reserved.
*/
#include <fftw3.h>
#include <gflags/gflags.h>
#include <stdint.h>
#include <string.h>
#include <wiringPi.h>
#include <pthread.h>
//#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>
#include <valarray>
#include <thread>

#include "../matrix/cpp/driver/direction_of_arrival.h"
#include "../matrix/cpp/driver/everloop.h"
#include "../matrix/cpp/driver/everloop_image.h"
#include "../matrix/cpp/driver/matrixio_bus.h"
#include "../matrix/cpp/driver/microphone_array.h"
#include "../matrix/cpp/driver/microphone_core.h"
/*
#include "../matrix/direction_of_arrival.cpp"
#include "../matrix/everloop.cpp"
#include "../matrix/matrixio_bus.cpp"
#include "../matrix/microphone_array.cpp"
#include "../matrix/microphone_core.cpp"
#include "../matrix/matrix_driver.cpp"
*/

DEFINE_int32(sampling_frequency, 16000, "Sampling Frequency");
DEFINE_int32(duration, 5, "Interrupt after N seconds");
DEFINE_int32(gain, 5, "Microphone Gain");
DEFINE_bool(big_menu, true, "Include 'advanced' options in the menu listing");

namespace hal = matrix_hal;

int led_offset[] = { 23, 27, 32, 1, 6, 10, 14, 19 };
int lut[] = { 1, 2, 10, 200, 10, 2, 1 };

int sampling_rate = FLAGS_sampling_frequency;
int seconds_to_record = FLAGS_duration;
bool recording;

//void *DOA(void* null);
void *DirectRecord(void* null);

hal::MatrixIOBus bus;
hal::Everloop everloop;
hal::EverloopImage image1d(bus.MatrixLeds());
hal::MicrophoneArray mics;
hal::MicrophoneCore mic_core(mics);
hal::DirectionOfArrival doa(mics);

int main(int argc, char *agrv[]) {
	google::ParseCommandLineFlags(&argc, &agrv, true);
	
	if (!bus.Init()) return false;
	if (!bus.IsDirectBus()) {
		std::cerr << "Kernel Modules has been loaded. Use ALSA implementation "
			<< std::endl;
		return false;
	}

	//testing my DOA
	std::cout << "after bus" << std::endl;
	int sampling_rate = FLAGS_sampling_frequency;
	everloop.Setup(&bus);
	mics.Setup(&bus);
	mics.SetSamplingRate(sampling_rate);
	mics.ShowConfiguration();
	mic_core.Setup(&bus);
	doa.Init();
	std::cout << "after doa.Init" << std::endl;
	float azimutal_angle;
	float polar_angle;
	int mic;

	std::cout << "before r = false" << std::endl;
	recording = false;
	std::cout << "after r = false" << std::endl;
	pthread_t Record;
	pthread_create(&Record, NULL, DirectRecord, NULL);
	pthread_join(Record, NULL);
	std::cout << "after r = join" << std::endl;
	while (recording) {
		mics.Read(); /* Reading 8-mics buffer from de FPGA */

		doa.Calculate();

		azimutal_angle = doa.GetAzimutalAngle() * 180 / M_PI;
		polar_angle = doa.GetPolarAngle() * 180 / M_PI;
		mic = doa.GetNearestMicrophone();

		std::cout << "azimutal angle = " << azimutal_angle
			<< ", polar angle = " << polar_angle << ", mic = " << mic
			<< std::endl;

		for (hal::LedValue &led : image1d.leds) {
			led.blue = 0;
		}

		for (int i = led_offset[mic] - 3, j = 0; i < led_offset[mic] + 3;
			++i, ++j) {
			if (i < 0) {
				image1d.leds[image1d.leds.size() + i].blue = lut[j];
			}
			else {
				image1d.leds[i % image1d.leds.size()].blue = lut[j];
			}

			everloop.Write(&image1d);
		}
	}
	std::cout << "after DOA" << std::endl;

	// Microhone Array Configuration
	//recording = false;
	//pthread_t Record, testDOA;
	//pthread_create(&Record, NULL, DirectRecord, NULL);
	//pthread_create(&testDOA, NULL, DOA, NULL);
	//pthread_join(Record, NULL);
	//pthread_join(testDOA, NULL);

	return 0;
}

/*void *DOA(void* null) {


hal::MicrophoneArray mics;
mics.Setup(&bus);
mics.SetSamplingRate(sampling_rate);
mics.ShowConfiguration();

hal::MicrophoneCore mic_core(mics);
mic_core.Setup(&bus);

hal::DirectionOfArrival doa(mics);
doa.Init();

float azimutal_angle;
float polar_angle;
int mic;

recording = false;
pthread_t Record;
pthread_create(&Record, NULL, DirectRecord, NULL);
pthread_join(Record, NULL);
while (recording) {
mics.Read(); /* Reading 8-mics buffer from de FPGA */
/*
doa.Calculate();

azimutal_angle = doa.GetAzimutalAngle() * 180 / M_PI;
polar_angle = doa.GetPolarAngle() * 180 / M_PI;
mic = doa.GetNearestMicrophone();

std::cout << "azimutal angle = " << azimutal_angle
<< ", polar angle = " << polar_angle << ", mic = " << mic
<< std::endl;

for (hal::LedValue &led : image1d.leds) {
	led.blue = 0;
}

for (int i = led_offset[mic] - 3, j = 0; i < led_offset[mic] + 3;
	++i, ++j) {
	if (i < 0) {
		image1d.leds[image1d.leds.size() + i].blue = lut[j];
	}
	else {
		image1d.leds[i % image1d.leds.size()].blue = lut[j];
	}

	everloop.Write(&image1d);
}
	}


}*/


void *DirectRecord(void* null) {
	recording = true;
	//hal::MatrixIOBus bus;
	//hal::MicrophoneArray mics;
	mics.Setup(&bus);
	mics.SetSamplingRate(sampling_rate);

	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);

	mics.ShowConfiguration();
	std::cout << "Duration : " << seconds_to_record << "s" << std::endl;

	// Microphone Core Init
	//hal::MicrophoneCore mic_core(mics);
	mic_core.Setup(&bus);

	int16_t buffer[mics.Channels() + 1]
		[mics.SamplingRate() + mics.NumberOfSamples()];

	mics.CalculateDelays(0, 0, 1000, 320 * 1000);

	std::ofstream os[mics.Channels() + 1];

	for (uint16_t c = 0; c < mics.Channels() + 1; c++) {
		std::string filename = std::to_string(c) + ".raw";
		os[c].open(filename, std::ofstream::binary);
	}

	/*
	std::thread et(
	[seconds_to_record](hal::MatrixIOBus *bus) {

	hal::Everloop everloop;
	everloop.Setup(bus);

	hal::EverloopImage image(bus->MatrixLeds());

	for (auto &led : image.leds) led.red = 10;
	everloop.Write(&image);

	int sleep = int(1000.0 * seconds_to_record / image.leds.size());

	for (auto &led : image.leds) {
	led.red = 0;
	led.green = 10;

	everloop.Write(&image);

	std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
	}
	},
	&bus);*/

	uint32_t samples = 0;
	for (int s = 0; s < seconds_to_record; s++) {
		for (;;) {
			mics.Read(); /* Reading 8-mics buffer from de FPGA */

						 /* buffering */
			for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {
				for (uint16_t c = 0; c < mics.Channels(); c++) { /* mics.Channels()=8 */
					buffer[c][samples] = mics.At(s, c);
				}
				buffer[mics.Channels()][samples] = mics.Beam(s);
				samples++;
			}

			/* write to file */
			if (samples >= mics.SamplingRate()) {
				for (uint16_t c = 0; c < mics.Channels() + 1; c++) {
					os[c].write((const char *)buffer[c], samples * sizeof(int16_t));
				}
				samples = 0;

				break;
			}
		}
		recording = false;

		pthread_exit(NULL);
	}

	//et.join();

}
