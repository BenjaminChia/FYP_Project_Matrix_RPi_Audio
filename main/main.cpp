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
#include <unistd.h>
#include <sstream>
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

//#include "../SharedResources.cpp"
#define BUFFER_SAMPLES_PER_CHANNEL	16000 //1 second of recording
#define STREAMING_CHANNELS			8 //Maxmium 8 channels
const int32_t bufferByteSize = STREAMING_CHANNELS * BUFFER_SAMPLES_PER_CHANNEL * sizeof(int16_t);

#define HOST_NAME_LENGTH			20 //maximum number of characters for host name
#define COMMAND_LENGTH				1

DEFINE_int32(sampling_frequency, 16000, "Sampling Frequency");
DEFINE_int32(duration, 10, "Interrupt after N seconds");
DEFINE_int32(gain, 5, "Microphone Gain");
DEFINE_bool(big_menu, true, "Include 'advanced' options in the menu listing");
using namespace std;
namespace hal = matrix_hal;

int led_offset[] = { 23, 27, 32, 1, 6, 10, 14, 19 };
int lut[] = { 1, 2, 10, 200, 10, 2, 1 };
char sysInfo[COMMAND_LENGTH + HOST_NAME_LENGTH];

char* hostname = &sysInfo[1];

int main(int argc, char *agrv[]) {
	google::ParseCommandLineFlags(&argc, &agrv, true);
	
	//initialization of bus
	hal::MatrixIOBus bus;
	if (!bus.Init()) return false;
	if (!bus.IsDirectBus()) return false;
	//set sampling rate and sec to record
	
	int sampling_rate = FLAGS_sampling_frequency;
	int seconds_to_record = FLAGS_duration;

	// Microhone Array Configuration
	hal::MicrophoneArray mics;
	mics.Setup(&bus);
	mics.SetSamplingRate(sampling_rate);
	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);

	mics.ShowConfiguration();
	std::cout << "Duration : " << seconds_to_record << "s" << std::endl;

	// Mic Core setup
	hal::MicrophoneCore mic_core(mics);
	mic_core.Setup(&bus);

	//Everloop
	hal::Everloop everloop;
	everloop.Setup(&bus);

	//Everloop Image
	hal::EverloopImage image1d(bus.MatrixLeds());

	//buffer setup
	int16_t buffer[mics.Channels() + 1]
		[mics.SamplingRate() + mics.NumberOfSamples()];

	//DOA setup
	hal::DirectionOfArrival doa(mics);
	doa.Init();
	int mic;

	mics.CalculateDelays(0, 0, 1000, 320 * 1000);

	ofstream os[mics.Channels() + 1];
	uint32_t bufferSwitch = 0;
	char dateAndTime[16];
	struct tm tm;
	
	time_t t = time(NULL);
	tm = *localtime(&t);
	gethostname(hostname, HOST_NAME_LENGTH);
	sprintf(dateAndTime, "%d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);
	ostringstream filenameStream;
	filenameStream << "/home/pi/Recordings/" << hostname << "_" << dateAndTime << "_8ch.wav";
	string filename = filenameStream.str();
	cout << filename << endl;

	ofstream file(filename, std::ofstream::binary);
	struct WaveHeader {
		//RIFF chunk
		char RIFF[4] = { 'R', 'I', 'F', 'F' };
		uint32_t overallSize;						// overall size of file in bytes
		char WAVE[4] = { 'W', 'A', 'V', 'E' };		// WAVE string

													//fmt subchunk
		char fmt[4] = { 'f', 'm', 't', ' ' };		// fmt string with trailing null char
		uint32_t fmtLength = 16;					// length of the format data
		uint16_t audioFormat = 1;					// format type. 1-PCM, 3- IEEE float, 6 - 8bit A law, 7 - 8bit mu law
		uint16_t numChannels = 8;					// no.of channels
		uint32_t samplingRate = 16000;				// sampling rate (blocks per second)
		uint32_t byteRate = 256000;					// SampleRate * NumChannels * BitsPerSample/8
		uint16_t blockAlign = 16;					// NumChannels * BitsPerSample/8
		uint16_t bitsPerSample = 16;				// bits per sample, 8- 8bits, 16- 16 bits etc

													//data subchunk
		char data[4] = { 'd', 'a', 't', 'a' };		// DATA string or FLLR string
		uint32_t dataSize;							// NumSamples * NumChannels * BitsPerSample/8 - size of the next chunk that will be read
	} header;

	
	header.dataSize = bufferByteSize ;
	header.overallSize = header.dataSize + 36;
	file.seekp(0);
	file.write((const char*)&header, sizeof(WaveHeader));
	file.close();

	for (uint16_t c = 8; c < mics.Channels() + 1; c++) {
		string filename = "mic_" + std::to_string(mics.SamplingRate()) +
			"_s16le_channel_" + std::to_string(c) + ".raw";
		os[c].open(filename, ofstream::binary);
	}
	

	//DOA lights
	uint32_t samples = 0;
	//record + DOA
	try
	{

	}
	catch (const std::exception&)
	{
			
	}
	for (int s = 0; s < seconds_to_record; s++) {
		for (;;) {
			mics.Read(); /* Reading 8-mics buffer from de FPGA */

			//Recorder starts
			for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {
				for (uint16_t c = 0; c < mics.Channels(); c++) { /* mics.Channels()=8 */
					buffer[c][samples] = mics.At(s, c);
				}
				buffer[mics.Channels()][samples] = mics.Beam(s);
				samples++;
			}//recording ends
			//file.write((const char*)buffer[bufferSwitch], bufferByteSize);
			//DOA part
			doa.Calculate();
			mic = doa.GetNearestMicrophone();
			for (hal::LedValue &led : image1d.leds) {
				led.blue = 0;
			}

			for (int i = led_offset[mic] - 3, j = 0; i < led_offset[mic] + 3; ++i, ++j) {
				if (i < 0) {
					image1d.leds[image1d.leds.size() + i].blue = lut[j];
				}
				else {
					image1d.leds[i % image1d.leds.size()].blue = lut[j];
				}

				everloop.Write(&image1d);
			}//DOA part end

			//write to file
			if (samples >= mics.SamplingRate()) {
				for (uint16_t c = 8; c < mics.Channels() + 1; c++) {
					os[c].write((const char *)buffer[c], samples * sizeof(int16_t));
				}
				samples = 0;
				break;
			}
		}
	}
	/*
	*/
	return 0;
}

			
	