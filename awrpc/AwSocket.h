#ifndef __AW_PACKETS_H__
#define __AW_PACKETS_H__

#include "ArchDeps.h"
#include "Elements.h"
#include <boost/asio.hpp>
#include <memory>	// shared_ptr
#include <cmath>

namespace AW {
	typedef boost::asio::ip::tcp::socket SocketType;

	inline uint32 min(uint32 a, uint32 b) {
		return a > b ? b : a;
	}

	class AwSocket {
	public:
		static AW::string receiveString(std::shared_ptr<boost::asio::ip::tcp::socket>& sock);
		static void sendString(std::shared_ptr<boost::asio::ip::tcp::socket> sock, const AW::string& str);

		static std::shared_ptr<byte> receivePackets(std::shared_ptr<boost::asio::ip::tcp::socket>& sock, uint32& length);
		static void sendPackets(std::shared_ptr<boost::asio::ip::tcp::socket>& sock, std::shared_ptr<byte> data, uint32 offset, uint32 length);
	private:
		AwSocket(std::shared_ptr<byte> firstPacket, uint32 offset, uint32 length);
		AwSocket(uint32 packetsRemaining, uint32 totalLength, std::shared_ptr<byte> data, uint32 length);
		void addPacket(std::shared_ptr<byte> data, uint32 offset, uint32 length);
		bool isDone() const;
		std::shared_ptr<byte> getPacketData(uint32& length) const;
		std::shared_ptr<byte> toByteArray(uint32& length) const;
	private:
		uint32 packetsRemaining;
		uint32 totalLength;
		std::shared_ptr<byte> buffer;
		uint32 bufferLength;
		uint32 currentPosition;
	};
};

#endif