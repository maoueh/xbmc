/*
*      Copyright (C) 2005-2008 Team XBMC
*      http://www.xbmc.org
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

#ifndef WINDOWING_FACTORY_H
#define WINDOWING_FACTORY_H

#include "system.h"

#if   defined(TARGET_WINDOWS) && defined(HAS_GL)
#include "windows/WinSystemWin32GL.h"

#elif defined(TARGET_WINDOWS) && defined(HAS_DX)
#include "windows/WinSystemWin32DX.h"

#elif defined(TARGET_LINUX)   && defined(HAS_GLX)
#include "X11/WinSystemX11GL.h"

#elif defined(TARGET_LINUX)   &&  defined(HAVE_X11) && defined(HAS_GLES)
#include "X11/WinSystemX11GLES.h"

#elif defined(TARGET_LINUX)   && !defined(HAVE_X11) && defined(HAS_GLES)
#include "egl/WinSystemGLES.h"

#elif defined(TARGET_DARWIN_OSX)
#include "osx/WinSystemOSXGL.h"

#elif defined(TARGET_DARWIN_IOS)
#include "osx/WinSystemIOS.h"

#endif

#endif // WINDOWING_FACTORY_H

