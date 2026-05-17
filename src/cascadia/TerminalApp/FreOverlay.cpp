// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "pch.h"
#include "FreOverlay.h"
#include "FreAgentEntry.g.cpp"
#include "FreOverlay.g.cpp"

#include "../inc/AgentRegistry.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;

namespace winrt::TerminalApp::implementation
{
    FreOverlay::FreOverlay()
    {
        InitializeComponent();
    }

    // ── Agent detection ─────────────────────────────────────────────────

    bool FreOverlay::_IsAgentInstalled(const wchar_t* name)
    {
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, name, L".exe", MAX_PATH, buf, nullptr) > 0)
            return true;
        const auto cmdName = std::wstring(name) + L".cmd";
        if (SearchPathW(nullptr, cmdName.c_str(), nullptr, MAX_PATH, buf, nullptr) > 0)
            return true;
        return false;
    }

    // ── Initialize ──────────────────────────────────────────────────────

    void FreOverlay::Initialize(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings)
    {
        _settings = settings;
        const auto& globals = _settings.GlobalSettings();
        namespace Reg = ::Microsoft::Terminal::Settings::Model::AgentRegistry;

        // Populate agent ComboBox: Copilot (always) + detected agents
        auto items = AgentComboBox().Items();
        items.Clear();
        int32_t selectedIndex = 0;
        int32_t idx = 0;
        const auto currentAgent = globals.AcpAgent();

        for (const auto& a : Reg::BuiltinAcpAgents)
        {
            const bool installed = _IsAgentInstalled(std::wstring{ a.id }.c_str());
            const bool isCopilot = (a.id == L"copilot");

            // Show Copilot always + detected agents only
            if (!isCopilot && !installed)
                continue;

            auto entry = winrt::make<FreAgentEntry>();
            entry.Id(winrt::hstring{ a.id });

            if (isCopilot && !installed)
            {
                entry.DisplayLabel(winrt::hstring{ std::wstring(a.displayName) + L" (will be installed)" });
            }
            else
            {
                entry.DisplayLabel(winrt::hstring{ std::wstring(a.displayName) + L" (installed)" });
            }

            items.Append(entry);

            if (a.id == currentAgent)
            {
                selectedIndex = idx;
            }
            idx++;
        }

        if (items.Size() > 0)
        {
            AgentComboBox().SelectedIndex(selectedIndex);
        }

        // Populate pane position ComboBox
        auto posItems = PanePositionComboBox().Items();
        posItems.Clear();
        posItems.Append(winrt::box_value(L"Bottom"));
        posItems.Append(winrt::box_value(L"Right"));
        posItems.Append(winrt::box_value(L"Left"));
        posItems.Append(winrt::box_value(L"Top"));

        const auto currentPos = globals.AgentPanePosition();
        if (currentPos == L"right") PanePositionComboBox().SelectedIndex(1);
        else if (currentPos == L"left") PanePositionComboBox().SelectedIndex(2);
        else if (currentPos == L"top") PanePositionComboBox().SelectedIndex(3);
        else PanePositionComboBox().SelectedIndex(0); // default: bottom

        // Set toggles from current settings
        AutoErrorToggle().IsOn(globals.AutoFixEnabled());
    }

    // ── Page navigation ─────────────────────────────────────────────────

    void FreOverlay::_OnNextButtonClick(const IInspectable& /*sender*/,
                                        const RoutedEventArgs& /*args*/)
    {
        WelcomePage().Visibility(Visibility::Collapsed);
        SettingsPage().Visibility(Visibility::Visible);
    }

    // ── Save & complete ─────────────────────────────────────────────────

    void FreOverlay::_OnSaveButtonClick(const IInspectable& /*sender*/,
                                        const RoutedEventArgs& /*args*/)
    {
        if (_settings)
        {
            const auto& globals = _settings.GlobalSettings();

            // Save agent selection
            if (const auto selected = AgentComboBox().SelectedItem())
            {
                if (const auto entry = selected.try_as<winrt::TerminalApp::FreAgentEntry>())
                {
                    globals.AcpAgent(entry.Id());
                }
            }

            // Save auto-error detection
            globals.AutoFixEnabled(AutoErrorToggle().IsOn());

            // Save pane position
            const auto posIdx = PanePositionComboBox().SelectedIndex();
            switch (posIdx)
            {
            case 1: globals.AgentPanePosition(L"right"); break;
            case 2: globals.AgentPanePosition(L"left"); break;
            case 3: globals.AgentPanePosition(L"top"); break;
            default: globals.AgentPanePosition(L"bottom"); break;
            }
        }

        Completed.raise(*this, nullptr);
    }

    void FreOverlay::_OnCloseButtonClick(const IInspectable& /*sender*/,
                                         const RoutedEventArgs& /*args*/)
    {
        Completed.raise(*this, nullptr);
    }

    // ── No-op: kept for IDL compatibility ───────────────────────────────

    void FreOverlay::ResetDragOffset()
    {
    }
}
