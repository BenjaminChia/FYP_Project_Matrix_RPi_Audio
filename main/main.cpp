/*
* Copyright 2016 <Admobilize>
* All rights reserved.
*/
//#include <fftw3.h>
#include <unistd.h>
#include <gflags/gflags.h>
#include <stdint.h>
#include <string.h>
#include <wiringPi.h>
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

#include <libsocket/inetserverstream.hpp>
#include <libsocket/inetserverdgram.hpp>
#include <libsocket/inetclientdgram.hpp>
#include <libsocket/exception.hpp>

#include <pthread.h>
#include <mqueue.h>
#include <fcntl.h>

#include <utility>
#include <memory>

using namespace std;


//#include "../SharedResources.cpp"

DEFINE_int32(sampling_frequency, 16000, "Sampling Frequency");
DEFINE_int32(duration, 5, "Interrupt after N seconds");
DEFINE_int32(gain, 5, "Microphone Gain");
DEFINE_bool(big_menu, true, "Include 'advanced' options in the menu listing");

#define BUFFER_SAMPLES_PER_CHANNEL	16000 //1 second of recording
#define STREAMING_CHANNELS			8 //Maxmium 8 channels
const int32_t bufferByteSize = STREAMING_CHANNELS * BUFFER_SAMPLES_PER_CHANNEL * sizeof(int16_t);

#define HOST_NAME_LENGTH			20 //maximum number of characters for host name
#define COMMAND_LENGTH				1

//void *record2Remote(void* null);
void *record2Disk(void* null);

int32_t syncTime();

//void irInterrupt();
//void * motionDetection(void * null);
void *udpBroadcastReceiver(void *null);
void * recorder(void * null);

char sysInfo[COMMAND_LENGTH + HOST_NAME_LENGTH];

bool pcConnected = false;
bool recording = false;
//double buffer of SAMPLES_PER_CHANNEL*8 samples each
int16_t buffer[2][BUFFER_SAMPLES_PER_CHANNEL][STREAMING_CHANNELS];

pthread_mutex_t bufferMutex[2] = { PTHREAD_MUTEX_INITIALIZER };

char* status = &sysInfo[0];
char* hostname = &sysInfo[1];
char commandArgument;

std::unique_ptr<libsocket::inet_stream> tcpConnection;

namespace hal = matrix_hal;

//Everloop Image
hal::MatrixIOBus bus;
hal::EverloopImage image1d(bus.MatrixLeds());
//Everloop
hal::Everloop everloop;

hal::MicrophoneArray mics;

//For DOA
int led_offset[] = { 23, 27, 32, 1, 6, 10, 14, 19 };
int lut[] = { 1, 2, 10, 200, 10, 2, 1 };


/*void *DOA(void* null);
void *DirectRecord(void* null);

hal::MatrixIOBus bus;
hal::Everloop everloop;
hal::EverloopImage image1d(bus.MatrixLeds());
hal::MicrophoneArray mics;
hal::MicrophoneCore mic_core(mics);
hal::DirectionOfArrival doa(mics);
*/
int main(int argc, char *agrv[]) {
	google::ParseCommandLineFlags(&argc, &agrv, true);

	//27th Sept Addition trying for UDP TDP
	gethostname(hostname, HOST_NAME_LENGTH);
	*status = 'I';
	char command = '\0';
	pthread_t udpThread;
	pthread_create(&udpThread, NULL, udpBroadcastReceiver, NULL);

	//stand by
	libsocket::inet_stream_server tcpServer("0.0.0.0", "8000", LIBSOCKET_IPv4);
	std::cout << hostname << " - TCP server listening :8000\n";



	//initialization of bus
	
	if (!bus.Init()) return false;
	if (!bus.IsDirectBus()) {
		std::cerr << "Kernel Modules has been loaded. Use ALSA implementation "
			<< std::endl;
		return false;
	}
	//set sampling rate and sec to record
	int sampling_rate = FLAGS_sampling_frequency;
	int seconds_to_record = FLAGS_duration;

	// Microhone Array Configuration
	//hal::MicrophoneArray mics;
	mics.Setup(&bus);
	mics.SetSamplingRate(sampling_rate);
	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);

	mics.ShowConfiguration();
	std::cout << "Duration : " << seconds_to_record << "s" << std::endl;

	// Mic Core setup
	hal::MicrophoneCore mic_core(mics);
	mic_core.Setup(&bus);

	//Everloop
	//hal::Everloop everloop;
	everloop.Setup(&bus);

	//Everloop Image
	//hal::EverloopImage image1d(bus.MatrixLeds());

	
	//buffer setup
	int16_t buffer[mics.Channels() + 1]
		[mics.SamplingRate() + mics.NumberOfSamples()];

	//DOA setup
	

	
	mics.CalculateDelays(0, 0, 1000, 320 * 1000);
	/*
	ofstream os[mics.Channels() + 1];
	
	for (uint16_t c = 0; c < mics.Channels() + 1; c++) {
		string filename = "mic_" + std::to_string(mics.SamplingRate()) +
			"_s16le_channel_" + std::to_string(c) + ".raw";
		os[c].open(filename, std::ofstream::binary);
	}
	*/
	//setup complete change lights to blue
	for (hal::LedValue &led : image1d.leds) {
		led.red = 0;
		led.green = 0;
		led.blue = 10;
		led.white = 0;
	}
	everloop.Write(&image1d);

	
	while (true) {
		try {
			tcpConnection = tcpServer.accept2();

			//change light to green connected
			for (hal::LedValue &led : image1d.leds) {
				led.red = 0;
				led.green = 10;
				led.blue = 0;
				led.white = 0;
			}
			everloop.Write(&image1d);
			pcConnected = true;

			tcpConnection->snd(sysInfo, COMMAND_LENGTH + HOST_NAME_LENGTH);
			syncTime();

			pthread_t recorderThread;

			while (tcpConnection->rcv(&command, 1, MSG_WAITALL)) {
				switch (command) {
				
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
					case 'L': {
						recording = false;
						pthread_join(recorderThread, NULL);
						break;
					}
					}
					*status = 'I';
					break;
				}

				case 'T': {
					for (hal::LedValue &led : image1d.leds) {
						led.red = 0;
						led.green = 0;
						led.blue = 0;
						led.white = 0;
					}
					everloop.Write(&image1d);
					system("sudo shutdown now");
					break;
				}

				default: cout << "unrecognized command" << endl;
				}
				command = '\0';
				//turn off leds
				for (hal::LedValue &led : image1d.leds) {
					led.red = 0;
					led.green = 0;
					led.blue = 0;
					led.white = 0;
				}
				everloop.Write(&image1d);
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

		//turn off leds
		for (hal::LedValue &led : image1d.leds) {
			led.red = 0;
			led.green = 0;
			led.blue = 0;
			led.white = 0;
		}
		everloop.Write(&image1d);
	}
	return 0;
}

inline int32_t syncTime() {
	uint32_t currentEpochTime;
	tcpConnection->rcv(&currentEpochTime, 4);
	char command[25];
	sprintf(command, "sudo date -s @%d", currentEpochTime);
	cout << "Sys time synced with remote PC: " << flush;
	return system(command);
}

	/*testing my DOA
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
	recording = true;
	std::cout << "after r = false" << std::endl;
	pthread_t Record;
	pthread_create(&Record, NULL, DirectRecord, NULL);
	while (recording) {
		mics.Read(); 	*//* Reading 8-mics buffer from de FPGA */
	/*
		doa.Calculate();

		azimutal_angle = doa.GetAzimutalAngle() * 180 / M_PI;
		polar_angle = doa.GetPolarAngle() * 180 / M_PI;
		mic = doa.GetNearestMicrophone();*/
	/*
		std::cout << "azimutal angle = " << azimutal_angle
			<< ", polar angle = " << polar_angle << ", mic = " << mic
			<< std::endl;
		*/
	
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
			//error
			//turn off leds
			for (hal::LedValue &led : image1d.leds) {
				led.red = 0;
				led.green = 0;
				led.blue = 0;
				led.white = 0;
			}
			everloop.Write(&image1d);

			cout << exc.mesg << endl;
		}
	}
}

inline double syncRecording(char expectedSecondLSD) {
	struct syncPacket {
		uint32_t rxTimeInt;
		uint32_t rxTimeFrac;
	} packet;
	string ip;
	string port;
	libsocket::inet_dgram_client udp(LIBSOCKET_IPv4);
	udp.sndto("N", tcpConnection->gethost(), "1230");
	udp.rcvfrom((void*)&packet, 8, ip, port);

	int32_t secondDiff = expectedSecondLSD - 48 - packet.rxTimeInt % 10;
	if (secondDiff < 0) secondDiff += 10;

	return secondDiff * 1000000 - packet.rxTimeFrac;
}

void *recorder(void* null) {
	int32_t buffer_switch = 0;
	int32_t writeInitDiscard = 0;

	recording = true;
	//lock down buffer 0 before spawning streaming thread
	pthread_mutex_lock(&bufferMutex[buffer_switch]);

	pthread_t workerThread;
	switch (*status) {
	//case 'N':pthread_create(&workerThread, NULL, record2Remote, NULL); break;
	case 'L':pthread_create(&workerThread, NULL, record2Disk, NULL); break;
	}

	if (pcConnected) {
		int32_t samplesToWait;

		mics.Read();
		samplesToWait = syncRecording(commandArgument)*0.016 - 128;
		while (samplesToWait > 128) {
			mics.Read();
			samplesToWait -= 128;
		}
		//one more read to go
		writeInitDiscard = samplesToWait;
	}
	cout << "------ Recording starting ------" << endl;
	mics.SetGain(3);
	mics.Read();
	while (recording) {
		int32_t step = 0;
		bool bufferFull = false;

		//fill the first partial buffer
		for (int32_t s = writeInitDiscard; s < 128; s++) {
			for (int32_t c = 0; c < STREAMING_CHANNELS; c++) {
				buffer[buffer_switch][step][c] = mics.At(s, c);
			}
			step++;
		}

		while (!bufferFull) {
			int32_t s = 0;

			mics.Read(); //The reading process is a blocking process that read in 8*128 samples every 8ms

			for (s = 0; s < 128; s++) {
				for (int32_t c = 0; c < STREAMING_CHANNELS; c++) {
					buffer[buffer_switch][step][c] = mics.At(s, c);
				}
				step++;
				if (step == BUFFER_SAMPLES_PER_CHANNEL) {
					bufferFull = true;
					break;
				}
			}
		}
		pthread_mutex_lock(&bufferMutex[(buffer_switch + 1) % 2]);
		pthread_mutex_unlock(&bufferMutex[buffer_switch]);
		buffer_switch = (buffer_switch + 1) % 2;
	}

	pthread_mutex_unlock(&bufferMutex[buffer_switch]);
	pthread_join(workerThread, NULL);
	cout << "------ Recording ended ------" << endl;
	pthread_exit(NULL);
}
	

void *record2Disk(void* null) {
	uint32_t bufferSwitch = 0;
	char dateAndTime[16];
	struct tm tm;
	//hal::EverloopImage white(bus.MatrixLeds());
	uint32_t samples = 0;
	do {//fix the file size to less than 2GB maximum, create new file when recording continues
		/*
		for (hal::LedValue &led : white.leds) {
			led.red = 0;
			led.green = 0;
			led.blue = 0;
			led.white = 10;
		}
		everloop.Write(&white);
		*/
		mics.Read(); /* Reading 8-mics buffer from de FPGA */
		hal::DirectionOfArrival doa(mics);
		doa.Init();
		int mic;
		//DOA part start
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
		}
		//DOA part end

		uint32_t counter = 0;

		time_t t = time(NULL);
		tm = *localtime(&t);

		sprintf(dateAndTime, "%d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);

		ostringstream filenameStream;
		filenameStream << "/home/pi/BenRecordings/" << hostname << "_" << dateAndTime << "_8ch.wav";
		string filename = filenameStream.str();
		cout << filename << endl;

		ofstream file(filename, std::ofstream::binary);

		// WAVE file header format
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

		file.write((const char*)&header, sizeof(WaveHeader));

		while (recording && counter < 8191) {

			pthread_mutex_lock(&bufferMutex[bufferSwitch]);

			file.write((const char*)buffer[bufferSwitch], bufferByteSize);

			pthread_mutex_unlock(&bufferMutex[bufferSwitch]);
			bufferSwitch = (bufferSwitch + 1) % 2;

		}

		header.dataSize = bufferByteSize * counter;
		header.overallSize = header.dataSize + 36;
		file.seekp(0);
		file.write((const char*)&header, sizeof(WaveHeader));
		file.close();

	} while (recording);

	pthread_exit(NULL);
}

	//DOA lights
	//uint32_t samples = 0;
	//record + DOA
	/*for (int s = 0; s < seconds_to_record; s++) {
		for (;;) {
			mics.Read(); /* Reading 8-mics buffer from de FPGA 

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

			//Recorder starts
			for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {
				for (uint16_t c = 0; c < mics.Channels(); c++) { /* mics.Channels()=8 
					buffer[c][samples] = mics.At(s, c);
				}
				buffer[mics.Channels()][samples] = mics.Beam(s);
				samples++;
			}
			/* write to file 
			if (samples >= mics.SamplingRate()) {
				for (uint16_t c = 0; c < mics.Channels() + 1; c++) {
					os[c].write((const char *)buffer[c], samples * sizeof(int16_t));
				}
				samples = 0;
				break;
			}
		}
	}
	return 0;
}*/

			
	/*
	pthread_join(Record, NULL);
	std::cout << "after r = join" << std::endl;
	std::cout << "after DOA" << std::endl;
	*/
	// Microhone Array Configuration
	//recording = false;
	//pthread_t Record, testDOA;
	//pthread_create(&Record, NULL, DirectRecord, NULL);
	//pthread_create(&testDOA, NULL, DOA, NULL);
	//pthread_join(Record, NULL);
	//pthread_join(testDOA, NULL);

	


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


/*
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
	}*/

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
	&bus);

	uint32_t samples = 0;
	for (int s = 0; s < seconds_to_record; s++) {
		for (;;) {
			mics.Read(); *//* Reading 8-mics buffer from de FPGA */