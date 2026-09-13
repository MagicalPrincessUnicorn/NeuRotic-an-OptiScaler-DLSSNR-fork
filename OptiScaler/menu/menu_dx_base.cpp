#include "pch.h"

#include <Config.h>
#include <Logger.h>
#include <resource.h>
#include <Util.h>
#include <dlssnr/DlssNrFeature_Dx12.h>

#include "menu_common.h"
#include "menu_dx_base.h"

#include <imgui/imgui_impl_win32.h>

bool MenuDxBase::RenderMenu()
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return false;

    if (MenuCommon::RenderMenu())
    {
        MenuCommon::FinalizeFrame();
        // This optional path bakes the menu into the upscaled image. Process its
        // button/keybind normally, but omit the draw while a screenshot is pending.
        return !DlssNr::ComparisonScreenshotBusy();
    }

    return false;
}

bool MenuDxBase::IsHandleDifferent()
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return false;

    HWND frontWindow = Util::GetProcessWindow();

    if (frontWindow != nullptr && frontWindow == _handle)
        return false;

    _handle = frontWindow;

    return true;
}

void MenuDxBase::Dx11Ready() { MenuCommon::Dx11Inited(); }

void MenuDxBase::Dx12Ready() { MenuCommon::Dx12Inited(); }

MenuDxBase::MenuDxBase(HWND handle) : _handle(handle)
{
    if (Config::Instance()->OverlayMenu.value_or_default())
        return;

    MenuCommon::Init(handle, false);

    _baseInit = MenuCommon::IsInited();
}

MenuDxBase::~MenuDxBase()
{
    if (!_baseInit)
        return;

    MenuCommon::Shutdown();
}

bool MenuDxBase::IsVisible() { return MenuCommon::IsVisible(); }
