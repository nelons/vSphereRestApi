#pragma once

#include <vector>
#include <string>
#include <cpprest/http_client.h>
#include <ppltasks.h>
#include <memory>

namespace part4
{
	struct vc_info {
		std::wstring server;
		std::wstring token;

		web::http::client::http_client_config config;
		web::http::client::credentials server_credentials;

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

	struct vc_task {
		std::weak_ptr<vc_info> pServer;
		int status_code;
		std::string exception;
		std::shared_ptr<vc_object> data;
	};

	std::shared_ptr<vc_property> get_object_property(std::shared_ptr<vc_object>& obj, const wchar_t* name);
	std::shared_ptr<vc_object> CheckJson(utility::string_t name, web::json::value v, std::shared_ptr<vc_object> parent = nullptr);
	
	concurrency::task<std::shared_ptr<vc_info>> ConnectAsync(const wchar_t* server, const wchar_t* username, const wchar_t* password);
	concurrency::task<std::shared_ptr<vc_task>> SendRequestAsync(web::http::method method, std::shared_ptr<vc_info> pInfo, const wchar_t* uri);
}