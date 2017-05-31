#include "pch.h"
#include "vsphere_api.h"

#include <algorithm>

using namespace std;
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;
using namespace concurrency;

using namespace Windows::Web::Http;

using namespace part4;

// Report - Local function only.
void report_object(std::shared_ptr<vc_object>& obj)
{
	wchar_t output[512];
	swprintf_s(output, 512, L"OBJECT - with %llu properties and %llu children.\n", obj->properties.size(), obj->children.size());
	OutputDebugStringW(output);

	for_each(begin(obj->properties), end(obj->properties), [](shared_ptr<vc_property> prop) {
		wchar_t output[512];
		swprintf_s(output, 512, L"Property %s: %s\n", prop->name.c_str(), prop->s.c_str());
		OutputDebugStringW(output);
	});

	for_each(begin(obj->children), end(obj->children), [](shared_ptr<vc_object> child) {
		report_object(child);
	});
}

std::shared_ptr<vc_property> part4::get_object_property(std::shared_ptr<vc_object>& obj, const wchar_t* name)
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

// Properties are automatically added to parent. Don't need to be returned.
// Objects are returned to be added to the children ..
std::shared_ptr<vc_object> part4::CheckJson(utility::string_t name, web::json::value v, std::shared_ptr<vc_object> parent)
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

concurrency::task<shared_ptr<vc_info>> part4::ConnectAsync(const wchar_t* server, const wchar_t* username, const wchar_t* password)
{
	std::shared_ptr<vc_info> pInfo = make_shared<vc_info>();
	//pInfo->config.set_validate_certificates(false);
	pInfo->server_credentials = credentials(username, password);
	pInfo->config.set_credentials(pInfo->server_credentials);
	pInfo->server = server;

	http_client client(server, pInfo->config);
	http_request request(methods::POST);
	request.set_request_uri(U("/rest/com/vmware/cis/session"));

	// TODO: Have to put this in as cpprestsdk isn't reliable on some networks.
	auto var = new HttpClient();



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

			char output[512];
			sprintf_s(output, 512, "Exception: %s.\n", e.what());
			OutputDebugStringA(output);
		}

		return pInfo;
	});
}

concurrency::task<std::shared_ptr<vc_task>> part4::SendRequestAsync(web::http::method method, std::shared_ptr<vc_info> pInfo, const wchar_t* uri)
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
			char output[512];
			sprintf_s(output, 512, "Exception: %s.\n", e.what());
			OutputDebugStringA(output);
		}

		return result;
	});
}