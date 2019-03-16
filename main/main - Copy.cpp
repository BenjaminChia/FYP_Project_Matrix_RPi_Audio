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
#include "sys/socket.h"
#include "bits/socket.h"
#include <stdio.h>
#include <malloc.h>
#include <math.h>

#include <libsocket/inetserverstream.hpp>
#include <libsocket/inetserverdgram.hpp>
#include <libsocket/inetclientdgram.hpp>
#include <libsocket/exception.hpp>

#include "../matrix/cpp/driver/direction_of_arrival.h"
#include "../matrix/cpp/driver/everloop.h"
#include "../matrix/cpp/driver/everloop_image.h"
#include "../matrix/cpp/driver/matrixio_bus.h"
#include "../matrix/cpp/driver/microphone_array.h"
#include "../matrix/cpp/driver/microphone_core.h"

//#include "sharedResources.h"
//#include "../SharedResources.cpp"
#define BUFFER_SAMPLES_PER_CHANNEL	16000 //1 second of recording
#define STREAMING_CHANNELS			1 //Maxmium 8 channels
const int32_t bufferByteSize = STREAMING_CHANNELS * BUFFER_SAMPLES_PER_CHANNEL * sizeof(int16_t);

#define HOST_NAME_LENGTH			20 //maximum number of characters for host name
#define COMMAND_LENGTH				1

void *record2Remote(void* null);
void *record2Disk(void* null);
void * recorder(void * null);
void *udpBroadcastReceiver(void *null);
void *arrivalDirection(void* null);

bool pcConnected = false;
bool recording;

char sysInfo[COMMAND_LENGTH + HOST_NAME_LENGTH];
char commandArgument;
char* status = &sysInfo[0];
char* hostname = &sysInfo[1];

int16_t buffer[2][BUFFER_SAMPLES_PER_CHANNEL][STREAMING_CHANNELS];

pthread_mutex_t bufferMutex[2] = { PTHREAD_MUTEX_INITIALIZER };

namespace hal = matrix_hal;
DEFINE_int32(sampling_frequency, 16000, "Sampling Frequency");
DEFINE_int32(duration, 10, "Interrupt after N seconds");
DEFINE_int32(gain, 3, "Microphone Gain");
DEFINE_bool(big_menu, true, "Include 'advanced' options in the menu listing");
using namespace std;



int led_offset[] = { 23, 27, 32, 1, 6, 10, 14, 19 };
int lut[] = { 1, 2, 10, 200, 10, 2, 1 };

std::unique_ptr<libsocket::inet_stream> tcpConnection;

hal::MicrophoneArray mics;
hal::MicrophoneArray doa_mics;
hal::MatrixIOBus bus;
hal::MicrophoneCore mic_core(mics);
hal::MicrophoneCore doa_mic_core(doa_mics);
hal::Everloop everloop;
hal::EverloopImage image1d(bus.MatrixLeds());


int main(int argc, char *agrv[]) {

	gethostname(hostname, HOST_NAME_LENGTH);
	*status = 'I';
	google::ParseCommandLineFlags(&argc, &agrv, true);

	if (!bus.Init()) return false;

	if (!bus.IsDirectBus()) {
		std::cerr << "Kernel Modules has been loaded. Use ALSA examples "
			<< std::endl;
	}

	char command = '\0';


	pthread_t udpThread;
	pthread_create(&udpThread, NULL, udpBroadcastReceiver, NULL);

	//stand by
	libsocket::inet_stream_server tcpServer("0.0.0.0", "8000", LIBSOCKET_IPv4);
	cout << hostname << " - TCP server listening :8000\n";

	//wait for network connection
	while (true) {
		try {

			tcpConnection = tcpServer.accept2();

			//connected
		
			pcConnected = true;

			tcpConnection->snd(sysInfo, COMMAND_LENGTH + HOST_NAME_LENGTH);
			//syncTime();

			pthread_t recorderThread;

			while (tcpConnection->rcv(&command, 1, MSG_WAITALL)) {
				cout << command << endl;
				switch (command) {
				case 'N': {//record to network
					if (*status == 'I') {
						*status = 'N';
						tcpConnection->rcv(&commandArgument, 1, MSG_WAITALL);
						pthread_create(&recorderThread, NULL, recorder, NULL);
						break;
					}
				}
				case 'L': {//record to disk
					if (*status == 'I') {
						*status = 'L';
						tcpConnection->rcv(&commandArgument, 1, MSG_WAITALL);
						pthread_create(&recorderThread, NULL, recorder, NULL);
					}

					break;
				}


				case 'I': { //stop everything

					switch (*status) {
					case 'I': break;
					case 'N': {
						recording = false;
						pthread_join(recorderThread, NULL);
						break;
					}
					case 'L': {
						recording = false;
						pthread_join(recorderThread, NULL);
						break;
					}
					}

					*status = 'I';
					break;
				}

				case '\0': {
					cout << "\0" << endl;
					break;
				}

				default: cout << "unrecognized command" << endl;
				}
				command = '\0';
			}
		}

		catch (const libsocket::socket_exception& exc)
		{
			//error
			cout << exc.mesg << endl;
		}
		pcConnected = false;
		cout << "Remote PC at " << tcpConnection->gethost() << ":" << tcpConnection->getport() << " disconnected" << endl;
		tcpConnection->destroy();
	}

	sleep(1);


	return 0;
}

void *udpBroadcastReceiver(void *null) {
	string remoteIP;
	string remotePort;
	string buffer;

	remoteIP.resize(16);
	remotePort.resize(16);
	buffer.resize(32);

	//start server
	libsocket::inet_dgram_server udpServer("0.0.0.0", "8001", LIBSOCKET_IPv4);
	cout << hostname << " - UDP server listening :8001\n";

	while (true) {
		try {
			udpServer.rcvfrom(buffer, remoteIP, remotePort);

			if (!pcConnected && buffer.compare("live long and prosper") == 0) {
				cout << "Remote PC at " << remoteIP << endl;
				udpServer.sndto("peace and long life", remoteIP, remotePort);
			}

		}
		catch (const libsocket::socket_exception& exc)
		{
			cout << exc.mesg << endl;
		}
	}

}
void *recorder(void* null) {

	recording = true;

	pthread_t doA;
	pthread_t workerThread;
	switch (*status) {
	case 'N':pthread_create(&workerThread, NULL, record2Remote, NULL); break;
	case 'L':pthread_create(&workerThread, NULL, record2Disk, NULL); break;
	}
	pthread_create(&doA, NULL, arrivalDirection, NULL);

	cout << "------ Recording starting ------" << endl;
	pthread_join(doA, NULL);
	pthread_join(workerThread, NULL);
	cout << "------ Recorder ended ------" << endl;
	
	pthread_exit(NULL);

}
void *record2Disk(void* null) {
	int sampling_rate = FLAGS_sampling_frequency;
	mics.Setup(&bus);
	cout << "bus setup" << endl;
	mics.SetSamplingRate(sampling_rate);
	mics.ShowConfiguration();
	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);
	mic_core.Setup(&bus);
	mics.CalculateDelays(0, 0, 1000, 320 * 1000);
	
	cout << "record 2 Disk " << endl;
	char command = '\0';
	
	int16_t buffer1[mics.Channels() + 1]
		[mics.SamplingRate() + mics.NumberOfSamples()];

	ofstream os[mics.Channels() + 1];

	char dateAndTime[16];
	struct tm tm;

	for (uint16_t c = 8; c < mics.Channels() + 1; c++) {
		string filename = "mic_" + std::to_string(mics.SamplingRate()) +
			"_s16le_channel_" + std::to_string(c) + ".raw";
		os[c].open(filename, ofstream::binary);
	}
	time_t t = time(NULL);
	tm = *localtime(&t);

	sprintf(dateAndTime, "%d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	ostringstream filenameStream;
	filenameStream << "/home/pi/BenRecordings/" << hostname << "_" << dateAndTime << "_8ch.wav";
	string filename = filenameStream.str();
	cout << filename << endl;
	uint32_t counter = 0;
	ofstream file(filename, std::ofstream::binary);

	//DOA lights
	uint32_t samples = 0;
	//record + DOA
	cout << "------ Recording starting ------" << endl;

	
	struct WaveHeader {
		//RIFF chunk
		char RIFF[4] = { 'R', 'I', 'F', 'F' };
		int overallSize;						// overall size of file in bytes
		char WAVE[4] = { 'W', 'A', 'V', 'E' };		// WAVE string

													//fmt subchunk
		char fmt[4] = { 'f', 'm', 't', ' ' };		// fmt string with trailing null char
		int fmtLength = 16;					// length of the format data
		short int  audioFormat = 1;					// format type. 1-PCM, 3- IEEE float, 6 - 8bit A law, 7 - 8bit mu law
		short int  numChannels = 1;					// no.of channels
		int sampleRate = 16000;				// sampling rate (blocks per second)
		int byteRate = sampleRate *2 ;					// SampleRate * NumChannels * BitsPerSample/8
		short int blockAlign = 2;					// NumChannels * BitsPerSample/8
		short int bitsPerSample = 16;				// bits per sample, 8- 8bits, 16- 16 bits etc

													//data subchunk
		char data[4] = { 'd', 'a', 't', 'a' };		// DATA string or FLLR string
		int dataSize;							// NumSamples * NumChannels * BitsPerSample/8 - size of the next chunk that will be read
	} header;
	cout << &header << "<header | size of > " << sizeof(WaveHeader) << endl;
	file.write((const char*)&header, sizeof(WaveHeader));

	do{
		mics.Read(); /* Reading 8-mics buffer from de FPGA */

		
		//Recorder starts
		for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {
			buffer1[8][samples] = mics.Beam(s);
			samples++;
			counter++;
		}


		if (samples >= mics.SamplingRate()) {
			for (uint16_t c = 8; c < mics.Channels() + 1; c++) {
				os[c].write((const char *)buffer1[c], samples * sizeof(int16_t));
				file.write((const char*)buffer1[c], samples * sizeof(int16_t));
			}
			samples = 0;
		}
		cout << recording << endl;
	}while (recording);

	cout << header.dataSize << "dataSize" << endl;
	cout << header.overallSize << "dataSize" << endl;
	header.dataSize = counter * 2 ;
	header.overallSize = header.dataSize + 36;
	cout << header.dataSize << "dataSize"<<endl;
	cout << header.overallSize << "dataSize" << endl;
	file.seekp(0);
	cout << &header << "<header | size of > "<< sizeof(WaveHeader) << endl ;
	file.write((const char*)&header, sizeof(WaveHeader));
	file.close();

	cout << "------ Recording ended ------" << endl;
	cout << recording << endl;
	pthread_exit(NULL);
	
	
} 

void *record2Remote(void* null)
{
	int sampling_rate = FLAGS_sampling_frequency;
	mics.Setup(&bus);
	cout << "bus setup" << endl;
	mics.SetSamplingRate(sampling_rate);
	mics.ShowConfiguration();
	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);
	mic_core.Setup(&bus);
	mics.CalculateDelays(0, 0, 1000, 320 * 1000);
	uint32_t samples = 0;
	uint32_t counter = 0;
	int16_t buffer1[mics.Channels() + 1]
		[mics.SamplingRate() + mics.NumberOfSamples()];

	//Recorder starts

	while (recording) {
		try {
			for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {
				buffer1[8][samples] = mics.Beam(s);
				samples++;
				counter++;
			}

			if (samples >= mics.SamplingRate()) {
				tcpConnection->snd(buffer1[8], samples * sizeof(int16_t));
				samples = 0;
			}

		}
		catch (const libsocket::socket_exception& exc)
		{
			//network disconnection means recording completed
			recording = false; //set flag
			*status = 'I';
			break;
		}


		

	
	}

	pthread_exit(NULL);//terminate itself
}

void *arrivalDirection(void* null) {
	//DOA setup
	int sampling_rate = FLAGS_sampling_frequency;
	doa_mics.Setup(&bus);
	cout << "bus setup" << endl;
	doa_mics.SetSamplingRate(sampling_rate);
	doa_mics.ShowConfiguration();
	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);
	doa_mic_core.Setup(&bus);
	everloop.Setup(&bus);
	hal::DirectionOfArrival doa(doa_mics);
	doa.Init();
	int mic;

	while (recording) {
		doa_mics.Read();
		doa.Calculate();
		mic = doa.GetNearestMicrophone();
		for (hal::LedValue &led : image1d.leds) 
			led.blue = 0;
		

		for (int i = led_offset[mic] - 3, j = 0; i < led_offset[mic] + 3; ++i, ++j) {
			if (i < 0) {
				image1d.leds[image1d.leds.size() + i].blue = lut[j];
			}
			else {
				image1d.leds[i % image1d.leds.size()].blue = lut[j];
			}

			everloop.Write(&image1d);
		}//DOA part end
	}

	for (hal::LedValue &led : image1d.leds)
		led.blue = 0;
	everloop.Write(&image1d);
	pthread_exit(NULL);
}