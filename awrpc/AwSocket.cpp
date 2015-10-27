
#include "ArchDeps.h"
#include "AwSocket.h"

#include <boost/asio.hpp>
#include <thread>
#include <memory>

using namespace std;

namespace AW {
	enum class Type { STRING, UINT32, INT32, FLOAT32, PARAMETER, FUNCTION, PACKET };
	uint32 readUInt32AndMove(std::shared_ptr<const byte> data, uint32& offset) {
		uint32 ret = 0;
		memcpy(&ret, data.get() + offset, sizeof(uint32));
		offset += sizeof(uint32);
		return ret;
	}

	Type readElementTypeAndMove(std::shared_ptr<const byte> data, uint32& offset) {
		Type t;
		memcpy(&t, data.get() + offset, sizeof(t));
		offset += sizeof(t);
		return t;
	}

	Type readElementType(std::shared_ptr<const byte> data, uint32 offset) {
		Type t;
		memcpy(&t, data.get() + offset, sizeof(t));
		return t;
	}
	void writeUInt32AndMove(std::shared_ptr<byte> data, uint32& offset, uint32 value) {
		memcpy(data.get() + offset, &value, sizeof(uint32));
		offset += sizeof(uint32);
	}
	void writeElementTypeAndMove(std::shared_ptr<byte> data, uint32& offset, Type type) {
		memcpy(data.get() + offset, &type, sizeof(type));
		offset += sizeof(Type);
	}

	AwSocket::AwSocket(std::shared_ptr<byte> firstPacket, uint32 offset, uint32 length) {
		packetsRemaining = readUInt32AndMove(firstPacket, offset);
		totalLength = readUInt32AndMove(firstPacket, offset);
		bufferLength = totalLength;
		buffer = std::shared_ptr<byte>(new byte[bufferLength]);

		uint32 currentDataLength = readUInt32AndMove(firstPacket, offset);
		if (sizeof(uint32) * 2 + currentDataLength > length)
			throw new std::overflow_error(__FUNCDNAME__);

		memcpy(buffer.get(), firstPacket.get() + offset, currentDataLength);
		currentPosition = currentDataLength;
	}
	AwSocket::AwSocket(uint32 packetsRemaining, uint32 totalLength, std::shared_ptr<byte> data, uint32 length)
		:packetsRemaining(packetsRemaining), totalLength(totalLength), buffer(data), currentPosition(0), bufferLength(length) {
	}
	void AwSocket::addPacket(std::shared_ptr<byte> data, uint32 offset, uint32 length) {
		if (packetsRemaining <= 0)
			throw std::invalid_argument("packets completed!");
		uint32 packetsRemaining2 = readUInt32AndMove(data, offset);
		if (packetsRemaining2 != packetsRemaining - 1)
			throw std::runtime_error(__FUNCSIG__);
		packetsRemaining = packetsRemaining2;

		if (totalLength != readUInt32AndMove(data, offset))
			throw std::runtime_error(__FUNCSIG__);

		uint32 currentDataLength = readUInt32AndMove(data, offset);
		if (sizeof(uint32) * 2 + currentDataLength > length)
			throw new std::overflow_error(__FUNCDNAME__);
		memcpy(buffer.get() + currentPosition, data.get() + offset, currentDataLength);
		currentPosition += currentDataLength;
	}

	bool AwSocket::isDone() const {
		return packetsRemaining == 0;
	}
	std::shared_ptr<byte> AwSocket::getPacketData(uint32& length) const {
		if (packetsRemaining != 0)
			throw std::runtime_error(__FUNCDNAME__);
		length = totalLength;

		std::shared_ptr<byte> ret(new byte[length]);
		memcpy(ret.get(), buffer.get(), length);
		return ret;
	}
	std::shared_ptr<byte> AwSocket::toByteArray(uint32& length) const {
		length = 3 * sizeof(uint32) + bufferLength;
		std::shared_ptr<byte> ret(new byte[length]);
		uint32 offset = 0;
		writeUInt32AndMove(ret, offset, packetsRemaining);
		writeUInt32AndMove(ret, offset, totalLength);
		writeUInt32AndMove(ret, offset, bufferLength);
		memcpy(ret.get() + offset, buffer.get(), bufferLength);
		return ret;
	}

	AW::string AwSocket::receiveString(std::shared_ptr<boost::asio::ip::tcp::socket>& sock) {
		AW::uint32 length = 0;
		auto buffer = receivePackets(sock, length);
		return AW::string((AW::character*)buffer.get(), length);
	}

	void AwSocket::sendString(std::shared_ptr<boost::asio::ip::tcp::socket> sock, const AW::string& str) {
		uint32 length = str.size() * sizeof(AW::character);
		auto buffer = new byte[length];
		std::copy(str.begin(), str.end(), buffer);
		sendPackets(sock, std::shared_ptr<byte>((byte*)buffer), 0, length);
	}

	std::shared_ptr<byte> AwSocket::receivePackets(std::shared_ptr<boost::asio::ip::tcp::socket>& sock, uint32& length) {
		std::shared_ptr<byte> buffer(new byte[PACKET_MAX_LENGTH]);
		uint32 count = 0;
		try {
			count = sock->receive(boost::asio::buffer(buffer.get(), PACKET_MAX_LENGTH));
			//count = boost::asio::read(*sock, boost::asio::buffer(buffer.get(), PACKET_MAX_LENGTH));
			if (count == 0) {
				length = 0;
				return shared_ptr<byte>(new byte[0]);
			}
		}
		catch (boost::system::system_error e) {
			throw std::runtime_error("disconnect");
		}

		AwSocket packet(buffer, 0, count);
		while (!packet.isDone()) {
			try {
				uint32 thisCount = sock->receive(boost::asio::buffer(buffer.get(), PACKET_MAX_LENGTH));
				packet.addPacket(buffer, 0, thisCount);
			}
			catch (boost::system::system_error e) {
				throw std::runtime_error("disconnect");
			}
		}
		return packet.getPacketData(length);
	}
	void AwSocket::sendPackets(std::shared_ptr<boost::asio::ip::tcp::socket>& sock, std::shared_ptr<byte> data, uint32 offset, uint32 length) {
		std::vector<AwSocket> ret;
		uint32 count = static_cast<int>(ceil((float)length / (PACKET_MAX_LENGTH - 3 * sizeof(uint32))));
		uint32 restLength = length;
		uint32 currentPosition = offset;
		for (uint32 i = 0; i < count; ++i) {
			uint32 dataSize = min(PACKET_MAX_LENGTH - 3 * sizeof(uint32), restLength);
			uint32 packetSize = dataSize + 3 * sizeof(uint32);
			std::shared_ptr<byte> packetData(new byte[packetSize]);

			uint32 packOffset = 0;
			writeUInt32AndMove(packetData, packOffset, count - i - 1);
			writeUInt32AndMove(packetData, packOffset, length);
			writeUInt32AndMove(packetData, packOffset, dataSize);

			memcpy(packetData.get() + packOffset, data.get() + currentPosition, dataSize);
			
			try {
				sock->send(boost::asio::buffer(reinterpret_cast<char*>(packetData.get()), packetSize));
			}
			catch (boost::system::system_error e) {
				throw std::runtime_error("disconnect");
			}
			
			currentPosition += dataSize;
			restLength -= dataSize;
		}
	}
}