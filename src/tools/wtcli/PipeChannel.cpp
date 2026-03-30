#include "PipeChannel.h"

#include <json/json.h>
#include <fmt/format.h>

// Stub implementation — pipe channel is the JSON fallback.
// TODO: Full implementation for pipe transport parity with Rust.

PipeChannel::~PipeChannel()
{
    if (_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_pipe);
    }
}

std::unique_ptr<PipeChannel> PipeChannel::Connect(const std::wstring& pipeName, const std::wstring& token)
{
    HANDLE pipe = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "[wtcli] Failed to connect to pipe: %ls (error %lu)\n", pipeName.c_str(), GetLastError());
        return nullptr;
    }

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    auto channel = std::make_unique<PipeChannel>();
    channel->_pipe = pipe;

    // Authenticate
    auto tokenUtf8 = WideToUtf8(token);
    std::string resultJson;
    auto hr = channel->_request("authenticate", fmt::format(R"({{"token":"{}"}})", tokenUtf8), resultJson);
    if (FAILED(hr))
    {
        fprintf(stderr, "[wtcli] Pipe authentication failed\n");
        return nullptr;
    }

    return channel;
}

HRESULT PipeChannel::_request(const std::string& method, const std::string& paramsJson, std::string& resultJson)
{
    auto id = std::to_string(_nextId++);
    auto request = fmt::format(R"({{"type":"request","id":"{}","method":"{}","params":{}}})", id, method, paramsJson) + "\n";

    DWORD written = 0;
    if (!WriteFile(_pipe, request.c_str(), static_cast<DWORD>(request.size()), &written, nullptr))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // Read response line (byte by byte until \n)
    std::string response;
    char ch;
    DWORD bytesRead;
    while (ReadFile(_pipe, &ch, 1, &bytesRead, nullptr) && bytesRead == 1)
    {
        if (ch == '\n')
            break;
        response.push_back(ch);
    }

    // Parse response
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(response);
    if (!Json::parseFromStream(rb, ss, &root, &errs))
    {
        return E_FAIL;
    }

    if (!root["error"].isNull())
    {
        auto code = root["error"]["code"].asString();
        auto msg = root["error"]["message"].asString();
        fprintf(stderr, "[wtcli] Protocol error [%s]: %s\n", code.c_str(), msg.c_str());
        return E_FAIL;
    }

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    resultJson = Json::writeString(wb, root["result"]);
    return S_OK;
}

// Helper: make a pipe request and parse the result JSON
static HRESULT PipeRequest(PipeChannel* ch, const std::string& method, const std::string& params, Json::Value& result)
{
    std::string resultJson;
    HRESULT hr = ch->_request(method, params, resultJson);
    if (FAILED(hr))
        return hr;

    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(resultJson);
    if (!Json::parseFromStream(rb, ss, &result, &errs))
        return E_FAIL;
    return S_OK;
}

// Helper: extract a string from JSON, return SysAllocString
static BSTR JsonToBstr(const Json::Value& v, const char* key)
{
    auto s = v.get(key, "").asString();
    if (s.empty())
        return nullptr;
    auto wide = Utf8ToWide(s);
    return SysAllocString(wide.c_str());
}

// ── Typed method implementations (JSON → MIDL struct conversion) ──

HRESULT PipeChannel::Authenticate(const std::wstring& token, bool& authenticated, std::wstring& protocolVersion)
{
    auto tokenUtf8 = WideToUtf8(token);
    std::string resultJson;
    HRESULT hr = _request("authenticate", fmt::format(R"({{"token":"{}"}})", tokenUtf8), resultJson);
    if (FAILED(hr))
        return hr;

    Json::Value result;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::istringstream ss(resultJson);
    if (!Json::parseFromStream(rb, ss, &result, &errs))
        return E_FAIL;

    authenticated = result.get("authenticated", false).asBool();
    protocolVersion = Utf8ToWide(result.get("protocol_version", "").asString());
    return S_OK;
}

HRESULT PipeChannel::GetCapabilities(std::wstring& protocolVersion, std::wstring& supportedMethodsJson)
{
    Json::Value result;
    HRESULT hr = PipeRequest(this, "get_capabilities", "{}", result);
    if (FAILED(hr))
        return hr;

    protocolVersion = Utf8ToWide(result.get("protocol_version", "").asString());
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    supportedMethodsJson = Utf8ToWide(Json::writeString(wb, result["methods"]));
    return S_OK;
}

HRESULT PipeChannel::GetActivePane(PROTOCOL_PANE_INFO& r)
{
    Json::Value result;
    HRESULT hr = PipeRequest(this, "get_active_pane", "{}", result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.PaneId = JsonToBstr(result, "pane_id");
    r.TabId = JsonToBstr(result, "tab_id");
    r.WindowId = JsonToBstr(result, "window_id");
    r.Title = JsonToBstr(result, "title");
    r.Profile = JsonToBstr(result, "profile");
    r.IsActive = TRUE;
    r.Pid = result.get("pid", 0u).asUInt();
    return S_OK;
}

HRESULT PipeChannel::ListWindows(std::vector<PROTOCOL_WINDOW_INFO>& results)
{
    Json::Value result;
    HRESULT hr = PipeRequest(this, "list_windows", "{}", result);
    if (FAILED(hr))
        return hr;

    for (const auto& w : result.get("windows", Json::arrayValue))
    {
        PROTOCOL_WINDOW_INFO info{};
        info.WindowId = JsonToBstr(w, "window_id");
        info.Title = JsonToBstr(w, "title");
        info.IsFocused = w.get("is_focused", false).asBool() ? TRUE : FALSE;
        info.TabCount = w.get("tab_count", 0u).asUInt();
        results.push_back(info);
    }
    return S_OK;
}

HRESULT PipeChannel::ListTabs(const std::wstring& windowIdFilter, std::vector<PROTOCOL_TAB_INFO>& results)
{
    auto filter = WideToUtf8(windowIdFilter);
    auto params = filter.empty() ? std::string("{}") : fmt::format(R"({{"window_id":"{}"}})", filter);

    Json::Value result;
    HRESULT hr = PipeRequest(this, "list_tabs", params, result);
    if (FAILED(hr))
        return hr;

    for (const auto& t : result.get("tabs", Json::arrayValue))
    {
        PROTOCOL_TAB_INFO info{};
        info.TabId = JsonToBstr(t, "tab_id");
        info.WindowId = JsonToBstr(t, "window_id");
        info.Title = JsonToBstr(t, "title");
        info.IsActive = t.get("is_active", false).asBool() ? TRUE : FALSE;
        info.PaneCount = t.get("pane_count", 0u).asUInt();
        results.push_back(info);
    }
    return S_OK;
}

HRESULT PipeChannel::ListPanes(const std::wstring& windowIdFilter, const std::wstring& tabIdFilter, std::vector<PROTOCOL_PANE_INFO>& results)
{
    auto wf = WideToUtf8(windowIdFilter);
    auto tf = WideToUtf8(tabIdFilter);
    std::string params = "{}";
    if (!tf.empty())
        params = fmt::format(R"({{"tab_id":"{}"}})", tf);
    else if (!wf.empty())
        params = fmt::format(R"({{"window_id":"{}"}})", wf);

    Json::Value result;
    HRESULT hr = PipeRequest(this, "list_panes", params, result);
    if (FAILED(hr))
        return hr;

    for (const auto& p : result.get("panes", Json::arrayValue))
    {
        PROTOCOL_PANE_INFO info{};
        info.PaneId = JsonToBstr(p, "pane_id");
        info.TabId = JsonToBstr(p, "tab_id");
        info.WindowId = JsonToBstr(p, "window_id");
        info.Title = JsonToBstr(p, "title");
        info.Profile = JsonToBstr(p, "profile");
        info.IsActive = p.get("is_active", false).asBool() ? TRUE : FALSE;
        info.Pid = p.get("pid", 0u).asUInt();
        info.Rows = p.isMember("size") ? p["size"].get("rows", 0).asInt() : 0;
        info.Columns = p.isMember("size") ? p["size"].get("columns", 0).asInt() : 0;
        results.push_back(info);
    }
    return S_OK;
}

HRESULT PipeChannel::ReadPaneOutput(const std::wstring& paneId, const std::wstring& source, int maxLines, PROTOCOL_PANE_OUTPUT& r)
{
    auto pid = WideToUtf8(paneId);
    auto src = WideToUtf8(source);
    auto params = fmt::format(R"({{"pane_id":"{}","source":"{}","max_lines":{}}})", pid, src, maxLines);

    Json::Value result;
    HRESULT hr = PipeRequest(this, "read_pane_output", params, result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.PaneId = JsonToBstr(result, "pane_id");
    r.Content = JsonToBstr(result, "content");
    r.LineCount = result.get("line_count", 0).asInt();
    r.Truncated = result.get("truncated", false).asBool() ? TRUE : FALSE;
    return S_OK;
}

HRESULT PipeChannel::GetProcessStatus(const std::wstring& paneId, PROTOCOL_PROCESS_STATUS& r)
{
    auto pid = WideToUtf8(paneId);
    Json::Value result;
    HRESULT hr = PipeRequest(this, "get_process_status", fmt::format(R"({{"pane_id":"{}"}})", pid), result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.PaneId = JsonToBstr(result, "pane_id");
    r.State = JsonToBstr(result, "state");
    r.Pid = result.get("pid", 0u).asUInt();
    r.ExitCode = result.get("exit_code", 0).asInt();
    r.HasExitCode = result.isMember("exit_code") && !result["exit_code"].isNull() ? TRUE : FALSE;
    return S_OK;
}

HRESULT PipeChannel::GetSessionVariable(const std::wstring& paneId, const std::wstring& name, PROTOCOL_SESSION_VARIABLE& r)
{
    auto pid = WideToUtf8(paneId);
    auto n = WideToUtf8(name);
    Json::Value result;
    HRESULT hr = PipeRequest(this, "get_session_variable", fmt::format(R"({{"pane_id":"{}","name":"{}"}})", pid, n), result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.PaneId = JsonToBstr(result, "pane_id");
    r.Name = JsonToBstr(result, "name");
    r.Value = JsonToBstr(result, "value");
    r.Exists = result.get("exists", false).asBool() ? TRUE : FALSE;
    return S_OK;
}

HRESULT PipeChannel::GetSettings(std::wstring& settingsJson)
{
    Json::Value result;
    HRESULT hr = PipeRequest(this, "get_settings", "{}", result);
    if (FAILED(hr))
        return hr;
    settingsJson = Utf8ToWide(result.get("settings", "").asString());
    return S_OK;
}

HRESULT PipeChannel::CreateTab(const std::wstring& windowId, const std::wstring& profile,
                                const std::wstring& commandline, const std::wstring& title,
                                bool suppressAppTitle, bool injectMcpCredentials, bool background,
                                PROTOCOL_TAB_CREATION_RESULT& r)
{
    Json::Value params;
    if (!windowId.empty()) params["window_id"] = WideToUtf8(windowId);
    if (!profile.empty()) params["profile"] = WideToUtf8(profile);
    if (!commandline.empty()) params["commandline"] = WideToUtf8(commandline);
    if (!title.empty()) params["title"] = WideToUtf8(title);
    params["suppress_application_title"] = suppressAppTitle;
    params["inject_mcp_credentials"] = injectMcpCredentials;
    params["background"] = background;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    Json::Value result;
    HRESULT hr = PipeRequest(this, "create_tab", Json::writeString(wb, params), result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.TabId = JsonToBstr(result, "tab_id");
    r.PaneId = JsonToBstr(result, "pane_id");
    r.WindowId = JsonToBstr(result, "window_id");
    r.Pid = result.get("pid", 0u).asUInt();
    return S_OK;
}

HRESULT PipeChannel::SplitPane(const std::wstring& paneId, const std::wstring& direction, float size,
                                const std::wstring& profile, const std::wstring& commandline,
                                bool injectMcpCredentials, bool background,
                                PROTOCOL_TAB_CREATION_RESULT& r)
{
    Json::Value params;
    if (!paneId.empty()) params["pane_id"] = WideToUtf8(paneId);
    if (!direction.empty()) params["direction"] = WideToUtf8(direction);
    params["size"] = size;
    if (!profile.empty()) params["profile"] = WideToUtf8(profile);
    if (!commandline.empty()) params["commandline"] = WideToUtf8(commandline);
    params["inject_mcp_credentials"] = injectMcpCredentials;
    params["background"] = background;

    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    Json::Value result;
    HRESULT hr = PipeRequest(this, "split_pane", Json::writeString(wb, params), result);
    if (FAILED(hr))
        return hr;

    memset(&r, 0, sizeof(r));
    r.TabId = JsonToBstr(result, "tab_id");
    r.PaneId = JsonToBstr(result, "pane_id");
    r.WindowId = JsonToBstr(result, "window_id");
    r.Pid = result.get("pid", 0u).asUInt();
    return S_OK;
}

HRESULT PipeChannel::ClosePane(const std::wstring& paneId)
{
    auto pid = WideToUtf8(paneId);
    Json::Value result;
    return PipeRequest(this, "close_pane", fmt::format(R"({{"pane_id":"{}"}})", pid), result);
}

HRESULT PipeChannel::SendInput(const std::wstring& paneId, const std::wstring& text)
{
    auto pid = WideToUtf8(paneId);
    auto t = WideToUtf8(text);
    Json::Value result;
    return PipeRequest(this, "send_input", fmt::format(R"({{"pane_id":"{}","text":"{}"}})", pid, t), result);
}

HRESULT PipeChannel::SetSessionVariable(const std::wstring& paneId, const std::wstring& name, const std::wstring& value)
{
    auto pid = WideToUtf8(paneId);
    auto n = WideToUtf8(name);
    auto v = WideToUtf8(value);
    Json::Value result;
    return PipeRequest(this, "set_session_variable", fmt::format(R"({{"pane_id":"{}","name":"{}","value":"{}"}})", pid, n, v), result);
}

HRESULT PipeChannel::SetSettings(const std::wstring& settingsContent, std::wstring& backupPath)
{
    // Settings content may contain quotes/special chars — use jsoncpp for proper escaping.
    Json::Value params;
    params["settings"] = WideToUtf8(settingsContent);
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";

    Json::Value result;
    HRESULT hr = PipeRequest(this, "set_settings", Json::writeString(wb, params), result);
    if (FAILED(hr))
        return hr;

    backupPath = Utf8ToWide(result.get("backup_path", "").asString());
    return S_OK;
}
