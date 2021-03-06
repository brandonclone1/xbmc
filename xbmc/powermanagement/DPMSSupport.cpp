/*
 *      Copyright (C) 2009-2013 Team XBMC
 *      http://kodi.tv
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"
#include "DPMSSupport.h"
#include "utils/log.h"
#include <assert.h>
#include <string>
#ifdef TARGET_WINDOWS
#include "guilib/GraphicContext.h"
#include "rendering/dx/RenderContext.h"
#endif

//////// Generic, non-platform-specific code

static const char* const MODE_NAMES[DPMSSupport::NUM_MODES] =
  { "STANDBY", "SUSPEND", "OFF" };

bool DPMSSupport::CheckValidMode(PowerSavingMode mode)
{
  if (mode < 0 || mode > NUM_MODES)
  {
    CLog::Log(LOGERROR, "Invalid power-saving mode %d", mode);
    return false;
  }
  return true;
}

const char* DPMSSupport::GetModeName(PowerSavingMode mode)
{
  if (!CheckValidMode(mode)) return NULL;
  return MODE_NAMES[mode];
}

DPMSSupport::DPMSSupport()
{
  PlatformSpecificInit();

  if (!m_supportedModes.empty())
  {
    std::string modes_message;
    for (size_t i = 0; i < m_supportedModes.size(); i++)
    {
      assert(CheckValidMode(m_supportedModes[i]));
      modes_message += " ";
      modes_message += MODE_NAMES[m_supportedModes[i]];
    }
    CLog::Log(LOGDEBUG, "DPMS: supported power-saving modes:%s",
              modes_message.c_str());
  }
}

bool DPMSSupport::IsModeSupported(PowerSavingMode mode) const
{
  if (!CheckValidMode(mode)) return false;
  for (size_t i = 0; i < m_supportedModes.size(); i++)
  {
    if (m_supportedModes[i] == mode) return true;
  }
  return false;
}

bool DPMSSupport::EnablePowerSaving(PowerSavingMode mode)
{
  if (!CheckValidMode(mode)) return false;
  if (!IsModeSupported(mode))
  {
    CLog::Log(LOGERROR, "DPMS: power-saving mode %s is not supported",
              MODE_NAMES[mode]);
    return false;
  }

  if (!PlatformSpecificEnablePowerSaving(mode)) return false;

  CLog::Log(LOGINFO, "DPMS: enabled power-saving mode %s",
            GetModeName(mode));
  return true;
}

bool DPMSSupport::DisablePowerSaving()
{
  if (!PlatformSpecificDisablePowerSaving()) return false;
  CLog::Log(LOGINFO, "DPMS: disabled power-saving");
  return true;
}

///////// Platform-specific support

#if defined(HAVE_X11)
#include "ServiceBroker.h"
#include "windowing/X11/WinSystemX11.h"
//// X Windows

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

// Mapping of PowerSavingMode to X11's mode constants.
static const CARD16 X_DPMS_MODES[] =
  { DPMSModeStandby, DPMSModeSuspend, DPMSModeOff };

void DPMSSupport::PlatformSpecificInit()
{
  CWinSystemX11 &winSystem = dynamic_cast<CWinSystemX11&>(CServiceBroker::GetWinSystem());
  Display* dpy = winSystem.GetDisplay();
  if (dpy == NULL)
    return;

  int event_base, error_base;   // we ignore these
  if (!DPMSQueryExtension(dpy, &event_base, &error_base)) {
    CLog::Log(LOGINFO, "DPMS: X11 extension not present, power-saving"
              " will not be available");
    return;
  }

  if (!DPMSCapable(dpy)) {
    CLog::Log(LOGINFO, "DPMS: display does not support power-saving");
    return;
  }

  m_supportedModes.push_back(SUSPEND); // best compromise
  m_supportedModes.push_back(OFF);     // next best
  m_supportedModes.push_back(STANDBY); // rather lame, < 80% power according to
                                       // DPMS spec
}

bool DPMSSupport::PlatformSpecificEnablePowerSaving(PowerSavingMode mode)
{
  CWinSystemX11 &winSystem = dynamic_cast<CWinSystemX11&>(CServiceBroker::GetWinSystem());
  Display* dpy = winSystem.GetDisplay();
  if (dpy == NULL)
    return false;

  // This is not needed on my ATI Radeon, but the docs say that DPMSForceLevel
  // after a DPMSDisable (from SDL) should not normally work.
  DPMSEnable(dpy);
  DPMSForceLevel(dpy, X_DPMS_MODES[mode]);
  // There shouldn't be any errors if we called DPMSEnable; if they do happen,
  // they're asynchronous and messy to detect.
  XFlush(dpy);
  return true;
}

bool DPMSSupport::PlatformSpecificDisablePowerSaving()
{
  CWinSystemX11 &winSystem = dynamic_cast<CWinSystemX11&>(CServiceBroker::GetWinSystem());
  Display* dpy = winSystem.GetDisplay();
  if (dpy == NULL)
    return false;

  DPMSForceLevel(dpy, DPMSModeOn);
  DPMSDisable(dpy);
  XFlush(dpy);

  winSystem.RecreateWindow();

  return true;
}

/////  Add other platforms here.
#elif defined(TARGET_WINDOWS)
void DPMSSupport::PlatformSpecificInit()
{
#ifdef TARGET_WINDOWS_DESKTOP
  // Assume we support DPMS. Is there a way to test it?
  m_supportedModes.push_back(OFF);
  m_supportedModes.push_back(STANDBY);
#else
  CLog::Log(LOGINFO, "DPMS: not supported on this platform");
#endif
}

bool DPMSSupport::PlatformSpecificEnablePowerSaving(PowerSavingMode mode)
{
  if(!g_graphicsContext.IsFullScreenRoot())
  {
    CLog::Log(LOGDEBUG, "DPMS: not in fullscreen, power saving disabled");
    return false;
  }
  switch(mode)
  {
#ifdef TARGET_WINDOWS_DESKTOP
  case OFF:
    // Turn off display
    return SendMessage(DX::Windowing().GetHwnd(), WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM) 2) == 0;
  case STANDBY:
    // Set display to low power
    return SendMessage(DX::Windowing().GetHwnd(), WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM) 1) == 0;
#endif
  default:
    return true;
  }
}

bool DPMSSupport::PlatformSpecificDisablePowerSaving()
{
#ifdef TARGET_WINDOWS_STORE
  return false;
#else
  // Turn display on
  return SendMessage(DX::Windowing().GetHwnd(), WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM) -1) == 0;
#endif
}

#elif defined(TARGET_DARWIN_OSX)
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CFNumber.h>

void DPMSSupport::PlatformSpecificInit()
{
  m_supportedModes.push_back(OFF);
  m_supportedModes.push_back(STANDBY);
}

bool DPMSSupport::PlatformSpecificEnablePowerSaving(PowerSavingMode mode)
{
  bool status;
  // http://lists.apple.com/archives/Cocoa-dev/2007/Nov/msg00267.html
  // This is an unsupported system call that might kernel panic on PPC boxes
  // The reported OSX-PPC panic is unverified so we are going to enable this until
  // we find out which OSX-PPC boxes have problems, then add detect/disable for those boxes.
  io_registry_entry_t r = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/IOResources/IODisplayWrangler");
  if(!r) return false;

  switch(mode)
  {
  case OFF:
    // Turn off display
    status = (IORegistryEntrySetCFProperty(r, CFSTR("IORequestIdle"), kCFBooleanTrue) == 0);
    break;
  case STANDBY:
    // Set display to low power
    status = (IORegistryEntrySetCFProperty(r, CFSTR("IORequestIdle"), kCFBooleanTrue) == 0);
    break;
  default:
    status = false;
    break;
  }
  return status;
}

bool DPMSSupport::PlatformSpecificDisablePowerSaving()
{
  // http://lists.apple.com/archives/Cocoa-dev/2007/Nov/msg00267.html
  // This is an unsupported system call that might kernel panic on PPC boxes
  // The reported OSX-PPC panic is unverified so we are going to enable this until
  // we find out which OSX-PPC boxes have problems, then add detect/disable for those boxes.
  io_registry_entry_t r = IORegistryEntryFromPath(kIOMasterPortDefault, "IOService:/IOResources/IODisplayWrangler");
  if(!r) return false;

  // Turn display on
  return (IORegistryEntrySetCFProperty(r, CFSTR("IORequestIdle"), kCFBooleanFalse) == 0);
}

#else
// Not implemented on this platform
void DPMSSupport::PlatformSpecificInit()
{
  CLog::Log(LOGINFO, "DPMS: not supported on this platform");
}

bool DPMSSupport::PlatformSpecificEnablePowerSaving(PowerSavingMode mode)
{
  return false;
}

bool DPMSSupport::PlatformSpecificDisablePowerSaving()
{
  return false;
}

#endif  // platform ifdefs


