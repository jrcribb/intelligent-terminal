// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "FreAgentEntry.g.h"
#include "FreOverlay.g.h"

namespace winrt::TerminalApp::implementation
{
    struct FreAgentEntry : FreAgentEntryT<FreAgentEntry>
    {
        FreAgentEntry() = default;

        winrt::hstring Id() const { return _id; }
        void Id(const winrt::hstring& value) { _id = value; }
        winrt::hstring DisplayLabel() const { return _displayLabel; }
        void DisplayLabel(const winrt::hstring& value) { _displayLabel = value; }

    private:
        winrt::hstring _id;
        winrt::hstring _displayLabel;
    };

    struct FreOverlay : FreOverlayT<FreOverlay>
    {
        FreOverlay();

        // Initialize with settings to populate controls.
        void Initialize(const winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings& settings);

        // Event — sender must be the WinRT projected type.
        til::typed_event<winrt::TerminalApp::FreOverlay, winrt::Windows::Foundation::IInspectable> Completed;

        // XAML event handlers — must be public for generated code access.
        void _OnNextButtonClick(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& args);
        void _OnSaveButtonClick(const winrt::Windows::Foundation::IInspectable& sender,
                                const winrt::Windows::UI::Xaml::RoutedEventArgs& args);
        void _OnCloseButtonClick(const winrt::Windows::Foundation::IInspectable& sender,
                                 const winrt::Windows::UI::Xaml::RoutedEventArgs& args);

        // No-op kept for IDL compatibility.
        void ResetDragOffset();

    private:
        winrt::Microsoft::Terminal::Settings::Model::CascadiaSettings _settings{ nullptr };

        // Detect whether an agent CLI is on PATH.
        static bool _IsAgentInstalled(const wchar_t* name);
    };
}

namespace winrt::TerminalApp::factory_implementation
{
    BASIC_FACTORY(FreAgentEntry);
    BASIC_FACTORY(FreOverlay);
}
