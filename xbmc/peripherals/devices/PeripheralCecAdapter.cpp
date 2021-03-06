/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://xbmc.org
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
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "PeripheralCecAdapter.h"
#include "dialogs/GUIDialogOK.h"
#include "input/XBIRRemote.h"
#include "Application.h"
#include "threads/SingleLock.h"
#include "dialogs/GUIDialogKaiToast.h"
#include "guilib/LocalizeStrings.h"
#include "peripherals/Peripherals.h"
#include "peripherals/bus/PeripheralBus.h"
#include "utils/log.h"

using namespace PERIPHERALS;
using namespace ANNOUNCEMENT;
using namespace CEC;

#define CEC_LIB_SUPPORTED_VERSION 2

CPeripheralCecAdapter::CPeripheralCecAdapter(const PeripheralType type, const PeripheralBusType busType, const CStdString &strLocation, const CStdString &strDeviceName, int iVendorId, int iProductId) :
  CPeripheralHID(type, busType, strLocation, strDeviceName, iVendorId, iProductId),
  CThread("CEC parser"),
  m_bStarted(false),
  m_bHasButton(false),
  m_bIsReady(false)
{
  m_cecParser = LoadLibCec("XBMC", CECDEVICE_PLAYBACKDEVICE1);
  if (!m_cecParser || m_cecParser->GetMinVersion() > CEC_LIB_SUPPORTED_VERSION)
  {
    /* unsupported libcec version */
    CLog::Log(LOGERROR, g_localizeStrings.Get(36013).c_str(), CEC_LIB_SUPPORTED_VERSION, m_cecParser ? m_cecParser->GetMinVersion() : -1);

    CStdString strMessage;
    strMessage.Format(g_localizeStrings.Get(36013).c_str(), CEC_LIB_SUPPORTED_VERSION, m_cecParser ? m_cecParser->GetMinVersion() : -1);
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(36000), strMessage);
    m_bError = true;
    UnloadLibCec(m_cecParser);
    m_cecParser = NULL;
  }
  else
  {
    m_features.push_back(FEATURE_CEC);
  }
}

CPeripheralCecAdapter::~CPeripheralCecAdapter(void)
{
  CAnnouncementManager::RemoveAnnouncer(this);
  if (m_cecParser)
  {
    FlushLog();
    m_cecParser->Close(1000);
    UnloadLibCec(m_cecParser);
  }
  StopThread(true);
}

void CPeripheralCecAdapter::Announce(EAnnouncementFlag flag, const char *sender, const char *message, const CVariant &data)
{
  if (flag == System && !strcmp(sender, "xbmc") && !strcmp(message, "ApplicationStop") && m_bIsReady)
  {
    if (GetSettingBool("cec_power_off_shutdown"))
      m_cecParser->PowerOffDevices();
    else if (GetSettingBool("cec_mark_inactive_shutdown"))
      m_cecParser->SetInactiveView();
  }
  else if (flag == GUI && !strcmp(sender, "xbmc") && !strcmp(message, "OnScreensaverDeactivated") && GetSettingBool("cec_standby_screensaver") && m_bIsReady)
  {
    m_cecParser->PowerOnDevices();
  }
  else if (flag == GUI && !strcmp(sender, "xbmc") && !strcmp(message, "OnScreensaverActivated") && GetSettingBool("cec_standby_screensaver"))
  {
    m_cecParser->StandbyDevices();
  }
  else if (flag == System && !strcmp(sender, "xbmc") && !strcmp(message, "OnSleep"))
  {
    if (GetSettingBool("cec_power_off_shutdown") && m_bIsReady)
      m_cecParser->PowerOffDevices();

    m_bStop = true;
    m_cecParser->Close(500);
    WaitForThreadExit(2000);
    CLog::Log(LOGDEBUG, "%s - closing the connection to the CEC adapter while in standby mode", __FUNCTION__);
  }
  else if (flag == System && !strcmp(sender, "xbmc") && !strcmp(message, "OnWake"))
  {
    CLog::Log(LOGDEBUG, "%s - reconnecting to the CEC adapter after standby mode", __FUNCTION__);
    m_bStop = false;
    Create();
  }
}

bool CPeripheralCecAdapter::InitialiseFeature(const PeripheralFeature feature)
{
  if (feature == FEATURE_CEC && !m_bStarted)
  {
    m_bStarted = true;
    Create();
  }

  return CPeripheral::InitialiseFeature(feature);
}

void CPeripheralCecAdapter::Process(void)
{
  if (!GetSettingBool("enabled"))
  {
    m_bStarted = false;
    return;
  }

  // the device needs a second to settle after it's plugged in
  Sleep(CEC_SETTLE_DOWN_TIME);


  CStdString strPort = GetSettingString("port");
  if (strPort.IsEmpty())
  {
    std::vector<cec_device> deviceList;
    strPort = m_strFileLocation;
    TranslateComPort(strPort);
    int iFound = m_cecParser->FindDevices(deviceList, strPort);

    if (iFound <= 0)
    {
      CLog::Log(LOGWARNING, "%s - no CEC adapters found on %s", __FUNCTION__, strPort.c_str());
      CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(36000), g_localizeStrings.Get(36011));
      m_bStarted = false;
      return;
    }
    else if (iFound > 0)
    {
      cec_device dev = deviceList[0];
      if (iFound > 1)
        CLog::Log(LOGDEBUG, "%s - multiple com ports found for device. taking the first one", __FUNCTION__);
      else
        CLog::Log(LOGDEBUG, "%s - autodetect com port '%s'", __FUNCTION__, dev.comm.c_str());
      if (!strPort.Equals(dev.comm.c_str()))
      {
        strPort = dev.comm.c_str();
        SetSetting("port", strPort);
      }
    }
  }

  // open the CEC adapter
  CLog::Log(LOGDEBUG, "%s - opening CEC adapter: %s", __FUNCTION__, strPort.c_str());

  if (!m_cecParser->Open(strPort.c_str(), 10000))
  {
    FlushLog();
    CGUIDialogOK::ShowAndGetInput(257, 0, 36006, 0);
    CGUIDialogKaiToast::QueueNotification(CGUIDialogKaiToast::Error, g_localizeStrings.Get(36000), g_localizeStrings.Get(36012));
    m_bStarted = false;
    return;
  }

  CLog::Log(LOGDEBUG, "%s - CEC adapter opened", __FUNCTION__);
  m_bIsReady = true;
  CAnnouncementManager::AddAnnouncer(this);

  if (GetSettingBool("cec_power_on_startup"))
    PowerOnCecDevices();
  m_cecParser->SetActiveView();
  FlushLog();

  while (!m_bStop)
  {
    FlushLog();
    ProcessNextCommand();
    Sleep(50);
  }

  m_bStarted = false;
}

bool CPeripheralCecAdapter::PowerOffCecDevices(void)
{
  bool bReturn(false);

  if (m_cecParser && m_bIsReady)
  {
    CLog::Log(LOGDEBUG, "%s - powering off CEC capable devices", __FUNCTION__);
    bReturn = m_cecParser->PowerOffDevices();
  }

  return bReturn;
}

bool CPeripheralCecAdapter::PowerOnCecDevices(void)
{
  bool bReturn(false);

  if (m_cecParser && m_bIsReady)
  {
    CLog::Log(LOGDEBUG, "%s - powering on CEC capable devices", __FUNCTION__);
    bReturn = m_cecParser->PowerOnDevices();
  }

  return bReturn;
}

bool CPeripheralCecAdapter::StandbyCecDevices(void)
{
  bool bReturn(false);

  if (m_cecParser && m_bIsReady)
  {
    CLog::Log(LOGDEBUG, "%s - putting CEC capable devices in standby mode", __FUNCTION__);
    bReturn = m_cecParser->StandbyDevices();
  }

  return bReturn;
}

bool CPeripheralCecAdapter::SendPing(void)
{
  bool bReturn(false);
  if (m_cecParser && m_bIsReady)
  {
    CLog::Log(LOGDEBUG, "%s - sending ping to the CEC adapter", __FUNCTION__);
    bReturn = m_cecParser->Ping();
  }

  return bReturn;
}

bool CPeripheralCecAdapter::StartBootloader(void)
{
  bool bReturn(false);
  if (m_cecParser && m_bIsReady)
  {
    CLog::Log(LOGDEBUG, "%s - starting the bootloader", __FUNCTION__);
    bReturn = m_cecParser->StartBootloader();
  }

  return bReturn;
}

void CPeripheralCecAdapter::ProcessNextCommand(void)
{
  cec_command command;
  if (m_cecParser && m_bIsReady && m_cecParser->GetNextCommand(&command))
  {
    CLog::Log(LOGDEBUG, "%s - processing command: initiator=%d destination=%d opcode=%d", __FUNCTION__, command.source, command.destination, command.opcode);

    switch (command.opcode)
    {
    case CEC_OPCODE_STANDBY:
      /* a device was put in standby mode */
      CLog::Log(LOGDEBUG, "%s - device %d was put in standby mode", __FUNCTION__, command.source);
      if (command.source == CECDEVICE_TV && GetSettingBool("standby_pc_on_tv_standby"))
      {
        m_bStarted = false;
        g_application.getApplicationMessenger().Suspend();
      }
      break;
    default:
      break;
    }
  }
}

bool CPeripheralCecAdapter::GetNextKey(void)
{
  CSingleLock lock(m_critSection);
  if (m_bHasButton)
    return false;

  cec_keypress key;
  if (!m_bIsReady || !(m_bHasButton = m_cecParser->GetNextKeypress(&key)))
    return false;

  CLog::Log(LOGDEBUG, "%s - received key %d", __FUNCTION__, key.keycode);
  m_button.iButtonReleased = XbmcThreads::SystemClockMillis();
  m_button.iButton        = 0;
  m_button.iButtonPressed = m_button.iButtonReleased - key.duration;

  switch (key.keycode)
  {
  case CEC_USER_CONTROL_CODE_SELECT:
    m_button.iButton = XINPUT_IR_REMOTE_SELECT;
    break;
  case CEC_USER_CONTROL_CODE_UP:
    m_button.iButton = XINPUT_IR_REMOTE_UP;
    break;
  case CEC_USER_CONTROL_CODE_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_DOWN;
    break;
  case CEC_USER_CONTROL_CODE_LEFT:
  case CEC_USER_CONTROL_CODE_LEFT_UP:
  case CEC_USER_CONTROL_CODE_LEFT_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_LEFT;
    break;
  case CEC_USER_CONTROL_CODE_RIGHT:
  case CEC_USER_CONTROL_CODE_RIGHT_UP:
  case CEC_USER_CONTROL_CODE_RIGHT_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_RIGHT;
    break;
  case CEC_USER_CONTROL_CODE_ROOT_MENU:
    m_button.iButton = XINPUT_IR_REMOTE_MENU;
    break;
  case CEC_USER_CONTROL_CODE_EXIT:
    m_button.iButton = XINPUT_IR_REMOTE_BACK;
    break;
  case CEC_USER_CONTROL_CODE_ENTER:
    m_button.iButton = XINPUT_IR_REMOTE_ENTER;
    break;
  case CEC_USER_CONTROL_CODE_CHANNEL_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_CHANNEL_MINUS;
    break;
  case CEC_USER_CONTROL_CODE_CHANNEL_UP:
    m_button.iButton = XINPUT_IR_REMOTE_CHANNEL_PLUS;
    break;
  case CEC_USER_CONTROL_CODE_PREVIOUS_CHANNEL:
    m_button.iButton = XINPUT_IR_REMOTE_BACK;
    break;
  case CEC_USER_CONTROL_CODE_SOUND_SELECT:
    m_button.iButton = XINPUT_IR_REMOTE_LANGUAGE;
    break;
  case CEC_USER_CONTROL_CODE_POWER:
    m_button.iButton = XINPUT_IR_REMOTE_POWER;
    break;
  case CEC_USER_CONTROL_CODE_VOLUME_UP:
    m_button.iButton = XINPUT_IR_REMOTE_VOLUME_PLUS;
    break;
  case CEC_USER_CONTROL_CODE_VOLUME_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_VOLUME_MINUS;
    break;
  case CEC_USER_CONTROL_CODE_MUTE:
    m_button.iButton = XINPUT_IR_REMOTE_MUTE;
    break;
  case CEC_USER_CONTROL_CODE_PLAY:
    m_button.iButton = XINPUT_IR_REMOTE_PLAY;
    break;
  case CEC_USER_CONTROL_CODE_STOP:
    m_button.iButton = XINPUT_IR_REMOTE_STOP;
    break;
  case CEC_USER_CONTROL_CODE_PAUSE:
    m_button.iButton = XINPUT_IR_REMOTE_STOP;
    break;
  case CEC_USER_CONTROL_CODE_REWIND:
    m_button.iButton = XINPUT_IR_REMOTE_REVERSE;
    break;
  case CEC_USER_CONTROL_CODE_FAST_FORWARD:
    m_button.iButton = XINPUT_IR_REMOTE_FORWARD;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER0:
    m_button.iButton = XINPUT_IR_REMOTE_0;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER1:
    m_button.iButton = XINPUT_IR_REMOTE_1;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER2:
    m_button.iButton = XINPUT_IR_REMOTE_2;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER3:
    m_button.iButton = XINPUT_IR_REMOTE_3;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER4:
    m_button.iButton = XINPUT_IR_REMOTE_4;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER5:
    m_button.iButton = XINPUT_IR_REMOTE_5;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER6:
    m_button.iButton = XINPUT_IR_REMOTE_6;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER7:
    m_button.iButton = XINPUT_IR_REMOTE_7;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER8:
    m_button.iButton = XINPUT_IR_REMOTE_8;
    break;
  case CEC_USER_CONTROL_CODE_NUMBER9:
    m_button.iButton = XINPUT_IR_REMOTE_9;
    break;
  case CEC_USER_CONTROL_CODE_RECORD:
    m_button.iButton = XINPUT_IR_REMOTE_RECORD;
    break;
  case CEC_USER_CONTROL_CODE_CLEAR:
    m_button.iButton = XINPUT_IR_REMOTE_CLEAR;
    break;
  case CEC_USER_CONTROL_CODE_DISPLAY_INFORMATION:
    m_button.iButton = XINPUT_IR_REMOTE_INFO;
    break;
  case CEC_USER_CONTROL_CODE_PAGE_UP:
    m_button.iButton = XINPUT_IR_REMOTE_CHANNEL_PLUS;
    break;
  case CEC_USER_CONTROL_CODE_PAGE_DOWN:
    m_button.iButton = XINPUT_IR_REMOTE_CHANNEL_MINUS;
    break;
  case CEC_USER_CONTROL_CODE_EJECT:
    break;
  case CEC_USER_CONTROL_CODE_FORWARD:
    m_button.iButton = XINPUT_IR_REMOTE_SKIP_PLUS;
    break;
  case CEC_USER_CONTROL_CODE_BACKWARD:
    m_button.iButton = XINPUT_IR_REMOTE_SKIP_MINUS;
    break;
  case CEC_USER_CONTROL_CODE_POWER_ON_FUNCTION:
    break;
  case CEC_USER_CONTROL_CODE_F1_BLUE:
    m_button.iButton = XINPUT_IR_REMOTE_BLUE;
    break;
  case CEC_USER_CONTROL_CODE_F2_RED:
    m_button.iButton = XINPUT_IR_REMOTE_RED;
    break;
  case CEC_USER_CONTROL_CODE_F3_GREEN:
    m_button.iButton = XINPUT_IR_REMOTE_GREEN;
    break;
  case CEC_USER_CONTROL_CODE_F4_YELLOW:
    m_button.iButton = XINPUT_IR_REMOTE_YELLOW;
    break;
  case CEC_USER_CONTROL_CODE_SETUP_MENU:
  case CEC_USER_CONTROL_CODE_CONTENTS_MENU:
  case CEC_USER_CONTROL_CODE_FAVORITE_MENU:
  case CEC_USER_CONTROL_CODE_DOT:
  case CEC_USER_CONTROL_CODE_NEXT_FAVORITE:
  case CEC_USER_CONTROL_CODE_INPUT_SELECT:
  case CEC_USER_CONTROL_CODE_INITIAL_CONFIGURATION:
  case CEC_USER_CONTROL_CODE_HELP:
  case CEC_USER_CONTROL_CODE_STOP_RECORD:
  case CEC_USER_CONTROL_CODE_PAUSE_RECORD:
  case CEC_USER_CONTROL_CODE_ANGLE:
  case CEC_USER_CONTROL_CODE_SUB_PICTURE:
  case CEC_USER_CONTROL_CODE_VIDEO_ON_DEMAND:
  case CEC_USER_CONTROL_CODE_ELECTRONIC_PROGRAM_GUIDE:
  case CEC_USER_CONTROL_CODE_TIMER_PROGRAMMING:
  case CEC_USER_CONTROL_CODE_PLAY_FUNCTION:
  case CEC_USER_CONTROL_CODE_PAUSE_PLAY_FUNCTION:
  case CEC_USER_CONTROL_CODE_RECORD_FUNCTION:
  case CEC_USER_CONTROL_CODE_PAUSE_RECORD_FUNCTION:
  case CEC_USER_CONTROL_CODE_STOP_FUNCTION:
  case CEC_USER_CONTROL_CODE_MUTE_FUNCTION:
  case CEC_USER_CONTROL_CODE_RESTORE_VOLUME_FUNCTION:
  case CEC_USER_CONTROL_CODE_TUNE_FUNCTION:
  case CEC_USER_CONTROL_CODE_SELECT_MEDIA_FUNCTION:
  case CEC_USER_CONTROL_CODE_SELECT_AV_INPUT_FUNCTION:
  case CEC_USER_CONTROL_CODE_SELECT_AUDIO_INPUT_FUNCTION:
  case CEC_USER_CONTROL_CODE_POWER_TOGGLE_FUNCTION:
  case CEC_USER_CONTROL_CODE_POWER_OFF_FUNCTION:
  case CEC_USER_CONTROL_CODE_F5:
  case CEC_USER_CONTROL_CODE_DATA:
  case CEC_USER_CONTROL_CODE_UNKNOWN:
    m_bHasButton = false;
    return false;
  }

  return true;
}

WORD CPeripheralCecAdapter::GetButton(void)
{
  CSingleLock lock(m_critSection);
  if (!m_bHasButton)
    GetNextKey();

  return m_bHasButton ? m_button.iButton : 0;
}

unsigned int CPeripheralCecAdapter::GetHoldTime(void)
{
  CSingleLock lock(m_critSection);
  if (!m_bHasButton)
    GetNextKey();

  if (!m_bHasButton)
    return 0;

  if (m_button.iButtonPressed && m_button.iButtonReleased)
    return m_button.iButtonReleased - m_button.iButtonPressed;
  else if (m_button.iButtonPressed)
    return XbmcThreads::SystemClockMillis() - m_button.iButtonPressed;

  return 0;
}

void CPeripheralCecAdapter::ResetButton(void)
{
  CSingleLock lock(m_critSection);
  m_bHasButton = false;
}

void CPeripheralCecAdapter::OnSettingChanged(const CStdString &strChangedSetting)
{
  if (strChangedSetting.Equals("enabled"))
  {
    bool bEnabled(GetSettingBool("enabled"));
    if (!bEnabled && m_cecParser && m_bStarted)
      StopThread(true);
    else if (bEnabled && !m_cecParser && m_bStarted)
      InitialiseFeature(FEATURE_CEC);
  }
}

void CPeripheralCecAdapter::FlushLog(void)
{
  cec_log_message message;
  while (m_cecParser && m_cecParser->GetNextLogMessage(&message))
  {
    int iLevel = -1;
    switch (message.level)
    {
    case CEC_LOG_ERROR:
      iLevel = LOGERROR;
      break;
    case CEC_LOG_WARNING:
      iLevel = LOGWARNING;
      break;
    case CEC_LOG_NOTICE:
      iLevel = LOGDEBUG;
      break;
    case CEC_LOG_DEBUG:
      if (GetSettingBool("cec_debug_logging"))
        iLevel = LOGDEBUG;
      break;
    }

    if (iLevel >= 0)
      CLog::Log(iLevel, "%s - %s", __FUNCTION__, message.message.c_str());
  }
}

bool CPeripheralCecAdapter::TranslateComPort(CStdString &strLocation)
{
  if (strLocation.Left(18).Equals("peripherals://usb/") && strLocation.Right(4).Equals(".dev"))
  {
    strLocation = strLocation.Right(strLocation.length() - 18);
    strLocation = strLocation.Left(strLocation.length() - 4);
    return true;
  }

  return false;
}
