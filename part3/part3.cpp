/*
	- Put main functions in a separate cpp file. (unity build ?)
	- Get Tags and items that are tagged.
*/

#include <iostream>
#include <conio.h>
#include <string>
#include <ppltasks.h>
#include <memory>
#include <algorithm>
#include <vector>
#include <cpprest/http_client.h>

using namespace std;
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;
using namespace concurrency;

struct vc_info {
	std::wstring server;
	std::wstring token;

	http_client_config config;
	credentials server_credentials;

	int last_status_code;
	std::string last_exception;
};

struct vc_property {
	std::wstring name;
	int type;

	// unrestricted unions not working on MSVC mean s cannot be a member :(
	union {
		bool b;
		int i;
		double d;
	};

	std::wstring s;
};

struct vc_object {
	std::vector<std::shared_ptr<vc_property>> properties;

	std::shared_ptr<vc_object> parent;
	std::vector<std::shared_ptr<vc_object>> children;
};

std::shared_ptr<vc_property> get_object_property(std::shared_ptr<vc_object>& obj, const wchar_t* name)
{
	std::shared_ptr<vc_property> result = nullptr;

	if (obj != nullptr && name != 0 && wcslen(name) > 0) {
		std::vector<std::shared_ptr<vc_property>>::iterator find_result = std::find_if(begin(obj->properties), end(obj->properties), [name](std::shared_ptr<vc_property> prop) {
			return (_wcsicmp(prop->name.c_str(), name) == 0);
		});

		if (find_result != end(obj->properties)) {
			result = *find_result;
		}
	}

	return result;
}

void report_object(std::shared_ptr<vc_object>& obj)
{
	printf("OBJECT - with %d properties and %d children.\n", obj->properties.size(), obj->children.size());

	for_each(begin(obj->properties), end(obj->properties), [](shared_ptr<vc_property> prop) {
		wprintf(L"Property %s: %s\n", prop->name.c_str(), prop->s.c_str());
	});

	for_each(begin(obj->children), end(obj->children), [](shared_ptr<vc_object> child) {
		report_object(child);
	});
}

struct vc_task {
	std::weak_ptr<vc_info> pServer;
	int status_code;
	std::string exception;
	std::shared_ptr<vc_object> data;
};

// Properties are automatically added to parent. Don't need to be returned.
// Objects are returned to be added to the children ..
std::shared_ptr<vc_object> CheckJson(utility::string_t name, web::json::value v, std::shared_ptr<vc_object> parent = nullptr)
{
	std::shared_ptr<vc_object> result = nullptr;

	if (v.is_array()) {
		for (unsigned int i = 0; i < v.size(); ++i) {
			std::shared_ptr<vc_object> obj = CheckJson(U(""), v[i], parent);
		}

	}
	else if (v.is_object()) {
		result = make_shared<vc_object>();

		web::json::object o = v.as_object();
		std::for_each(begin(o), end(o), [&result](std::pair<utility::string_t, web::json::value> item) {
			CheckJson(item.first, item.second, result);
		});

		if (parent != nullptr)
			parent->children.push_back(result);

	}
	else {
		std::shared_ptr<vc_property> prop = std::make_shared<vc_property>();
		prop->name = name;

		if (v.is_string()) {
			prop->s = v.as_string();

		}
		else if (v.is_integer()) {
			prop->i = v.as_integer();

		}
		else if (v.is_boolean()) {
			prop->b = v.as_bool();

		}
		else if (v.is_double()) {
			prop->d = v.as_double();

		}

		if (parent)
			parent->properties.push_back(prop);
	}

	return result;
}

task<shared_ptr<vc_info>> ConnectAsync(const wchar_t* server, const wchar_t* username, const wchar_t* password)
{
	std::shared_ptr<vc_info> pInfo = make_shared<vc_info>();
	pInfo->config.set_validate_certificates(false);
	pInfo->server_credentials = credentials(username, password);
	pInfo->config.set_credentials(pInfo->server_credentials);
	pInfo->server = server;

	http_client client(server, pInfo->config);
	http_request request(methods::POST);
	request.set_request_uri(U("/rest/com/vmware/cis/session"));

	return concurrency::create_task(client.request(request)).then([pInfo](http_response response) {
		// if status is OK then extract json.
		pInfo->last_status_code = response.status_code();
		return response.extract_json();

	}).then([pInfo](web::json::value v) {
		// get and set the token
		std::shared_ptr<vc_object> data = CheckJson(U(""), v);
		if (data) {
			std::shared_ptr<vc_property> value = get_object_property(data, L"value");
			if (value) {
				pInfo->token = value->s;
			}
		}

	}).then([pInfo](task<void> t) {
		// exception catching.
		try {
			t.get();

		}
		catch (const std::exception& e) {
			pInfo->last_exception = e.what();
			printf("Exception: %s\n", e.what());

		}

		return pInfo;
	});
}

task<std::shared_ptr<vc_task>> SendRequestAsync(web::http::method method, std::shared_ptr<vc_info> pInfo, const wchar_t* uri)
{
	std::shared_ptr<vc_task> result = make_shared<vc_task>();
	result->pServer = pInfo;

	return concurrency::create_task([pInfo, uri, method]() {

		http_client client(pInfo->server, pInfo->config);
		http_request request(method);
		request.set_request_uri(uri);

		// We need to pass the token with the request.
		request.headers().add(U("vmware-api-session-id"), pInfo->token.c_str());

		return client.request(request);

	}).then([pInfo, result](http_response response) {
		pInfo->last_status_code = response.status_code();
		result->status_code = pInfo->last_status_code;
		return response.extract_json();

	}).then([result](web::json::value v) {
		// parse the result and store.
		result->data = CheckJson(U(""), v);

	}).then([result](task<void> t) -> std::shared_ptr<vc_task> {
		try {
			t.get();

		}
		catch (const std::exception& e) {
			printf("Get exception: %s.\n", e.what());
		}

		return result;
	});
}

void main()
{
	std::wstring vcenter_url = L"https://auto-vc.auto.internal";

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

	std::shared_ptr<vc_info> pInfo = ConnectAsync(vcenter_url.c_str(), admin_username.c_str(), admin_password.c_str()).get();
	if (pInfo && pInfo->last_status_code == 200) {
		wprintf(L"Connected successfully to %s.\n", vcenter_url.c_str());

		shared_ptr<vc_task> tag_results = SendRequestAsync(methods::GET, pInfo, L"/rest/com/vmware/cis/tagging/tag").get();
		if (tag_results->status_code == 200) {
			if (tag_results->data) {
				report_object(tag_results->data);

				for_each(begin(tag_results->data->properties), end(tag_results->data->properties), [pInfo](shared_ptr<vc_property> prop) {
					wstring target = L"/rest/com/vmware/cis/tagging/tag/id:";
					target += prop->s.c_str();
					shared_ptr<vc_task> tag_info = SendRequestAsync(methods::GET, pInfo, target.c_str()).get();
					if (tag_info && tag_info->status_code == 200) {
						shared_ptr<vc_property> tag_name = nullptr;
						if (tag_info->data->children.size() > 0)
							tag_name = get_object_property(tag_info->data->children[0], L"name");

						// Get the list of objects with this tag.
						wstring tag_url = L"/rest/com/vmware/cis/tagging/tag-association/id:";
						tag_url += prop->s.c_str();
						tag_url += L"?~action=list-attached-objects";

						shared_ptr<vc_task> tagged_objects = SendRequestAsync(methods::POST, pInfo, tag_url.c_str()).get();
						if (tagged_objects && tagged_objects->status_code == 200) {

							// object is reported even if there is no data.
							if (tagged_objects->data->children.size() > 0) {
								if (tag_name)
									wprintf(L"Objects that are tagged with %s:\n", tag_name->s.c_str());

								//report_object(tagged_objects->data);

								// Output information.
								// tagged_objects->data->children[i] is an object.
								std::for_each(begin(tagged_objects->data->children), end(tagged_objects->data->children), [](std::shared_ptr<vc_object> object) {
									// Get details of the object.
									report_object(object);

									auto attached_object_id = get_object_property(object, L"id");
									auto attached_object_type = get_object_property(object, L"type");

									// do something with the tagged object here.


								});
							}
						}
					}
				});
			}
		}

	}
	else {
		// Could not connect.
		if (pInfo) {
			printf("ConnectAsync request returned code %d.\n", pInfo->last_status_code);

		}
		else {
			printf("Unknown error - ConnectAsync did not return server information object.\n");

		}
	}

	std::cout << "Press any key to exit." << endl;
	_getch();
}