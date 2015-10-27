#ifndef __AW_ELEMENTS__
#define __AW_ELEMENTS__

#include "ArchDeps.h"
#include <algorithm>
#include <vector>
#include <sstream>
#include <memory>
#include <iostream>
#include <map>
#include <functional>
#include <codecvt>

namespace AW {
	//////////////////////////////////////////////////////////////////////////
	// Constants
	//////////////////////////////////////////////////////////////////////////
	constexpr AW::uint32 TypeStringLength = 2;
	constexpr AW::character* StringTypeName = t("SS");
	constexpr AW::character* UInt32TypeName = t("U4");
	constexpr AW::character* Int32TypeName = t("I4");
	constexpr AW::character* Real64TypeName = t("R8");
	constexpr AW::character* TupleTypeName = t("TP");
	constexpr AW::character* MapTypeName = t("MP");

	//////////////////////////////////////////////////////////////////////////
	// Declarations
	//////////////////////////////////////////////////////////////////////////
	class ElementBase;
	static std::shared_ptr<ElementBase> fromString(std::basic_stringstream<AW::character>& ss);

	//////////////////////////////////////////////////////////////////////////
	// Helper Functions
	//////////////////////////////////////////////////////////////////////////
	inline bool atEof(std::basic_stringstream<AW::character>& s) {
		return (size_t)s.tellg() == (s.str().size() * sizeof(AW::character));
	}
	inline void assert_format(bool condition) {
		if (!condition) {
			throw std::runtime_error("wrong format");
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Concrete Type Element Traits
	//////////////////////////////////////////////////////////////////////////
	template<typename T> 
	class ElementTrait {
		static AW::uint32 getSize(const T& v);
		static AW::string&& toString(const T& v);
	};

	template<>
	class ElementTrait<AW::uint32> {
	public:
		explicit ElementTrait(const AW::uint32& v) :value(v) { }
		AW::uint32 getValue() const { return value; }
		AW::uint32 getSize() {
			if (str.size() == 0) {
				std::basic_stringstream<character> ss;
				ss << std::hex << value;
				str = ss.str();
			}
			return str.size() * sizeof(AW::character);
		}
		AW::string toString() {
			if (str.size() == 0) {
				std::basic_stringstream<character> ss;
				ss << value;
				str = ss.str();
			}
			return str;
		}
		static const character* getType() {
			return t("U4");
		}
		static std::shared_ptr<AW::uint32> fromString(const AW::string& s) {
			std::basic_stringstream<character> ss;
			AW::uint32 value;
			ss << s;
			ss >> std::hex >> value;
			return std::shared_ptr<AW::uint32>(new AW::uint32(value));
		}
	private:
		AW::uint32 value;
		AW::string str;
	};

	template<>
	class ElementTrait<AW::string> {
	public:
		explicit ElementTrait(const AW::string& s) :str(s) { }
		AW::string getValue() const { return str; }

		AW::uint32 getSize() {
			return sizeof(AW::character) * str.size();
		}
		AW::string toString() {
			return str;
		}

		static std::shared_ptr<AW::string> fromString(const AW::string& ss) {
			return std::shared_ptr<AW::string>(new AW::string(ss));
		}

		static const character* getType() {
			return t("SS");
		}
	private:
		AW::string str;
	};

	//////////////////////////////////////////////////////////////////////////
	// Abstract Element
	//////////////////////////////////////////////////////////////////////////
	class ElementBase {
	public:
		virtual AW::string toString() = 0;
		virtual AW::string getType() const = 0;
	};

	//////////////////////////////////////////////////////////////////////////
	// Tuple (or Array)
	//////////////////////////////////////////////////////////////////////////
	class TupleType :public ElementBase {
	public:
		TupleType() { }

		template<typename ElementT> 
		explicit TupleType(const std::vector<ElementT>& elements) {
			for (auto e : elements) {
				this->elements.push_back(new std::shared_ptr<ElementT>(new ElementT(e)));
			}
		}
		
		/* Element size */
		uint32 size() const {
			return elements.size();
		}

		/* Traverse array(elements have the same type) */
		template<typename ValT>
		void for_each_const(std::function<void(const ValT&)> func) const {
			for (auto e : elements) {
				func(e);
			}
		}

		/* Traverse tuple(elements have different types) */
		void for_each_const(std::function<void(std::shared_ptr<ElementBase>)> func) const {
			for (auto e : elements) {
				func(e);
			}
		}

		std::shared_ptr<ElementBase> get(uint32 index) {
			return elements[index];
		}

		template<typename T>
		T get(uint32 index) {
			return *dynamic_cast<T*>(elements[index].get());
		}
		void add(std::shared_ptr<ElementBase> e) {
			elements.push_back(e);
		}
		std::shared_ptr<ElementBase> pop() {
			auto ret = *(elements.end() - 1);
			elements.pop_back();
			return ret;
		}

		virtual AW::string toString() override {
			std::basic_stringstream<AW::character> ss, buffer;

			for (auto element : elements) {
				buffer << element->toString();
			}
			AW::string bufStr(buffer.str());

			ss << t("<") << TupleTypeName << t(" ") << std::hex << bufStr.size() * sizeof(AW::character) << t(">") << bufStr;
			return ss.str();
		}
		static std::shared_ptr<TupleType> fromStringData(std::basic_stringstream<AW::character>& ss) {
			std::shared_ptr<TupleType> ret(new TupleType);
			while (!atEof(ss)) {
				assert_format(!ss.bad());
				auto v = fromString(ss);
				ret->elements.push_back(v);
				//std::cout << "pos: " << ss.tellg() << " total: " << ss.str().size() << std::endl;
			}
			return ret;
		}

		virtual AW::string getType() const override {
			return TupleTypeName;
		}
	private:
		std::vector<std::shared_ptr<ElementBase>> elements;
	};

	//////////////////////////////////////////////////////////////////////////
	// Map
	//////////////////////////////////////////////////////////////////////////
	class MapType :public ElementBase {
	public:
		MapType() { }

		template<typename KeyT, typename ValT>
		MapType(const std::map<KeyT, ValT>& m) {
			for (auto e : m) {
				maps[e.first->toString()] = e.second;
			}
		}

		template<typename KeyT, typename ValT>
		std::shared_ptr<ValT> get(const KeyT& key) {
			return dynamic_cast<std::shared_ptr<ValT>>(maps[key.toString]);
		}

		void for_each_const(std::function<void(std::shared_ptr<ElementBase>, std::shared_ptr<ElementBase>)> func) const {
			for (auto element : maps) {
				std::basic_stringstream<AW::character> ss(element.first);
				func(fromString(ss), element.second);
			}
		}

		void add(std::shared_ptr<ElementBase> key, std::shared_ptr<ElementBase> value) {
			maps[key->toString()] = value;
		}
		virtual AW::string getType() const override {
			return MapTypeName;
		}
		virtual AW::string toString() override {
			std::basic_stringstream<AW::character> ss, buffer;
			for (auto element : maps) {
				buffer << element.first << element.second->toString();
			}
			auto bufStr = buffer.str();
			ss << "<" << MapTypeName << " " << std::hex << sizeof(AW::character) * bufStr.size() << ">" << bufStr;
			return ss.str();
		}
		static std::shared_ptr<MapType> fromStringData(std::basic_stringstream<AW::character>& ss) {
			std::shared_ptr<MapType> ret(new MapType);
			while (!atEof(ss)) {
				assert_format(!ss.bad());
				auto key = fromString(ss);
				auto val = fromString(ss);
				ret->maps[key->toString()] = val;
			}
			return ret;
		}
	private:
		std::map<AW::string, std::shared_ptr<ElementBase>> maps;
	};

	//////////////////////////////////////////////////////////////////////////
	// Concrete Type Element(string, uint32)
	//////////////////////////////////////////////////////////////////////////
	template<typename T, typename TraitT = ElementTrait<T>>
	class Element :public ElementBase {
	public:
		explicit Element(const T& v) :trait(v) { }
		AW::string toString() {
			std::basic_stringstream<AW::character> ss;
			ss << t("<") << TraitT::getType() << t(" ") << std::hex << trait.getSize() << t(">");
			ss << trait.toString();
			return ss.str();
		}

		T getValue() const {
			return trait.getValue();
		}
		AW::string getType() const {
			return TraitT::getType();
		}

	private:
		friend std::shared_ptr<ElementBase> fromString(std::basic_stringstream<AW::character>& ss);
		static std::shared_ptr<Element<T, TraitT>> fromString(const AW::string& s) {
			return std::shared_ptr<Element<T, TraitT>>(new Element<T, TraitT>(*TraitT::fromString(s)));
		}
		TraitT trait;
	};

	//////////////////////////////////////////////////////////////////////////
	// Create an element from string
	//////////////////////////////////////////////////////////////////////////
	static std::shared_ptr<ElementBase> fromString(std::basic_stringstream<AW::character>& ss) {
		//std::cout << AwStringToStdString(ss.str()) << std::endl;
		character leftAngleBracket, blankspace;
		character typeBuffer[TypeStringLength + 1]; typeBuffer[TypeStringLength] = t('\0');
		character sizeString[sizeof(AW::uint32) * 2 + 1]; sizeString[sizeof(AW::uint32) * 2] = t('\0');
		ss.read(&leftAngleBracket, 1);
		ss.read(typeBuffer, TypeStringLength);
		AW::string type(typeBuffer, TypeStringLength);
		ss.read(&blankspace, 1);
		assert_format(leftAngleBracket == t('<'));
		assert_format(blankspace == t(' '));

		AW::uint32 pos = 0, length;
		while (true) {
			character c;
			assert_format(!ss.eof() && !ss.bad());
			ss.read(&c, 1);
			assert_format(ss.gcount() == 1);

			if (c != t('>'))
				sizeString[pos++] = c;
			else
				break;
		}
		sizeString[pos] = t('\0');
		// string to uint32
		std::basic_stringstream<AW::character> bss(sizeString);
		bss >> std::hex >> length;
		AW::character* contentBuffer = new AW::character[length];
		//std::cout << ss.str().size() << std::endl;

		//auto readCount = ss.readsome(contentBuffer, length);
		ss.read(contentBuffer, length);
		auto readCount = ss.gcount();

		assert_format(readCount == length);
		AW::string content(contentBuffer, length);

		if (type == ElementTrait<AW::string>::getType()) {
			return Element<AW::string>::fromString(content);
		}
		else if (type == ElementTrait<AW::uint32>::getType()) {
			return Element<AW::uint32>::fromString(content);
		}
		else if (type == TupleTypeName) {
			std::basic_stringstream<AW::character> ss(content);
			return TupleType::fromStringData(ss);
		}
		else if (type == MapTypeName) {
			std::basic_stringstream<AW::character> ss(content);
			return MapType::fromStringData(ss);
		}
		else {
			assert_format(false);
			return nullptr; // never here
		}
	}
}

#endif