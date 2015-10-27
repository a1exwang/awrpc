#include "Looper.h"
#include "Server.h"
#include "Client.h"

#include <amule.h>
#include <amuleDlg.h>
#include <SearchDlg.h>
#include <MuleNotebook.h>
#include <SearchListCtrl.h>
#include <SearchFile.h>
#include <DownloadQueue.h>
#include <GuiEvents.h>
#include <TransferWnd.h>
#include <DownloadListCtrl.h>
#include <PartFile.h>

#include <muuli_wdr.h>
#include <wx/app.h>
#include <wx/choice.h>
#include <wx/window.h>

#include <thread>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>
#include <boost/asio.hpp>
#include <codecvt>
#include <mutex>
#include <condition_variable>

using namespace std;
using namespace AW;
using boost::asio::ip::tcp;

AW::string WxStringToAwString(const wxString& str) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
	return convert.to_bytes(wstring(str.GetData()));
}

wxString AwStringToWxString(const AW::string& str) {
	std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert;
	return wxString(convert.from_bytes(str));
}

mutex mutexPre;
mutex mutexAfter;

mutex muEvent;
condition_variable conEvent;

void wait() {
	unique_lock<mutex> lock1(muEvent);
	conEvent.wait(lock1);
}

void RPCServerStart() {
	thread([]() -> void {
		AwRpc* awrpc;
		auto rpcTable = std::vector<std::shared_ptr<AbstractServerBase>>({
			
			std::shared_ptr<AbstractServerBase>(new Server<AW::string, AW::string>([](AW::string v) -> AW::string { return v; }, t("echo"))),
			std::shared_ptr<AbstractServerBase>(new Server<std::vector<AW::string>, AW::string>([&](AW::string keyWord) -> std::vector<AW::string> {
				
				mutexPre.lock();
				auto searchDlg = theApp->amuledlg->m_searchwnd;
				// set search parameters
				dynamic_cast<wxChoice*>(searchDlg->FindWindow(ID_SEARCHTYPE))->SetSelection(2);
				dynamic_cast<wxTextCtrl*>(searchDlg->FindWindow(IDC_SEARCHNAME))->SetValue(AwStringToWxString(keyWord));
			
				int index = searchDlg->m_notebook->GetPageCount();

				// notify the UI thread to start search
				searchDlg->AddPendingEvent(wxCommandEvent(wxEVT_COMMAND_BUTTON_CLICKED, IDC_STARTS));
				// *********************
				// here we need to wait for the button clicked event to finish
				//wait();
				wxMilliSleep(1000);
				mutexPre.unlock();

				// wait for search results
				// change this number to change result count

				vector<AW::string> ret;
				wxSleep(10);
				
				mutexAfter.lock();
				// get search results
				CSearchListCtrl* page = dynamic_cast<CSearchListCtrl*>(searchDlg->m_notebook->GetPage(index));
				
				for (int i = 0; i < page->GetItemCount(); ++i) {
					CSearchFile* cfile = reinterpret_cast<CSearchFile*>(page->GetItemData(i));
					wxString ed2k = theApp->CreateED2kLink(cfile) + wxString(_("\n"));
					ret.push_back(WxStringToAwString(ed2k));
				}
				mutexAfter.unlock();

				return ret;
			}, t("searchByKeyword")))
		});
		awrpc = new AwRpc(rpcTable);
		awrpc->startService();
	}).detach();
}