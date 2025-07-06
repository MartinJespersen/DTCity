// The Digital Grove Codebase
// Copyright (c) 2024 Ryan Fleury. All rights reserved.

////////////////////////////////////////////////////////////////
//~ rjf: Main Layer Initialization

internal void HTTP_Init(void) {
  if (http_w32_state == 0) {
    Arena *arena = ArenaAlloc();
    http_w32_state = push_array(arena, HTTP_W32_State, 1);
    http_w32_state->arena = arena;
    http_w32_state->hSession =
        WinHttpOpen(0, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
  }
  // HTTP_InitReceipt init = {0};
}

////////////////////////////////////////////////////////////////
//~ rjf: Low-Level Request Functions

internal HTTP_Response HTTP_Request(Arena *arena, String8 url, String8 body,
                                    HTTP_RequestParams *params) {
  Temp scratch = ScratchBegin(&arena, 1);
  B32 good = 1;
  U32 code = 0;
  String8List response_data_strings = {0};
  {
    //- rjf: extract URL info
    String8 url_protocol_part = {0};
    String8 url_port_part = {0};
    String8 url_hostname_part = {0};
    String8 url_path_part = {0};
    {
      U64 protocol_delimiter_pos = FindSubstr8(url, Str8Lit("://"), 0, 0);
      U64 post_protocol_pos = 0;
      if (protocol_delimiter_pos < url.size) {
        post_protocol_pos = protocol_delimiter_pos + 3;
        url_protocol_part = Str8Prefix(url, post_protocol_pos);
      }
      U64 last_colon_pos = FindSubstr8(url, Str8Lit(":"), post_protocol_pos, 0);
      if (last_colon_pos < url.size) {
        url_port_part = Str8Skip(url, last_colon_pos + 1);
      }
      U64 first_non_protocol_slash_pos =
          FindSubstr8(url, Str8Lit("/"), post_protocol_pos, 0);
      if (first_non_protocol_slash_pos < url.size) {
        url_hostname_part = Str8Prefix(url, first_non_protocol_slash_pos);
        url_hostname_part = Str8Skip(url_hostname_part, post_protocol_pos);
        url_path_part = Str8Skip(url, first_non_protocol_slash_pos);
      } else {
        url_hostname_part = Str8Skip(url, post_protocol_pos);
      }
    }

    //- rjf: convert url parts to connection parameters
    U16 port = INTERNET_DEFAULT_HTTPS_PORT;
    String16 hostname16 = {0};
    {
      if (url_port_part.size != 0) {
        port = (U16)U64FromStr8(url_port_part, 10);
      } else if (Str8Match(url_protocol_part, Str8Lit("https://"),
                           MatchFlag_CaseInsensitive)) {
        port = INTERNET_DEFAULT_HTTPS_PORT;
      } else if (Str8Match(url_protocol_part, Str8Lit("http://"),
                           MatchFlag_CaseInsensitive)) {
        port = INTERNET_DEFAULT_HTTP_PORT;
      } else if (Str8Match(url_protocol_part, Str8Lit("ftp://"),
                           MatchFlag_CaseInsensitive)) {
        port = 21;
      } else if (Str8Match(url_protocol_part, Str8Lit("ssh://"),
                           MatchFlag_CaseInsensitive)) {
        port = 22;
      }
      hostname16 = Str16From8(scratch.arena, url_hostname_part);
    }

    //- rjf: convert method to verb wchar
    const WCHAR *verb = L"GET";
    switch (params->method) {
    default:
    case HTTP_Method_Get: {
      verb = L"GET";
    } break;
    case HTTP_Method_Head: {
      verb = L"HEAD";
    } break;
    case HTTP_Method_Post: {
      verb = L"POST";
    } break;
    case HTTP_Method_Put: {
      verb = L"PUT";
    } break;
    case HTTP_Method_Delete: {
      verb = L"DELETE";
    } break;
    case HTTP_Method_Connect: {
      verb = L"CONNECT";
    } break;
    case HTTP_Method_Options: {
      verb = L"OPTIONS";
    } break;
    case HTTP_Method_Trace: {
      verb = L"TRACE";
    } break;
    case HTTP_Method_Patch: {
      verb = L"PATCH";
    } break;
    }

    //- rjf: convert url parts to request parameters
    const WCHAR *path_name = L"";
    if (url_path_part.size != 0) {
      String16 url_path_part16 = Str16From8(scratch.arena, url_path_part);
      path_name = (WCHAR *)url_path_part16.str;
    }

    //- rjf: unpack body params
    String16 header16 = {0};
    {
      String8List header_strings = {0};
      if (params->user_agent.size != 0) {
        Str8ListPushF(scratch.arena, &header_strings, "User-Agent: %S\n",
                      params->user_agent);
      }
      if (params->authorization.size != 0) {
        Str8ListPushF(scratch.arena, &header_strings, "Authorization: %S\n",
                      params->authorization);
      }
      if (params->content_type.size != 0) {
        Str8ListPushF(scratch.arena, &header_strings, "Content-Type: %S\n",
                      params->content_type);
      }
      String8 header = Str8ListJoin(scratch.arena, &header_strings, 0);
      header16 = Str16From8(scratch.arena, header);
    }

    //- rjf: convert body/header info to winhttp expected params
    WCHAR *header = WINHTTP_NO_ADDITIONAL_HEADERS;
    U64 header_size = 0;
    void *optional = WINHTTP_NO_REQUEST_DATA;
    U64 optional_size = 0;
    if (header16.size != 0) {
      header = (WCHAR *)header16.str;
      header_size = header16.size;
    }
    if (body.size != 0) {
      optional = body.str;
      optional_size = body.size;
    }

    //- rjf: open connection
    HINTERNET hConnect = 0;
    if (http_w32_state->hSession != 0) {
      hConnect = WinHttpConnect(http_w32_state->hSession,
                                (WCHAR *)hostname16.str, port, 0);
      good = (hConnect != 0);
    }

    //- rjf: open request
    HINTERNET hRequest = 0;
    if (good) {
      hRequest =
          WinHttpOpenRequest(hConnect, verb, path_name, 0, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
      good = (hRequest != 0);
    }

    //- rjf: send request
    if (good) {
      good = !!WinHttpSendRequest(hRequest, header, header_size, optional,
                                  optional_size, optional_size, 0);
    }

    //- rjf: receive request
    if (good) {
      good = !!WinHttpReceiveResponse(hRequest, 0);
    }

    //- rjf: read response code
    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code,
                        &status_code_size, WINHTTP_NO_HEADER_INDEX);
    code = status_code;

    //- rjf: read response data
    for (; good;) {
      DWORD bytes_to_read = 0;
      good = WinHttpQueryDataAvailable(hRequest, &bytes_to_read);
      if (!good || bytes_to_read == 0) {
        break;
      }
      String8 data = PushStr8FillByte(scratch.arena, bytes_to_read, 0);
      DWORD bytes_read = 0;
      good = WinHttpReadData(hRequest, data.str, data.size, &bytes_read);
      if (good) {
        Str8ListPush(scratch.arena, &response_data_strings, data);
      }
    }
  }
  HTTP_Response response = {0};
  response.good = good;
  response.code = code;
  response.body = Str8ListJoin(arena, &response_data_strings, 0);
  ScratchEnd(scratch);
  return response;
}
