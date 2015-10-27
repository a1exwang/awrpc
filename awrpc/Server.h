#ifndef __AW_SERVER_H__
#define __AW_SERVER_H__

#include "ArchDeps.h"
#include "Elements.h"
#include "AwSocket.h"
#include "Looper.h"

#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <functional>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>

namespace AW {

	//////////////////////////////////////////////////////////////////////////
	// Reduction (fixed)
	//////////////////////////////////////////////////////////////////////////
	template<typename RetValT, typename FirstArgT, typename...ArgsT>
	class Server :public Server<RetValT, ArgsT...> {
	public:
		virtual std::shared_ptr<ElementBase> callFromParameters(std::shared_ptr<TupleType> params) override {
			arg0 = Server<RetValT, FirstArgT>(nullptr, "").parse(params);
			return Server<RetValT, ArgsT...>::callFromParameters(params);
		}

		Server(const std::function<RetValT(FirstArgT, ArgsT...)>& func, const AW::string& name)
			:Server<RetValT, ArgsT...>([&, func](ArgsT... args) -> RetValT { return func(arg0, args...); }, name) { }
	private:
		FirstArgT arg0;
	};

	//////////////////////////////////////////////////////////////////////////
	// Abstract Server, used to store server function pointers into vectors
	//////////////////////////////////////////////////////////////////////////
	class AbstractServerBase {
	public:
		virtual std::shared_ptr<ElementBase> callFromParameters(std::shared_ptr<TupleType> params) { return nullptr; }
		virtual AW::string getName() const { return t(""); }
	};

	//////////////////////////////////////////////////////////////////////////
	// ServerRet classes
	//////////////////////////////////////////////////////////////////////////
	template<typename RetValT>
	class ServerRetBase :public AbstractServerBase {
		virtual std::shared_ptr<ElementBase> typeToElement(RetValT v) = 0;
	};

	// Server Return for uint32,string...
	template<typename RetValT>
	class ServerRet :public ServerRetBase<RetValT> {
	public:
		virtual std::shared_ptr<ElementBase> typeToElement(RetValT v) override {
			return std::shared_ptr<ElementBase>(new Element<RetValT>(v));
		}
	};

	// Server Return for vector, map
	// vector
	template<typename ElementType>
	class ServerRet<std::vector<ElementType>> :public ServerRetBase<std::vector<ElementType>> {
	public:
		virtual std::shared_ptr<ElementBase> typeToElement(std::vector<ElementType> v) override {
			std::shared_ptr<TupleType> ret(new TupleType);
			for (auto e : v) {
				auto elementBase = ServerRet<ElementType>().typeToElement(e);
				ret->add(elementBase);
			}
			return ret;
		}
	};
	// map
	template<typename KeyT, typename ValT>
	class ServerRet<std::map<KeyT, ValT>> :public ServerRetBase<std::map<KeyT, ValT>> {
	public:
		virtual std::shared_ptr<ElementBase> typeToElement(std::map<KeyT, ValT> m) override {
			std::shared_ptr<MapType> ret(new MapType);
			for (auto e : m) {
				auto elementBaseKey = ServerRet<KeyT>().typeToElement(e.first);
				auto elementBaseVal = ServerRet<ValT>().typeToElement(e.second);
				ret->add(elementBaseKey, elementBaseVal);
			}
			return ret;
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// Specialization
	//////////////////////////////////////////////////////////////////////////
	// Abstract Server Parameter Processors
	template <typename RetValT, typename FirstArgT>
	class ServerBase :public ServerRet<RetValT> {
	public:
		ServerBase() { }
		ServerBase(const std::function<RetValT(FirstArgT)>& func, const AW::string& name) : func(func), name(name) { }

		virtual std::shared_ptr<ElementBase> callFromParameters(std::shared_ptr<TupleType> params) override {
			return ServerRet<RetValT>::typeToElement(func(parse(params)));
		}

		virtual AW::string getName() const override { return name; }
		virtual FirstArgT parse(std::shared_ptr<TupleType> params) = 0;

	protected:
		FirstArgT arg;
		AW::string name;
		std::function<RetValT(FirstArgT)> func;
	};

	// Server Parameter Part(uint32, string)
	template<typename RetValT, typename FirstArgT>
	class Server<RetValT, FirstArgT> :public ServerBase<RetValT, FirstArgT> {
	public:
		using ServerBase<RetValT, FirstArgT>::ServerBase;
		virtual FirstArgT parse(std::shared_ptr<TupleType> params) {
			auto p = std::shared_ptr <Element<FirstArgT>>(new Element<FirstArgT>(*dynamic_cast<Element<FirstArgT>*>(params->pop().get())));
			return p->getValue();
		}
	};

	// Server Parameter Part(map, vector)
	// vector
	template<typename RetValT, typename ElementT>
	class Server<RetValT, std::vector<ElementT>> :public ServerBase<RetValT, std::vector<ElementT>> {
	public:
		using ServerBase<RetValT, std::vector<ElementT>>::ServerBase;
		virtual std::vector<ElementT> parse(std::shared_ptr<TupleType> params) {
			auto p = std::shared_ptr <TupleType>(new TupleType(*dynamic_cast<TupleType*>(params->pop().get())));
			std::vector<ElementT> ret;
			p->for_each_const([&ret](std::shared_ptr<ElementBase> e) -> void {
				std::shared_ptr<TupleType> tt(new TupleType);
				tt->add(e);
				ret.push_back(Server<RetValT, ElementT>().parse(tt));
			});
			return ret;
		}
	};

	// map
	template<typename RetValT, typename KeyT, typename ValT>
	class Server<RetValT, std::map<KeyT, ValT>> :public ServerBase <RetValT, std::map<KeyT, ValT>> {
	public:
		using ServerBase<RetValT, std::map<KeyT, ValT>>::ServerBase;

		virtual std::map<KeyT, ValT> parse(std::shared_ptr<TupleType> params) {
			auto p = std::shared_ptr <MapType>(new MapType(*dynamic_cast<MapType*>(params->pop().get())));
			std::map<KeyT, ValT> ret;
			p->for_each_const([&ret](std::shared_ptr<ElementBase> key, std::shared_ptr<ElementBase> val) -> void {
				std::shared_ptr<TupleType> tt(new TupleType);
				tt->add(key);
				tt->add(val);
				ret[Server<RetValT, KeyT>().parse(tt)] =
					Server<RetValT, ValT>().parse(tt);
			});
			return ret;
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// Server Connection
	//////////////////////////////////////////////////////////////////////////
	using boost::asio::ip::tcp;
	class AwRpc {
	public:
		AwRpc(AW::uint32 port, std::vector<std::shared_ptr<AbstractServerBase>>&& tab) :port(port), tab(tab), comPort(COMMUNICATION_PORT_START) { }
		explicit AwRpc(std::vector<std::shared_ptr<AbstractServerBase>> tab) :port(DEFAULT_PORT), tab(tab), comPort(COMMUNICATION_PORT_START){ }

		void startService() {
			boost::asio::io_service service;
			std::shared_ptr<tcp::acceptor> acc(new tcp::acceptor(service, tcp::endpoint(tcp::v4(), DEFAULT_PORT)));

			while (true) {
				socket = std::shared_ptr<boost::asio::ip::tcp::socket>(new tcp::socket(service));
				acc->accept(*socket);
				std::cout << AwStringToStdString(t("New Connection")) << std::endl;

				std::basic_stringstream<character> ss;
				AW::string comPortStr;
				unsigned short pt = comPort;
				ss << comPort;
				ss >> comPortStr;
				comPort++;

				std::thread([pt, this]() -> void {
					boost::asio::io_service service;
					std::shared_ptr<tcp::acceptor> acc(new tcp::acceptor(service, tcp::endpoint(tcp::v4(), pt)));
					auto socket = std::shared_ptr<boost::asio::ip::tcp::socket>(new tcp::socket(service));

					acc->accept(*socket);
					auto looper = Looper::createLooper();
					looper->startInNewThread();

					while (true) {
						try {
							receiveFunctionCall(socket, tab, looper);
						}
						catch (std::exception& e) {
							std::cout << e.what() << std::endl;
							break;
						}
					}
					std::cout << "Client Down" << std::endl;
				}).detach();
				
				AwSocket::sendString(socket, comPortStr);

				//std::this_thread::sleep_for(std::chrono::milliseconds(500));

				socket->close();
				std::cout << "server close" << std::endl;
			}
			std::cout << "server Down!" << std::endl;
		}
		void startServiceAsync() {
			std::thread([this]() -> void { this->startService(); }).detach();
		}
		//bool isServerUp() const { return serverUp; }
		std::shared_ptr<boost::asio::ip::tcp::socket> getSocket() const { return socket; }

		static void receiveFunctionCall(std::shared_ptr<boost::asio::ip::tcp::socket> socket, const std::vector<std::shared_ptr<AbstractServerBase>>& tab, std::shared_ptr<Looper> looper = nullptr) {
			// Lock the socket
			//////////////////////////////////////////////////////////////////////////
			// receive here
			std::basic_stringstream<AW::character> ss(AwSocket::receiveString(socket));
			//std::cout << AwStringToStdString(ss.str()) << std::endl;
			auto funcTuple = std::shared_ptr<TupleType>(new TupleType(*dynamic_cast<TupleType*>(fromString(ss).get())));
			auto funcName = funcTuple->get<Element<AW::string>>(0).getValue();
			auto params = std::shared_ptr<TupleType>(new TupleType(funcTuple->get<TupleType>(1)));

			auto funcClosure = [&tab, funcName, params, &socket](const Event&) -> bool {
				for (auto f : tab) {
					if (f->getName() == funcName) {
						auto ret = f->callFromParameters(params);
						std::this_thread::sleep_for(std::chrono::milliseconds(10));

						//////////////////////////////////////////////////////////////////////////
						// send here
						AwSocket::sendString(socket, ret->toString());
						break;
					}
				}
				return true;
			};
			if (looper == nullptr) {
				Event e;
				funcClosure(e);
			}
			else 
				looper->putEvent(new Event(funcClosure));
		}

		uint32 port;
		uint32 comPort;
		std::vector<std::shared_ptr<AbstractServerBase>> tab;
		std::shared_ptr<boost::asio::ip::tcp::socket> socket;
		std::mutex muSocket;

		//Looper* looper;
		//bool serverUp = false;
	};

	// Client Connection helper function
	void clientStart(std::string addr, std::function<void(std::shared_ptr<tcp::socket>)> callback) {
		boost::asio::io_service io_service;
		std::shared_ptr<tcp::socket> dispatchSocket(new tcp::socket(io_service));
		dispatchSocket->connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(addr), DEFAULT_PORT));

		AW::string portStr = AwSocket::receiveString(dispatchSocket);
		uint32 port;
		std::basic_stringstream<AW::character> ss;
		ss << portStr;
		ss >> port;
		dispatchSocket->close();
		std::cout << "client close" << std::endl;

		std::thread([&callback, addr, port]() -> void {
			boost::asio::io_service io_service;
			std::shared_ptr<boost::asio::ip::tcp::socket> socket(new boost::asio::ip::tcp::socket(io_service));

			try {
				socket->connect(tcp::endpoint(boost::asio::ip::address::from_string(addr), port));
				callback(socket);
			}
			catch (std::exception& e) {
				std::cout << e.what() << std::endl;
			}
		}).join();
	}

}

#endif