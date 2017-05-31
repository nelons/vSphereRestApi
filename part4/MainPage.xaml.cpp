//
// MainPage.xaml.cpp
// Implementation of the MainPage class.
//

#include "pch.h"
#include "MainPage.xaml.h"

#include "vsphere_api.h"

using namespace part4;

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Foundation::Collections;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

using namespace concurrency;

// The Blank Page item template is documented at https://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

MainPage::MainPage()
{
	InitializeComponent();
}


void MainPage::Button_Click(Platform::Object^ sender, Windows::UI::Xaml::RoutedEventArgs^ e)
{
	String^ vcenter = tbvCenter->Text;
	String^ username = tbUsername->Text;
	String^ password = tbPassword->Text;

	create_task(ConnectAsync(vcenter->Data(), username->Data(), password->Data())).then([this](std::shared_ptr<vc_info> pInfo) {
		if (pInfo && pInfo->last_status_code == 200) {
			this->btnConnect->IsEnabled = false;
		}
	});

}
