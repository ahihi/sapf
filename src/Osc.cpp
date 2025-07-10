#include "Osc.hpp"

#ifdef SAPF_OSC
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <tuple>
#include <osc/OscReceivedElements.h>
#include <osc/OscPacketListener.h>
#include <ip/UdpSocket.h>

#include "VM.hpp"

class OscListener : public osc::OscPacketListener {
public:
	void putMapping(std::string address, P<Ref> ref) {
		auto search = mappings.find(address);
		if(search != mappings.end()) {
			std::cout << "release existing mapping" << std::endl;
			search->second->release();
		}

		std::cout << "put mapping" << std::endl;
		ref->retain();
		mappings[address] = ref;
	}

	void removeMapping(std::string address) {
		auto search = mappings.find(address);
		if(search == mappings.end()) {
			std::cout << "no such mapping" << std::endl;
			return;
		}

		std::cout << "remove mapping" << std::endl;
		search->second->release();
		mappings.erase(search);
	}

	size_t numMappings() {
		return mappings.size();
	}
	
protected:
	virtual void ProcessMessage(
		const osc::ReceivedMessage& m,
		const IpEndpointName& remoteEndpoint
	) {
		(void) remoteEndpoint; // suppress unused parameter warning

		try {
			auto search = mappings.find(m.AddressPattern());
			if(search == mappings.end()) {
				std::cout << "no mapping for " << m.AddressPattern() << std::endl;
				return;
			}

			// std::cout << "found " << m.AddressPattern() << std::endl;
			auto& ref = search->second;

			// if(!ref->o.isVList()) {
			// 	P<List> list = reinterpret_cast<P<List> >(ref->o);
			// 	int64_t len = min(list->length, m.ArgumenCount());

			//      // TODO...
			// } else {
				ref->set(m.ArgumentsBegin()->AsFloat());
			// }
		} catch(osc::Exception& e) {
			// any parsing errors such as unexpected argument types, or
			// missing arguments get thrown as exceptions.
			std::cout << "error while parsing message: "
				  << m.AddressPattern() << ": " << e.what() << "\n";
		}
	}

	std::unordered_map<std::string,P<Ref>> mappings;
};

class OscHandler {
public:
	OscHandler(unsigned long addr, int port) :
		endpointName(addr, port)
	{}

	~OscHandler() {
		stopListening();
	}

	OscHandler(OscHandler&& other) :
		endpointName(std::move(other.endpointName)),
		listener(std::move(other.listener)),
		sock(std::move(other.sock)),
		thread(std::move(other.thread))
	{}
	
	void startListening() {
		if(isListening()) {
			std::cout << "already listening" << std::endl;
			return;
		}

		std::cout << "start listening" << std::endl;
		sock = std::make_unique<UdpListeningReceiveSocket>(endpointName, &listener);
		thread = std::thread([&] {
			sock->Run();
		});
	}

	void stopListening() {
		if(!isListening()) {
			std::cout << "not listening" << std::endl;
			return;
		}

		std::cout << "stop listening" << std::endl;
		sock->AsynchronousBreak();
		thread.join();
		sock = nullptr;
	}

	bool isListening() {
		return thread.joinable();
	}
	
	IpEndpointName endpointName;
	OscListener listener;
	std::unique_ptr<UdpListeningReceiveSocket> sock;
	std::thread thread;
};

// using a map instead of unordered_map because hashing tuples is a bit of a pain
std::map<std::tuple<unsigned long,int>,OscHandler> oscHandlers;

static void oscmap_(Thread& th, Prim* prim) {
	P<Ref> ref = th.popRef("oscmap : ref");
	P<String> oscAddress = th.popString("oscmap : oscAddress");
	int port = (int) th.popInt("oscmap : port");
	P<String> netAddress = th.popString("oscmap : netAddress");

	unsigned long addr = GetHostByName(netAddress->s);
	auto key = std::make_tuple(addr, port);
	auto search = oscHandlers.find(key);
	if(search == oscHandlers.end()) {
		search = oscHandlers.emplace(key, OscHandler(addr, port)).first;
		search->second.startListening();
	}

	search->second.listener.putMapping(oscAddress->s, ref);
}

static void oscunmap_(Thread& th, Prim* prim) {
	P<String> oscAddress = th.popString("oscunmap : oscAddress");
	int port = (int) th.popInt("oscunmap : port");
	P<String> netAddress = th.popString("oscunmap : netAddress");
	
	unsigned long addr = GetHostByName(netAddress->s);
	auto key = std::make_tuple(addr, port);

	auto handlerSearch = oscHandlers.find(key);
	if(handlerSearch == oscHandlers.end()) {
		post("no such OSC handler\n");
		return;
	}

	auto& handler = handlerSearch->second;
	handler.listener.removeMapping(oscAddress->s);

	if(handler.listener.numMappings() == 0) {
		post("remove handler\n");
		oscHandlers.erase(handlerSearch);
	}
}
#endif

#define DEF(NAME, TAKES, LEAVES, HELP) 	vm.def(#NAME, TAKES, LEAVES, NAME##_, HELP);

void AddOscOps() {
#ifdef SAPF_OSC
	vm.addBifHelp("\n*** OSC control ***");
	DEF(oscmap, 4, 0, "(netAddress port oscAddress ref -->) map an OSC message to a Ref");
	DEF(oscunmap, 3, 0, "(netAddress port oscAddress -->) unmap an OSC message");
#endif // SAPF_OSC
}
