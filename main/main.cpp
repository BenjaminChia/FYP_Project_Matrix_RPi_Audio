/** Copyright 2016 <Admobilize>* All rights reserved.*/#include <fftw3.h>#include <gflags/gflags.h>#include <stdint.h>#include <string.h>#include <wiringPi.h>#include <pthread.h>#include <unistd.h>#include <sstream>#include <fstream>#include <iostream>#include <string>#include <valarray>#include <thread>#include "sys/socket.h"#include "bits/socket.h"#include <stdio.h>
#include <malloc.h>
#include <math.h>#include <libsocket/inetserverstream.hpp>#include <libsocket/inetserverdgram.hpp>#include <libsocket/inetclientdgram.hpp>#include <libsocket/exception.hpp>#include "../matrix/cpp/driver/direction_of_arrival.h"#include "../matrix/cpp/driver/everloop.h"#include "../matrix/cpp/driver/everloop_image.h"#include "../matrix/cpp/driver/matrixio_bus.h"#include "../matrix/cpp/driver/microphone_array.h"#include "../matrix/cpp/driver/microphone_core.h"//#include "sharedResources.h"//#include "../SharedResources.cpp"#define BUFFER_SAMPLES_PER_CHANNEL	16000 //1 second of recording#define STREAMING_CHANNELS			1 //Maxmium 8 channelsconst int32_t bufferByteSize = STREAMING_CHANNELS * BUFFER_SAMPLES_PER_CHANNEL * sizeof(int16_t);#define HOST_NAME_LENGTH			20 //maximum number of characters for host name#define COMMAND_LENGTH				1void *record2Disk(void* null);void * recorder(void * null);void *udpBroadcastReceiver(void *null);bool pcConnected = false;bool recording;char sysInfo[COMMAND_LENGTH + HOST_NAME_LENGTH];char commandArgument;char* status = &sysInfo[0];char* hostname = &sysInfo[1];int16_t buffer[2][BUFFER_SAMPLES_PER_CHANNEL][STREAMING_CHANNELS];pthread_mutex_t bufferMutex[2] = { PTHREAD_MUTEX_INITIALIZER };namespace hal = matrix_hal;DEFINE_int32(sampling_frequency, 16000, "Sampling Frequency");DEFINE_int32(duration, 10, "Interrupt after N seconds");DEFINE_int32(gain, 3, "Microphone Gain");DEFINE_bool(big_menu, true, "Include 'advanced' options in the menu listing");using namespace std;int led_offset[] = { 23, 27, 32, 1, 6, 10, 14, 19 };int lut[] = { 1, 2, 10, 200, 10, 2, 1 };std::unique_ptr<libsocket::inet_stream> tcpConnection;hal::MicrophoneArray mics;hal::MatrixIOBus bus;////int isBigEndian() {
//	int test = 1;
//	char *p = (char*)&test;
//
//	return p[0] == 0;
//}
//void reverseEndianness(const long long int size, void* value) {
//	int i;
//	char result[32];
//	for (i = 0; i<size; i += 1) {
//		result[i] = ((char*)value)[size - i - 1];
//	}
//	for (i = 0; i<size; i += 1) {
//		((char*)value)[i] = result[i];
//	}
//}
//void toBigEndian(const long long int size, void* value) {
//	char needsFix = !((1 && isBigEndian()) || (0 && !isBigEndian()));
//	if (needsFix) {
//		reverseEndianness(size, value);
//	}
//}//void toLittleEndian(const long long int size, void* value) {
//	char needsFix = !((0 && isBigEndian()) || (1 && !isBigEndian()));
//	if (needsFix) {
//		reverseEndianness(size, value);
//	}
//}//typedef struct WaveHeader {
//	// Riff Wave Header
//	char chunkId[4];
//	int  chunkSize;
//	char format[4];
//
//	// Format Subchunk
//	char subChunk1Id[4];
//	int  subChunk1Size;
//	short int audioFormat;
//	short int numChannels;
//	int sampleRate;
//	int byteRate;
//	short int blockAlign;
//	short int bitsPerSample;
//	//short int extraParamSize;
//
//	// Data Subchunk
//	char subChunk2Id[4];
//	int  subChunk2Size;
//
//} WaveHeader;//WaveHeader makeWaveHeader(int const sampleRate, short int const numChannels, short int const bitsPerSample) {
//	WaveHeader myHeader;
//
//	// RIFF WAVE Header
//	myHeader.chunkId[0] = 'R';
//	myHeader.chunkId[1] = 'I';
//	myHeader.chunkId[2] = 'F';
//	myHeader.chunkId[3] = 'F';
//	myHeader.format[0] = 'W';
//	myHeader.format[1] = 'A';
//	myHeader.format[2] = 'V';
//	myHeader.format[3] = 'E';
//
//	// Format subchunk
//	myHeader.subChunk1Id[0] = 'f';
//	myHeader.subChunk1Id[1] = 'm';
//	myHeader.subChunk1Id[2] = 't';
//	myHeader.subChunk1Id[3] = ' ';
//	myHeader.audioFormat = 1; // FOR PCM
//	myHeader.numChannels = 1; // 1 for MONO, 2 for stereo
//	myHeader.sampleRate = 16000; // ie 44100 hertz, cd quality audio
//	myHeader.bitsPerSample = 16; // 
//	myHeader.byteRate = 32000; //myHeader.sampleRate * myHeader.numChannels * myHeader.bitsPerSample / 8;
//	myHeader.blockAlign = 2; //myHeader.numChannels * myHeader.bitsPerSample / 8;
//
//	// Data subchunk
//	myHeader.subChunk2Id[0] = 'd';
//	myHeader.subChunk2Id[1] = 'a';
//	myHeader.subChunk2Id[2] = 't';
//	myHeader.subChunk2Id[3] = 'a';
//
//	// All sizes for later:
//	// chuckSize = 4 + (8 + subChunk1Size) + (8 + subChubk2Size)
//	// subChunk1Size is constanst, i'm using 16 and staying with PCM
//	// subChunk2Size = nSamples * nChannels * bitsPerSample/8
//	// Whenever a sample is added:
//	//    chunkSize += (2)
//	//    subChunk2Size += (2)
//	myHeader.subChunk1Size = 16;
//	myHeader.subChunk2Size = 0;
//	myHeader.chunkSize = 4 + (8 + 16) + (8 + myHeader.subChunk2Size);
//	
//
//	return myHeader;
//}

// -------------------------------------------------------- [ Section: Wave ] -
//typedef struct Wave {
//	WaveHeader header;
//	char* data;
//	long long int index;
//	long long int size;
//	long long int nSamples;
//} Wave;//
//void waveDestroy(Wave* wave) {
//	free(wave->data);
//}////void waveAddSample(Wave* wave, const float* samples) {
//	int i;
//	short int sample8bit;
//	int sample16bit;
//	long int sample32bit;
//	char* sample;
//	if (wave->header.bitsPerSample == 8) {
//		for (i = 0; i<wave->header.numChannels; i += 1) {
//			sample8bit = (short int)(127 + 127.0*samples[i]);
//			toLittleEndian(1, (void*)&sample8bit);
//			sample = (char*)&sample8bit;
//			(wave->data)[wave->index] = sample[0];
//			wave->index += 1;
//		}
//	}
//	if (wave->header.bitsPerSample == 16) {
//		for (i = 0; i<wave->header.numChannels; i += 1) {
//			sample16bit = (int)(32767 * samples[i]);
//			//sample = (char*)&litEndianInt( sample16bit );
//			toLittleEndian(2, (void*)&sample16bit);
//			sample = (char*)&sample16bit;
//			wave->data[wave->index + 0] = sample[0];
//			wave->data[wave->index + 1] = sample[1];
//			wave->index += 2;
//		}
//	}
//	if (wave->header.bitsPerSample == 32) {
//		for (i = 0; i<wave->header.numChannels; i += 1) {
//			sample32bit = (long int)((pow(2, 32 - 1) - 1)*samples[i]);
//			//sample = (char*)&litEndianLong( sample32bit );
//			toLittleEndian(4, (void*)&sample32bit);
//			sample = (char*)&sample32bit;
//			wave->data[wave->index + 0] = sample[0];
//			wave->data[wave->index + 1] = sample[1];
//			wave->data[wave->index + 2] = sample[2];
//			wave->data[wave->index + 3] = sample[3];
//			wave->index += 4;
//		}
//	}
//}////void waveToFile(Wave* wave, const char* filename) {
//	// First make sure all numbers are little endian
//	toLittleEndian(sizeof(int), (void*)&(wave->header.chunkSize));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.subChunk1Size));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.audioFormat));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.numChannels));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.sampleRate));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.byteRate));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.blockAlign));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.bitsPerSample));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.subChunk2Size));
//
//	// Open the file, write header, write data
//	FILE *file;
//	file = fopen(filename, "wb");
//	fwrite(&(wave->header), sizeof(WaveHeader), 1, file);
//	fwrite((void*)(wave->data), sizeof(char), wave->size, file);
//	fclose(file);
//
//	// Convert back to system endian-ness
//	toLittleEndian(sizeof(int), (void*)&(wave->header.chunkSize));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.subChunk1Size));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.audioFormat));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.numChannels));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.sampleRate));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.byteRate));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.blockAlign));
//	toLittleEndian(sizeof(short int), (void*)&(wave->header.bitsPerSample));
//	toLittleEndian(sizeof(int), (void*)&(wave->header.subChunk2Size));
//}int main(int argc, char *agrv[]) {	gethostname(hostname, HOST_NAME_LENGTH);	*status = 'I';	google::ParseCommandLineFlags(&argc, &agrv, true);	if (!bus.Init()) return false;

	if (!bus.IsDirectBus()) {
		std::cerr << "Kernel Modules has been loaded. Use ALSA examples "
			<< std::endl;
	}
	//hal::MatrixIOBus bus;
	char command = '\0';	pthread_t udpThread;	pthread_create(&udpThread, NULL, udpBroadcastReceiver, NULL);	//stand by	libsocket::inet_stream_server tcpServer("0.0.0.0", "8000", LIBSOCKET_IPv4);	cout << hostname << " - TCP server listening :8000\n";	//wait for network connection	while (true) {		try {			tcpConnection = tcpServer.accept2();			//connected					pcConnected = true;			tcpConnection->snd(sysInfo, COMMAND_LENGTH + HOST_NAME_LENGTH);			//syncTime();			pthread_t recorderThread;			while (tcpConnection->rcv(&command, 1, MSG_WAITALL)) {				cout << command << endl;				switch (command) {			 				case 'L': {//record to disk					if (*status == 'I') {						*status = 'L';						tcpConnection->rcv(&commandArgument, 1, MSG_WAITALL);						pthread_create(&recorderThread, NULL, recorder, NULL);					}					break;				}				case 'I': { //stop everything					switch (*status) {					case 'I': break;					case 'L': {						recording = false;						pthread_join(recorderThread, NULL);						break;					}					}					*status = 'I';					break;				}				case '\0': {					cout << "\0" << endl;					break;				}				default: cout << "unrecognized command" << endl;				}				command = '\0';			}		}		catch (const libsocket::socket_exception& exc)		{			//error			cout << exc.mesg << endl;		}		pcConnected = false;		cout << "Remote PC at " << tcpConnection->gethost() << ":" << tcpConnection->getport() << " disconnected" << endl;		tcpConnection->destroy();	}	sleep(1);	return 0;}void *udpBroadcastReceiver(void *null) {	string remoteIP;	string remotePort;	string buffer;	remoteIP.resize(16);	remotePort.resize(16);	buffer.resize(32);	//start server	libsocket::inet_dgram_server udpServer("0.0.0.0", "8001", LIBSOCKET_IPv4);	cout << hostname << " - UDP server listening :8001\n";	while (true) {		try {			udpServer.rcvfrom(buffer, remoteIP, remotePort);			if (!pcConnected && buffer.compare("live long and prosper") == 0) {				cout << "Remote PC at " << remoteIP << endl;				udpServer.sndto("peace and long life", remoteIP, remotePort);			}		}		catch (const libsocket::socket_exception& exc)		{			cout << exc.mesg << endl;		}	}}void *recorder(void* null) {	hal::EverloopImage image1d(bus.MatrixLeds());	int32_t buffer_switch = 0;	int32_t writeInitDiscard = 0;	recording = true;	//lock down buffer 0 before spawning streaming thread	//pthread_mutex_lock(&bufferMutex[buffer_switch]);	pthread_t workerThread;	switch (*status) {	case 'L':pthread_create(&workerThread, NULL, record2Disk, NULL); break;	}	/*	if (pcConnected) {		int32_t samplesToWait;		microphoneArray.Read();		samplesToWait = syncRecording(commandArgument)*0.016 - 128;		while (samplesToWait > 128) {			microphoneArray.Read();			samplesToWait -= 128;		}		//one more read to go		writeInitDiscard = samplesToWait;	}*/	cout << "------ Recording starting ------" << endl;	mics.SetGain(3);	mics.Read(); 		//while (recording) {	//	int32_t step = 0;	//	bool bufferFull = false;	//	//fill the first partial buffer	//	for (int32_t s = writeInitDiscard; s < 128; s++) {	//		for (int32_t c = 0; c < STREAMING_CHANNELS; c++) {	//			buffer[buffer_switch][step][c] = mics.At(s, c);	//		}	//		step++;	//	}	//	while (!bufferFull) {	//		int32_t s = 0;	//		mics.Read(); //The reading process is a blocking process that read in 8*128 samples every 8ms	//		for (s = 0; s < 128; s++) {	//			for (int32_t c = 0; c < STREAMING_CHANNELS; c++) {	//				buffer[buffer_switch][step][c] = mics.At(s, c);	//			}	//			step++;	//			if (step == BUFFER_SAMPLES_PER_CHANNEL) {	//				bufferFull = true;	//				break;	//			}	//		}	//	}	//	pthread_mutex_lock(&bufferMutex[(buffer_switch + 1) % 2]);	//	pthread_mutex_unlock(&bufferMutex[buffer_switch]);	//	buffer_switch = (buffer_switch + 1) % 2;	//}		//pthread_mutex_unlock(&bufferMutex[buffer_switch]);	pthread_join(workerThread, NULL);	cout << "------ Recorder ended ------" << endl;		pthread_exit(NULL);}void *record2Disk(void* null) {		cout << "record 2 Disk " << endl;
	cout << recording << endl;
	int sampling_rate = FLAGS_sampling_frequency;	//hal::MicrophoneArray mics;
	mics.Setup(&bus);
	cout << "bus setup" << endl;
	mics.SetSamplingRate(sampling_rate);
	mics.ShowConfiguration();	//int sampling_rate = FLAGS_sampling_frequency;	cout << "bus " << endl;	//initialization of bus	//if (!bus.Init()) return false;	//if (!bus.IsDirectBus()) return false;	//set sampling rate and sec to record		//int seconds_to_record = FLAGS_duration;	// Microhone Array Configuration		cout << "mic " << endl;		//mics.SetSamplingRate(sampling_rate);	cout << "setsamplingrate" << endl;	if (FLAGS_gain > 0) mics.SetGain(FLAGS_gain);	mics.ShowConfiguration();	//std::cout << "Duration : " << seconds_to_record << "s" << std::endl;	// Mic Core setup	hal::MicrophoneCore mic_core(mics);	mic_core.Setup(&bus);	//Everloop	hal::Everloop everloop;	everloop.Setup(&bus);	//Everloop Image	hal::EverloopImage image1d(bus.MatrixLeds());	//buffer setup	int16_t buffer1[mics.Channels() + 1]		[mics.SamplingRate() + mics.NumberOfSamples()];	//DOA setup	hal::DirectionOfArrival doa(mics);	doa.Init();	char command = '\0';	int mic;	mics.CalculateDelays(0, 0, 1000, 320 * 1000);	ofstream os[mics.Channels() + 1];	uint32_t bufferSwitch = 0;	char dateAndTime[16];	struct tm tm;	for (uint16_t c = 8; c < mics.Channels() + 1; c++) {		string filename = "mic_" + std::to_string(mics.SamplingRate()) +			"_s16le_channel_" + std::to_string(c) + ".raw";		os[c].open(filename, ofstream::binary);	}	time_t t = time(NULL);
	tm = *localtime(&t);

	sprintf(dateAndTime, "%d%02d%02d_%02d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec);

	ostringstream filenameStream;
	filenameStream << "/home/pi/BenRecordings/" << hostname << "_" << dateAndTime << "_8ch.wav";
	string filename = filenameStream.str();
	cout << filename << endl;
	uint32_t counter = 0;
	ofstream file(filename, std::ofstream::binary);	//DOA lights	uint32_t samples = 0;	//record + DOA	cout << "------ Recording starting ------" << endl;		struct WaveHeader {
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
	} header;	cout << &header << "<header | size of > " << sizeof(WaveHeader) << endl;	file.write((const char*)&header, sizeof(WaveHeader));	do{		mics.Read(); /* Reading 8-mics buffer from de FPGA */		

		// WAVE file header format
		//frameData[0] = buffer[bufferSwitch];
		//waveAddSample(&mySound, frameData);
		//file.write((const char*)&header, sizeof(WaveHeader));

		/*pthread_mutex_lock(&bufferMutex[bufferSwitch]);
		file.write((const char*)buffer[bufferSwitch], bufferByteSize);
		pthread_mutex_unlock(&bufferMutex[bufferSwitch]);

		bufferSwitch = (bufferSwitch + 1) % 2;		counter++;*/
		/*while (recording && counter < 8191) {

			pthread_mutex_lock(&bufferMutex[bufferSwitch]);
			file.write((const char*)buffer[bufferSwitch], bufferByteSize);
			pthread_mutex_unlock(&bufferMutex[bufferSwitch]);

			bufferSwitch = (bufferSwitch + 1) % 2;

			counter++;
		}*/
				//DOA part		doa.Calculate();		mic = doa.GetNearestMicrophone();		for (hal::LedValue &led : image1d.leds) {			led.blue = 0;		}		for (int i = led_offset[mic] - 3, j = 0; i < led_offset[mic] + 3; ++i, ++j) {			if (i < 0) {				image1d.leds[image1d.leds.size() + i].blue = lut[j];			}			else {				image1d.leds[i % image1d.leds.size()].blue = lut[j];			}			everloop.Write(&image1d);		}//DOA part end		//Recorder starts		for (uint32_t s = 0; s < mics.NumberOfSamples(); s++) {			for (uint16_t c = 8; c < mics.Channels(); c++) { /* mics.Channels()=8 */				buffer1[c][samples] = mics.At(s, c);			}			buffer1[mics.Channels()][samples] = mics.Beam(s);			samples++;			counter++;		}		if (samples >= mics.SamplingRate()) {			for (uint16_t c = 8; c < mics.Channels() + 1; c++) {				os[c].write((const char *)buffer1[c], samples * sizeof(int16_t));				file.write((const char*)buffer1[c], samples * sizeof(int16_t));			}			samples = 0;		}		std::cout << recording << endl;	}while (recording);	cout << header.dataSize << "dataSize" << endl;
	cout << header.overallSize << "dataSize" << endl;	header.dataSize = counter * 2 ;
	header.overallSize = header.dataSize + 36;
	cout << header.dataSize << "dataSize"<<endl;
	cout << header.overallSize << "dataSize" << endl;
	file.seekp(0);
	cout << &header << "<header | size of > "<< sizeof(WaveHeader) << endl ;
	file.write((const char*)&header, sizeof(WaveHeader));
	file.close();	for (hal::LedValue &led : image1d.leds) {		led.blue = 0;	}	everloop.Write(&image1d);	cout << "------ Recording ended ------" << endl;	cout << recording << endl;	pthread_exit(NULL);		} 					