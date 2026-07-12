#include <windows.h>

#include "scsi_sptd/sptd.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if(reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        return scsi_sptd_install_from_command_line();
    }

    return TRUE;
}
