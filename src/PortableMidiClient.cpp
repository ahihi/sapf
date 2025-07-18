#include "PortableMidiClient.hpp"

#ifdef SAPF_COREMIDI
PortableMidiClient::PortableMidiClient(const int numIn, const int numOut, const MIDIReadProc midiReadProc,
    const MIDINotifyProc midiNotifyProc) {
    
    int enc = kCFStringEncodingMacRoman;
    CFAllocatorRef alloc = CFAllocatorGetDefault();

    {
        CFStringRef clientName = CFStringCreateWithCString(alloc, "SAPF", enc);
        CFReleaser clientNameReleaser(clientName);

        __block OSStatus err2 = noErr;
        dispatch_sync(dispatch_get_main_queue(), ^{
            err2 = MIDIClientCreate(clientName, midiNotifyProc, nil, &mMIDIClient);
        });
        printf("mMIDIClient %d\n", (int)mMIDIClient);
        if (err2) {
            fprintf(stderr, "Could not create MIDI client. error %d\n", err2);
            return;
        }
    }

    for (int i=0; i<numIn; ++i) {
        char str[32];
        snprintf(str, 32, "in%d\n", i);
        CFStringRef inputPortName = CFStringCreateWithCString(alloc, str, enc);
        CFReleaser inputPortNameReleaser(inputPortName);

        const auto err = MIDIInputPortCreate(mMIDIClient, inputPortName, midiReadProc, &i, mMIDIInPort+i);
        if (err) {
            mNumMidiInPorts = i;
            fprintf(stderr, "Could not create MIDI port %s. error %d\n", str, err);
            return;
        }
    }

    mNumMidiInPorts = numIn;

    for (int i=0; i<numOut; ++i) {
        char str[32];
        snprintf(str, 32, "out%d\n", i);
        CFStringRef outputPortName = CFStringCreateWithCString(alloc, str, enc);
        CFReleaser outputPortNameReleaser(outputPortName);

        const auto err = MIDIOutputPortCreate(mMIDIClient, outputPortName, mMIDIOutPort+i);
        if (err) {
            mNumMidiOutPorts = i;
            fprintf(stderr, "Could not create MIDI out port. error %d\n", err);
        }
    }
    mNumMidiOutPorts = numOut;
}

PortableMidiClient::~PortableMidiClient() {
	/*
	* do not catch errors when disposing ports
	*/
	int i = 0;
	for (i=0; i<mNumMidiOutPorts; ++i) {
		if (mMIDIOutPort[i]) {
			MIDIPortDispose(mMIDIOutPort[i]);
		}
	}

	for (i=0; i<mNumMidiInPorts; ++i) {
		if (mMIDIInPort[i]) {
			MIDIPortDispose(mMIDIInPort[i]);
		}
	}

	if (mMIDIClient) {
		if( MIDIClientDispose(mMIDIClient) ) {
			fprintf(stderr, "Error: failed to dispose MIDIClient\n" );
		}
	}
}

int PortableMidiClient::numMidiInPorts() const {
	return mNumMidiInPorts;
}

int PortableMidiClient::numMidiOutPorts() const {
	return mNumMidiOutPorts;
}

void PortableMidiClient::printMIDIEndpoints() {
    OSStatus error;
	int numSrc = (int)MIDIGetNumberOfSources();
	int numDst = (int)MIDIGetNumberOfDestinations();

	printf("midi sources %d destinations %d\n", (int)numSrc, (int)numDst);

	for (int i=0; i<numSrc; ++i) {
		MIDIEndpointRef src = MIDIGetSource(i);
		SInt32 uid = 0;
		MIDIObjectGetIntegerProperty(src, kMIDIPropertyUniqueID, &uid);

		MIDIEntityRef ent;
		error = MIDIEndpointGetEntity(src, &ent);

		CFStringRef devname, endname;
		char cendname[1024], cdevname[1024];

		// Virtual sources don't have entities
		if(error)
		{
			MIDIObjectGetStringProperty(src, kMIDIPropertyName, &devname);
			MIDIObjectGetStringProperty(src, kMIDIPropertyName, &endname);
			CFStringGetCString(devname, cdevname, 1024, kCFStringEncodingUTF8);
			CFStringGetCString(endname, cendname, 1024, kCFStringEncodingUTF8);
		}
		else
		{
			MIDIDeviceRef dev;

			MIDIEntityGetDevice(ent, &dev);
			MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &devname);
			MIDIObjectGetStringProperty(src, kMIDIPropertyName, &endname);
			CFStringGetCString(devname, cdevname, 1024, kCFStringEncodingUTF8);
			CFStringGetCString(endname, cendname, 1024, kCFStringEncodingUTF8);
		}
		
		printf("MIDI Source %2d '%s', '%s' UID: %d\n", i, cdevname, cendname, uid);
	}



	for (int i=0; i<numDst; ++i) {
		MIDIEndpointRef dst = MIDIGetDestination(i);
		SInt32 uid = 0;
		MIDIObjectGetIntegerProperty(dst, kMIDIPropertyUniqueID, &uid);

		MIDIEntityRef ent;
		error = MIDIEndpointGetEntity(dst, &ent);

		CFStringRef devname, endname;
		char cendname[1024], cdevname[1024];

		// Virtual destinations don't have entities either
		if(error)
		{
			MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &devname);
			MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &endname);
			CFStringGetCString(devname, cdevname, 1024, kCFStringEncodingUTF8);
			CFStringGetCString(endname, cendname, 1024, kCFStringEncodingUTF8);

		}
		else
		{
			MIDIDeviceRef dev;

			MIDIEntityGetDevice(ent, &dev);
			MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &devname);
			MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &endname);
			CFStringGetCString(devname, cdevname, 1024, kCFStringEncodingUTF8);
			CFStringGetCString(endname, cendname, 1024, kCFStringEncodingUTF8);
		}
		printf("MIDI Destination %2d '%s', '%s' UID: %d\n", i, cdevname, cendname, uid);
	}
}

void PortableMidiClient::connectInputPort(const int uid, const int inputIndex) const {
	if (inputIndex < 0 || inputIndex >= mNumMidiInPorts) return;

	MIDIEndpointRef src=0;
	MIDIObjectType mtype;
	MIDIObjectFindByUniqueID(uid, (MIDIObjectRef*)&src, &mtype);
	if (mtype != kMIDIObjectType_Source) return;

	//pass the uid to the midiReadProc to identify the src
	void* p = (void*)(uintptr_t)inputIndex;
	MIDIPortConnectSource(mMIDIInPort[inputIndex], src, p);
}

void PortableMidiClient::disconnectInputPort(const int uid, const int inputIndex) const {
	if (inputIndex < 0 || inputIndex >= mNumMidiInPorts) return;

	MIDIEndpointRef src=0;
	MIDIObjectType mtype;
	MIDIObjectFindByUniqueID(uid, (MIDIObjectRef*)&src, &mtype);
	if (mtype != kMIDIObjectType_Source) return;

	MIDIPortDisconnectSource(mMIDIInPort[inputIndex], src);
}
#else
PortableMidiClient::PortableMidiClient(const int numIn, const int numOut, const RtMidiIn::RtMidiCallback midiCallback) {
    try {
        // Create and configure MIDI input ports
        for (int i = 0; i < numIn; ++i) {
            try {
            	auto midiIn = std::make_unique<RtMidiIn>();
                midiIn->setCallback(midiCallback, reinterpret_cast<void*>(i));
                midiIn->ignoreTypes(false, false, false); // Don't ignore sysex, timing, or active sensing
            	mMIDIInPorts.push_back(std::move(midiIn));
            } catch (RtMidiError &error) {
                fprintf(stderr, "Error creating MIDI input port %d: %s\n", i, error.getMessage().c_str());
            }
        }

        // Create MIDI output ports
        for (int i = 0; i < numOut; ++i) {
            try {
                mMIDIOutPorts.push_back(std::make_unique<RtMidiOut>());
            } catch (RtMidiError &error) {
                fprintf(stderr, "Error creating MIDI output port %d: %s\n", i, error.getMessage().c_str());
            }
        }
    } catch (RtMidiError &error) {
        fprintf(stderr, "Error initializing MIDI: %s\n", error.getMessage().c_str());
    } 
}

PortableMidiClient::~PortableMidiClient() {
    for (const auto& midiInPort : mMIDIInPorts) {
        if (midiInPort) {
            midiInPort->closePort();
        }
    }

    for (const auto& midiOutPort : mMIDIOutPorts) {
        if (midiOutPort) {
            midiOutPort->closePort();
        }
    }
}

int PortableMidiClient::numMidiInPorts() const {
	return mMIDIInPorts.size();
}

int PortableMidiClient::numMidiOutPorts() const {
	return mMIDIOutPorts.size();
}

void PortableMidiClient::connectInputPort(const int uid, const int inputIndex) const {
	if (inputIndex < 0 || inputIndex >= mMIDIInPorts.size()) return;

	try {
		if (!mMIDIInPorts[inputIndex]) return;

		// Close any existing connection
		if (mMIDIInPorts[inputIndex]->isPortOpen()) {
			mMIDIInPorts[inputIndex]->closePort();
		}

		// Connect to the specified port by UID (which is port number in RtMidi)
		if (uid >= 0 && uid < mMIDIInPorts[inputIndex]->getPortCount()) {
			mMIDIInPorts[inputIndex]->openPort(uid);
		}
	} catch (RtMidiError &error) {
		fprintf(stderr, "Error connecting MIDI input: %s\n", error.getMessage().c_str());
	}
}

void PortableMidiClient::disconnectInputPort(const int uid, const int inputIndex) const {
	if (inputIndex < 0 || inputIndex >= mMIDIInPorts.size()) return;

	try {
		if (!mMIDIInPorts[inputIndex]) return;

		// Close the port
		if (mMIDIInPorts[inputIndex]->isPortOpen()) {
			mMIDIInPorts[inputIndex]->closePort();
		}
	} catch (RtMidiError &error) {
		fprintf(stderr, "Error disconnecting MIDI input: %s\n", error.getMessage().c_str());
	}
}

void PortableMidiClient::printMIDIEndpoints() {
	try {
		RtMidiIn* midiin = new RtMidiIn();
		RtMidiOut* midiout = new RtMidiOut();
        
		unsigned int numSrc = midiin->getPortCount();
		unsigned int numDst = midiout->getPortCount();
        
		printf("midi sources %d destinations %d\n", (int)numSrc, (int)numDst);
        
		for (unsigned int i = 0; i < numSrc; i++) {
			try {
				std::string portName = midiin->getPortName(i);
				printf("MIDI Source %2d '%s' UID: %d\n", i, portName.c_str(), i);
			} catch (RtMidiError &error) {
				fprintf(stderr, "Error: %s\n", error.getMessage().c_str());
			}
		}
        
		for (unsigned int i = 0; i < numDst; i++) {
			try {
				std::string portName = midiout->getPortName(i);
				printf("MIDI Destination %2d '%s' UID: %d\n", i, portName.c_str(), i);
			} catch (RtMidiError &error) {
				fprintf(stderr, "Error: %s\n", error.getMessage().c_str());
			}
		}
        
		delete midiin;
		delete midiout;
	} catch (RtMidiError &error) {
		fprintf(stderr, "Error: %s\n", error.getMessage().c_str());
	}
}

#endif
