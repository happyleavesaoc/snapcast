#include "controller.h"
#include <iostream>
#include <string>
#include <memory>
#include <unistd.h>
#include "oggDecoder.h"
#include "pcmDecoder.h"
#include "player.h"
#include "timeProvider.h"
#include "common/serverSettings.h"
#include "common/timeMsg.h"
#include "common/requestMsg.h"

using namespace std;


Controller::Controller() : MessageReceiver(), active_(false), streamClient(NULL), sampleFormat(NULL)
{
	decoder = new OggDecoder();
}


void Controller::onMessageReceived(SocketConnection* connection, const BaseMessage& baseMessage, char* buffer)
{
	if (baseMessage.type == message_type::payload)
	{
		if ((stream != NULL) && (decoder != NULL))
		{
			PcmChunk* pcmChunk = new PcmChunk(*sampleFormat, 0);
			pcmChunk->deserialize(baseMessage, buffer);
//cout << "chunk: " << pcmChunk->payloadSize;
			if (decoder->decode(pcmChunk))
			{
				stream->addChunk(pcmChunk);
//cout << ", decoded: " << pcmChunk->payloadSize << ", Duration: " << pcmChunk->getDuration() << ", sec: " << pcmChunk->tv_sec << ", usec: " << pcmChunk->tv_usec/1000 << ", type: " << pcmChunk->type << "\n";
			}
			else
				delete pcmChunk;
		}
	}
/*	else if (baseMessage.type == message_type::header)
	{
		if (decoder != NULL)
		{
			HeaderMessage* headerMessage = new HeaderMessage();
			headerMessage->deserialize(baseMessage, buffer);
			decoder->setHeader(headerMessage);
		}
	}
	else if (baseMessage.type == message_type::sampleformat)
	{
		sampleFormat = new SampleFormat();
		sampleFormat->deserialize(baseMessage, buffer);
		cout << "SampleFormat rate: " << sampleFormat->rate << ", bits: " << sampleFormat->bits << ", channels: " << sampleFormat->channels << "\n";
	}
	else if (baseMessage.type == message_type::serversettings)
	{
		ServerSettings* serverSettings = new ServerSettings();
		serverSettings->deserialize(baseMessage, buffer);
		cout << "ServerSettings port: " << serverSettings->port << "\n";
		streamClient = new StreamClient(this, ip, serverSettings->port);
	}
*/
}


void Controller::start(const std::string& _ip, size_t _port, int _bufferMs)
{
	bufferMs = _bufferMs;
	ip = _ip;

	controlConnection = new ClientConnection(this, ip, _port);
	controlConnection->start();

	controllerThread = new thread(&Controller::worker, this);
}


void Controller::stop()
{
	active_ = false;
}


void Controller::worker()
{
//	Decoder* decoder;
	active_ = true;	

	RequestMsg requestMsg("serverSettings");
	shared_ptr<ServerSettings> serverSettings(NULL);
	while (!(serverSettings = controlConnection->sendReq<ServerSettings>(&requestMsg, 2000)));
	cout << "ServerSettings port: " << serverSettings->port << "\n";
	streamClient = new StreamClient(this, ip, serverSettings->port);

	requestMsg.request = "sampleFormat";
	while (!(sampleFormat = controlConnection->sendReq<SampleFormat>(&requestMsg, 2000)));
	cout << "SampleFormat rate: " << sampleFormat->rate << ", bits: " << sampleFormat->bits << ", channels: " << sampleFormat->channels << "\n";

	if (decoder != NULL)
	{
		requestMsg.request = "headerChunk";
		shared_ptr<HeaderMessage> headerChunk(NULL);
		while (!(headerChunk = controlConnection->sendReq<HeaderMessage>(&requestMsg, 2000)));
		decoder->setHeader(headerChunk.get());
	}

	RequestMsg timeReq("time");
	for (size_t n=0; n<10; ++n)
	{
		shared_ptr<TimeMsg> reply = controlConnection->sendReq<TimeMsg>(&timeReq, 2000);
		if (reply)
		{
			double latency = (reply->received.sec - reply->sent.sec) + (reply->received.usec - reply->sent.usec) / 1000000.;
			TimeProvider::getInstance().setDiffToServer((reply->latency - latency) * 1000 / 2);
			usleep(1000);
		}
	}

	streamClient->start();
	stream = new Stream(*sampleFormat);
	stream->setBufferLen(bufferMs);

	Player player(stream);
	player.start();

	while (active_)
	{
		usleep(1000000);
		try
		{		
			shared_ptr<TimeMsg> reply = controlConnection->sendReq<TimeMsg>(&timeReq, 2000);
			if (reply)
			{
				double latency = (reply->received.sec - reply->sent.sec) + (reply->received.usec - reply->sent.usec) / 1000000.;
//					cout << "C2S: " << timeMsg.latency << ", S2C: " << latency << ", diff: " << (timeMsg.latency - latency) / 2 << endl;
				TimeProvider::getInstance().setDiffToServer((reply->latency - latency) * 1000 / 2);
				cout << TimeProvider::getInstance().getDiffToServer() << "\n";
			}
		}
		catch (const std::exception& e)
		{
		}
	}
}



