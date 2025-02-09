/**
 * @file
 * @brief main function and system functions
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <fcntl.h>
#include <unistd.h>

#include "../../common/common.h"
#include "../system.h"

/* ======================================================================= */
/* General routines */
/* ======================================================================= */

void Sys_Init (void)
{
	sys_os = Cvar_Get("sys_os", "linux", CVAR_SERVERINFO);
	sys_affinity = Cvar_Get("sys_affinity", "0", CVAR_ARCHIVE);
	sys_priority = Cvar_Get("sys_priority", "0", CVAR_ARCHIVE, "Process nice level");
}

/**
 * @brief The entry point for linux server and client.
 * Initializes the program and calls @c Qcommon_Frame in an infinite loop.
 */
#ifdef __vita__
#include <vitasdk.h>
#include <vitaGL.h>
int quake_main (unsigned int argc, void *argv)
#else
int main (int argc, char** argv)
#endif
{
	vglUseTripleBuffering(GL_FALSE);
	vglSetParamBufferSize(4 * 1024 * 1024);
	vglInitWithCustomThreshold(0, 960, 544, 2 * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);
	Sys_ConsoleInit();
	Qcommon_Init(argc, (char **)argv);

	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | FNDELAY);

	while (1)
		Qcommon_Frame();

	return 0;
}

#ifdef __vita__
int _newlib_heap_size_user = 320 * 1024 * 1024;

int main(int argc, char **argv)
{
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	int ret = sceNetShowNetstat();
	SceNetInitParam initparam;
	if (ret == SCE_NET_ERROR_ENOTINIT) {
		initparam.memory = malloc(141 * 1024);
		initparam.size = 141 * 1024;
		initparam.flags = 0;
		sceNetInit(&initparam);
	}
	
	// We need a bigger stack to run Quake, so we create a new thread with a proper stack size
	SceUID main_thread = sceKernelCreateThread("UFO: AI", quake_main, 0x40, 0x100000, 0, 0, NULL);
	if (main_thread >= 0){
		sceKernelStartThread(main_thread, 0, NULL);
		sceKernelWaitThreadEnd(main_thread, NULL, NULL);
	}
	return 0;
}
#endif
