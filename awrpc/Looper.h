#ifndef __AW_LOOPER_H__
#define __AW_LOOPER_H__

#include "ArchDeps.h"
#include <iostream>
#include <memory>
#include <functional>
#include <queue>
#include <condition_variable>
#include <sstream>
#include <map>
#include <algorithm>
#include <thread>

namespace AW {
	class Event {
	public:
		explicit Event(const AW::string& what = t(""), std::function<bool(const Event& e)> handlerFunc = nullptr)
			:what(what), handlerFunc(handlerFunc) { }

		Event(std::function<bool(const Event& e)> handlerFunc)
			:handlerFunc(handlerFunc) { }

		bool execute() {
			if (handlerFunc == nullptr) {
				return handle();
			}
			else {
				return handlerFunc(*this);
			}
		}
		virtual bool handle() {
			return false;
		}
		static AW::string type() { return t("Event"); }
		virtual AW::string getType() { return type(); }
		void setHandler(std::function<bool(const Event& e)> handlerFunc) {
			this->handlerFunc = handlerFunc;
		}
	protected:
		std::function<bool(const Event& e)> handlerFunc = nullptr;
		AW::string what;
	};

	class OutputEvent :public Event {
	public:
		OutputEvent(const std::string& content, const AW::string& what = t("")) :Event(what, nullptr), content(content) { }
		static AW::string type() { return t("OutputEvent"); }
		virtual AW::string getType() override { return type(); }

		virtual bool handle() {
			std::cout << "OutputEvent: " << content << std::endl;
			return true;
		}
	private:
		std::string content;
	};
	class QuitEvent :public Event {
	public:
		QuitEvent(const AW::string& what = t("")) :Event(what, nullptr) { }
		static AW::string type() { return t("QuitEvent"); }
		virtual AW::string getType() override { return type(); }

		virtual bool handle() { return true; }
	};
	class InitializedEvent :public Event {
	public:
		InitializedEvent(const AW::string& what = t("")) :Event(what, nullptr) { }
		static AW::string type() { return t("InitializedEvent"); }
		virtual bool handle() { return true; }
	};
	class Looper {
	public:
		// call from other threads
		// creation
		static std::shared_ptr<Looper> createLooper() {
			return std::shared_ptr<Looper>(new Looper);
		}
		// don't need to delete it
		void putEvent(Event* e) {
			std::shared_ptr<Event> ev(e);
			muEventQueue.lock();
			eventQueue.push(ev);
			muEventQueue.unlock();

			cvEventAdded.notify_all();
		}

		template<typename EventT>
		void waitForEventPreExecution() {
			std::shared_ptr<std::mutex> mu(new std::mutex);
			std::shared_ptr<std::unique_lock<std::mutex>> lock(new std::unique_lock<std::mutex>(*mu));
			std::shared_ptr<std::condition_variable> cv(new std::condition_variable);

			muPreExecTable.lock();
			preExecutionTale[EventT::type()].push_back(cv.get());
			muPreExecTable.unlock();
			cv->wait(*lock);
		}

		template<typename EventT>
		void waitForEventAfterExecution(std::unique_lock<std::mutex>* lock, std::condition_variable* cv) {
			muAfterExecTable.lock();
			afterExecutionTale[EventT::type()].push_back(cv);
			muAfterExecTable.unlock();
			cv->wait(*lock);
		}

		// from my thread
		void start() {
			std::unique_lock<std::mutex> lock(muEventAdded);

			// looper initialized
			putEvent(new InitializedEvent);

			while (true) {
				while (eventQueue.empty()) {
					cvEventAdded.wait(lock);
				}

				muEventQueue.lock();
				auto e = eventQueue.front();
				eventQueue.pop();
				muEventQueue.unlock();

				// check pre-execution table
				auto it = preExecutionTale.find(e->getType());
				if (it != preExecutionTale.end()) {
					for (auto el : it->second) {
						el->notify_all();
					}
					preExecutionTale.erase(it->first);
				}

				if (e->getType() == QuitEvent::type()) {
					return;
				}
				e->execute();
				eventCount++;

				// check after-execution table
				auto itAfter = afterExecutionTale.find(e->getType());
				if (itAfter != afterExecutionTale.end()) {
					for (auto el : itAfter->second) {
						el->notify_all();
					}
				}
			}
		}

		void startInNewThread() {
			std::thread([this]() -> void { this->start(); }).detach();
		}
	private:
		Looper() { } // disable inheritance and copy
		Looper(const Looper&);
		Looper& operator=(const Looper&);

		std::queue<std::shared_ptr<Event>> eventQueue;

		std::mutex muEventQueue;
		std::condition_variable cvEventAdded;
		std::mutex muEventAdded;

		std::map<AW::string, std::vector<std::condition_variable*>> preExecutionTale;
		std::mutex muPreExecTable;

		std::map<AW::string, std::vector<std::condition_variable*>> afterExecutionTale;
		std::mutex muAfterExecTable;

		//////////////////////////////////////////////////////////////////////////
		// for debug purpose
		//////////////////////////////////////////////////////////////////////////
		int eventCount = 0;
	};
}
#endif