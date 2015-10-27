#ifndef __AW_CLIENT_H__
#define __AW_CLIENT_H__

#include "ArchDeps.h"
#include "Elements.h"
#include "AwSocket.h"
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <memory>

namespace AW {
	//////////////////////////////////////////////////////////////////////////
	// Return value specializations
	//////////////////////////////////////////////////////////////////////////

	// Abstract ClientRet
	template <typename RetValT>
	class ClientRetBase {
	public:
		ClientRetBase(std::shared_ptr<SocketType> sock, const AW::string& name) :sock(sock), name(name) {}
		ClientRetBase() {}
		virtual RetValT operator()(std::shared_ptr<ElementBase> params) {
			return process(params);
		}
		RetValT process(std::shared_ptr<ElementBase> params) {
			auto pack = packFunctionTuple(params);
			std::basic_stringstream<AW::character> ss;
			sendAndReceive(sock, pack, ss);
			auto r = fromString(ss);
			return parse(r);
		}
		virtual RetValT parse(std::shared_ptr<ElementBase> params) = 0;

		AW::string getName() const { return name; }
		AW::string packFunctionTuple(std::shared_ptr<ElementBase> params) {
			std::shared_ptr<TupleType> ps(new TupleType);
			ps->add(std::shared_ptr<Element<AW::string>>(new Element<AW::string>(name)));
			ps->add(params);
			return ps->toString();
		}
		static void sendAndReceive(std::shared_ptr<boost::asio::ip::tcp::socket> sock, const AW::string& str, std::basic_stringstream<AW::character>& ss) {
			AwSocket::sendString(sock, str);
			ss << AwSocket::receiveString(sock);
		}
	protected:
		std::shared_ptr<SocketType> sock;
		AW::string name;
	};

	// ClientRet template (for concrete types(string, uint32, but not map<,> or vector<>)
	template<typename RetValT>
	class ClientRet :public ClientRetBase<RetValT> {
	public:
		using ClientRetBase<RetValT>::ClientRetBase;
		virtual RetValT parse(std::shared_ptr<ElementBase> ret) override {
			auto r = ret;
			return dynamic_cast<Element<RetValT>*>(r.get())->getValue();
		}
	};

	// ClientRet template specialization (for map, vector)
	// vector
	template<typename ElementT>
	class ClientRet<std::vector<ElementT>> :public ClientRetBase<std::vector<ElementT>> {
	public:
		using ClientRetBase<std::vector<ElementT>>::ClientRetBase;
		virtual std::vector<ElementT> parse(std::shared_ptr<ElementBase> retEle) override {
			std::vector<ElementT> ret;
			dynamic_cast<TupleType*>(retEle.get())->for_each_const([&ret](std::shared_ptr<AW::ElementBase> element) -> void {
				ret.push_back(ClientRet<ElementT>().parse(element));
			});
			return ret;
		}
	};

	// map
	template<typename KeyT, typename ValT>
	class ClientRet<std::map<KeyT, ValT>> :public ClientRetBase<std::map<KeyT, ValT>> {
	public:
		using ClientRetBase<std::map<KeyT, ValT>>::ClientRetBase;
		virtual std::map<KeyT, ValT> parse(std::shared_ptr<ElementBase> retEle) override {
			std::map<KeyT, ValT> ret;
			dynamic_cast<MapType*>(retEle.get())->for_each_const([&ret](std::shared_ptr<AW::ElementBase> key, std::shared_ptr<AW::ElementBase> val) -> void {
				ret[ClientRet<KeyT>().parse(key)] = ClientRet<ValT>().parse(val);
			});
			return ret;
		}
	};

	//////////////////////////////////////////////////////////////////////////
	// Abstract Client
	//////////////////////////////////////////////////////////////////////////
	template<typename RetValT, typename FirstArgT>
	class ClientBase :public ClientRet<RetValT> {
	public:
		using ClientRet<RetValT>::ClientRet;

		virtual RetValT operator()(FirstArgT t) {
			return operator()(std::shared_ptr<TupleType>(new TupleType), t);
		}
		virtual RetValT operator()(std::shared_ptr<TupleType> params, FirstArgT t) {
			parse(params, t);
			return ClientRetBase<RetValT>::process(params);
		}
		virtual void parse(std::shared_ptr<TupleType> params, FirstArgT t) = 0;
	protected:
		std::shared_ptr<SocketType> sock;
	};

	//////////////////////////////////////////////////////////////////////////
	// Specialization
	//////////////////////////////////////////////////////////////////////////

	// Generic Client (for many parameters)
	template<typename RetValT, typename FirstArgT, typename...ArgsT>
	class Client :public Client<RetValT, ArgsT...> {
	public:
		using Client<RetValT, ArgsT...>::Client;
		RetValT operator()(FirstArgT t, ArgsT... args) {
			return operator()(std::shared_ptr<TupleType>(new TupleType), t, args...);
		}
	private:
		RetValT operator()(std::shared_ptr<TupleType> params, FirstArgT t, ArgsT... args) {
			Client<RetValT, FirstArgT>().parse(params, t);
			return Client<RetValT, ArgsT...>::operator()(params, args...);
		}
	};

	// Concrete Client classes(for string, uint32..., but not map<,> ..)
	template<typename RetValT, typename FirstArgT>
	class Client<RetValT, FirstArgT> :public ClientBase<RetValT, FirstArgT> {
	public:
		using ClientBase<RetValT, FirstArgT>::ClientBase;
		virtual void parse(std::shared_ptr<TupleType> params, FirstArgT t) override {
			params->add(std::shared_ptr<Element<FirstArgT>>(new Element<FirstArgT>(t)));
		}
	};

	// Generic Client classes(for map, vector)
	// vector (Tuple)
	template<typename RetValT, typename ElementT>
	class Client<RetValT, std::vector<ElementT>> :public ClientBase<RetValT, std::vector<ElementT>> {
	public:
		using ClientBase<RetValT, std::vector<ElementT>>::ClientBase;
		virtual void parse(std::shared_ptr<TupleType> params, std::vector<ElementT> t) override {
			std::shared_ptr<TupleType> p(new TupleType);
			for (auto e : t)
				Client<RetValT, ElementT>().parse(p, e);
			params->add(p);
		}
	};

	// map
	template<typename RetValT, typename KeyT, typename ValT>
	class Client<RetValT, std::map<KeyT, ValT>> :public ClientBase<RetValT, std::map<KeyT, ValT>> {
	public:
		using ClientBase<RetValT, std::map<KeyT, ValT>>::ClientBase;
		virtual void parse(std::shared_ptr<TupleType> params, std::map<KeyT, ValT> t) override {
			std::shared_ptr<MapType> p(new MapType);
			for (auto e : t) {
				std::shared_ptr<TupleType> p1(new TupleType), p2(new TupleType);
				Client<RetValT, KeyT>().parse(p1, e.first);
				Client<RetValT, ValT>().parse(p2, e.second);
				p->add(p1->get(0), p2->get(0));
			}
			params->add(p);
		}
	};
}

#endif