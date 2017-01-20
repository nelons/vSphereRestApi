#include <iostream>
#include <conio.h>
#include <string>
#include <ppltasks.h>
#include <cpprest/http_client.h>

using namespace std;
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;
using namespace concurrency;

void main()
{
	web::http::client::http_client_config config;
	config.set_validate_certificates(false);

	std::wstring vcenter_url = L"";

	if (vcenter_url.length() == 0) {
		printf("Enter the IP/DNS of the vCenter Server: ");
		std::wstring vcenter_address;
		getline(wcin, vcenter_address);
		vcenter_url = L"https://";
		vcenter_url += vcenter_address;
	}

	std::wstring admin_username = L"administrator@vsphere.local";
	std::wstring admin_password = L"";

	if (admin_password.length() == 0) {
		wprintf(L"Enter the password for the '%s' account: ", admin_username.c_str());
		getline(wcin, admin_password);
	}

	credentials cred(admin_username.c_str(), admin_password.c_str());
	config.set_credentials(cred);

	http_client client(vcenter_url.c_str(), config);

	wstring token = L"";
	uri_builder builder(U("/rest/com/vmware/cis/session"));
	create_task(client.request(methods::POST, builder.to_string())).then([](http_response response) {
		printf("SSO Request Response Status Code: %d\n", response.status_code());

		return response.extract_json();

	}).then([&token](web::json::value v) {

		if (v.has_field(U("value"))) {
			json::value f = v.at(U("value"));
			token = f.as_string();
			wprintf(L"SSO Token value: %s.\n", f.as_string().c_str());
		}

	}).then([](task<void> t) {
		try {
			t.get();

		}
		catch (const std::exception& e) {
			printf("Exception: %s\n", e.what());

		}
	}).wait();

	if (token.length() > 0) {
		printf("Successfully authenticated against SSO.\n");

		/*
		*	We can now send requests against the vCenter.
		*/
		web::http::client::http_client_config vc_config;
		vc_config.set_validate_certificates(false);
		http_client vc_client(vcenter_url.c_str(), vc_config);

		uri_builder vc_vm_uri(U("/rest/vcenter/host"));

		http_request vc_req(methods::GET);
		vc_req.headers().add(U("vmware-api-session-id"), token.c_str());
		vc_req.set_request_uri(vc_vm_uri.to_string());

		create_task(vc_client.request(vc_req)).then([](http_response response) {
			printf("/rest/vcenter/host Request Status Code: %d\n", response.status_code());

		}).wait();

	}

	cout << "Press any key to exit." << endl;
	_getch();
}