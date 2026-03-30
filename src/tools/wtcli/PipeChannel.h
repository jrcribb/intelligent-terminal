#pragma once

#include "Channel.h"
#include <Windows.h>

// JSON pipe fallback channel. Connects to \\.\pipe\WindowsTerminal-{pid}
// and uses the same newline-delimited JSON wire protocol as the Rust wta.
class PipeChannel : public Channel
{
public:
    static std::unique_ptr<PipeChannel> Connect(const std::wstring& pipeName, const std::wstring& token);

    ~PipeChannel() override;

    HRESULT Authenticate(const std::wstring& token, bool& authenticated, std::wstring& protocolVersion) override;
    HRESULT GetCapabilities(std::wstring& protocolVersion, std::wstring& supportedMethodsJson) override;

    HRESULT GetActivePane(PROTOCOL_PANE_INFO& result) override;
    HRESULT ListWindows(std::vector<PROTOCOL_WINDOW_INFO>& results) override;
    HRESULT ListTabs(const std::wstring& windowIdFilter, std::vector<PROTOCOL_TAB_INFO>& results) override;
    HRESULT ListPanes(const std::wstring& windowIdFilter, const std::wstring& tabIdFilter, std::vector<PROTOCOL_PANE_INFO>& results) override;
    HRESULT ReadPaneOutput(const std::wstring& paneId, const std::wstring& source, int maxLines, PROTOCOL_PANE_OUTPUT& result) override;
    HRESULT GetProcessStatus(const std::wstring& paneId, PROTOCOL_PROCESS_STATUS& result) override;
    HRESULT GetSessionVariable(const std::wstring& paneId, const std::wstring& name, PROTOCOL_SESSION_VARIABLE& result) override;
    HRESULT GetSettings(std::wstring& settingsJson) override;

    HRESULT CreateTab(const std::wstring& windowId, const std::wstring& profile,
                      const std::wstring& commandline, const std::wstring& title,
                      bool suppressAppTitle, bool injectMcpCredentials, bool background,
                      PROTOCOL_TAB_CREATION_RESULT& result) override;
    HRESULT SplitPane(const std::wstring& paneId, const std::wstring& direction, float size,
                      const std::wstring& profile, const std::wstring& commandline,
                      bool injectMcpCredentials, bool background,
                      PROTOCOL_TAB_CREATION_RESULT& result) override;
    HRESULT ClosePane(const std::wstring& paneId) override;
    HRESULT SendInput(const std::wstring& paneId, const std::wstring& text) override;
    HRESULT SetSessionVariable(const std::wstring& paneId, const std::wstring& name, const std::wstring& value) override;
    HRESULT SetSettings(const std::wstring& settingsContent, std::wstring& backupPath) override;

    // Send a JSON request and read the JSON response.
    HRESULT _request(const std::string& method, const std::string& paramsJson, std::string& resultJson);

private:
    HANDLE _pipe = INVALID_HANDLE_VALUE;
    int _nextId = 1;
};
