#ifndef __AW_ARCH_DEPS_H__
#define __AW_ARCH_DEPS_H__

#include <string>
#include <vector>
#include <algorithm>
#include <codecvt>
namespace AW {

//#if defined _M_IX86
#define __AW_MSVC_WIN_32__
#define __AW_LITTLE_ENDIAN__
//#endif
#define __AW_UTF8__

#ifndef _M_IX86
	#define __FUNCDNAME__ "func"
	#define __FUNCSIG__ "funcsig"
#endif

#ifdef __AW_MSVC_WIN_32__
	typedef int				empty_t;
	constexpr empty_t		empty = 0;

#ifdef __AW_UTF16__
	typedef std::u16string	string;
#define t(str) u##str
	typedef char16_t		character;
	inline static std::string AwStringToStdString(AW::string awStr) {
		static std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codecvt;
		return codecvt.to_bytes(awStr);
	}

	inline static AW::string StdStringToAwString(std::string str) {
		std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> codecvt;
		return codecvt.from_bytes(str);
	}
#elif defined __AW_UTF8__
	typedef std::string		string;
#define t(str) str
	typedef char			character;
	inline static std::string AwStringToStdString(AW::string awStr) {
		return awStr;
	}

	inline static AW::string StdStringToAwString(std::string str) {
		return str;
	}
#endif

	typedef unsigned char	byte;
	typedef unsigned int	uint32;
	typedef int				int32;
	typedef double			real64;

#endif

#ifdef __AW_LITTLE_ENDIAN__
#define toAwEndian(x) x
#endif

#ifdef __AW_BIG_ENDIAN__
	// only for inner types
	template<typename T>
	std::vector<AW::byte>&& toAwEndian(const std::vector<AW::byte>& v) {
		std::vector<AW::byte> ret(v.size());
		reverse_copy(v.cbegin(), v.cend(), ret.begin());
		return std::move(ret);
	}
#endif

	constexpr uint32 DEFAULT_PORT = 23521;
	constexpr uint32 WORKER_PORT_START = 23522;
	constexpr uint32 MAX_PORTS = 1000;
	constexpr uint32 COMMUNICATION_PORT_START = 25523;
	constexpr uint32 RECV_SEND_INTERVAL_MS = 10;


    constexpr character* CALLBACK_FUNC_NAME = t("___callback");
    constexpr character* NOP = t("__NOP");

	constexpr uint32 PACKET_MAX_LENGTH = 1400;
};

#endif