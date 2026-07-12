#include "scsi_sptd/iat_hook.h"

#include <string.h>

static IMAGE_IMPORT_DESCRIPTOR *find_import_directory(HMODULE module)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos_header = (IMAGE_DOS_HEADER *)base;

    if(dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return NULL;
    }

    IMAGE_NT_HEADERS *nt_headers = (IMAGE_NT_HEADERS *)(base + dos_header->e_lfanew);
    if(nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return NULL;
    }

    DWORD import_rva = nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(import_rva == 0) {
        return NULL;
    }

    return (IMAGE_IMPORT_DESCRIPTOR *)(base + import_rva);
}

static BOOL replace_import_thunk(IMAGE_THUNK_DATA *iat_thunk, FARPROC replacement_function)
{
    DWORD old_protect = 0;
    SIZE_T thunk_size = sizeof(iat_thunk->u1.Function);

    if(!VirtualProtect(&iat_thunk->u1.Function, thunk_size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        return FALSE;
    }

    iat_thunk->u1.Function = (ULONG_PTR)replacement_function;
    FlushInstructionCache(GetCurrentProcess(), &iat_thunk->u1.Function, thunk_size);
    VirtualProtect(&iat_thunk->u1.Function, thunk_size, old_protect, &old_protect);

    return TRUE;
}

BOOL hook_iat_by_name(const char *dll_name, const char *function_name, FARPROC replacement_function)
{
    if(dll_name == NULL || function_name == NULL || replacement_function == NULL) {
        return FALSE;
    }

    HMODULE module = GetModuleHandleA(NULL);
    if(module == NULL) {
        return FALSE;
    }

    BYTE *base = (BYTE *)module;
    IMAGE_IMPORT_DESCRIPTOR *import_directory = find_import_directory(module);
    if(import_directory == NULL) {
        return FALSE;
    }

    for(IMAGE_IMPORT_DESCRIPTOR *descriptor = import_directory; descriptor->Name != 0; ++descriptor) {
        const char *imported_module_name = (const char *)(base + descriptor->Name);
        if(lstrcmpiA(imported_module_name, dll_name) != 0) {
            continue;
        }

        IMAGE_THUNK_DATA *name_thunk = descriptor->OriginalFirstThunk != 0
            ? (IMAGE_THUNK_DATA *)(base + descriptor->OriginalFirstThunk)
            : (IMAGE_THUNK_DATA *)(base + descriptor->FirstThunk);
        IMAGE_THUNK_DATA *iat_thunk = (IMAGE_THUNK_DATA *)(base + descriptor->FirstThunk);

        for(; name_thunk->u1.AddressOfData != 0; ++name_thunk, ++iat_thunk) {
            if(IMAGE_SNAP_BY_ORDINAL(name_thunk->u1.Ordinal)) {
                continue;
            }

            IMAGE_IMPORT_BY_NAME *imported_function = (IMAGE_IMPORT_BY_NAME *)(base + name_thunk->u1.AddressOfData);
            if(strcmp((const char *)imported_function->Name, function_name) == 0) {
                return replace_import_thunk(iat_thunk, replacement_function);
            }
        }
    }

    return FALSE;
}

BOOL Hook_IAT_Name(const char *dll_name, const char *function_name, FARPROC replacement_function)
{
    return hook_iat_by_name(dll_name, function_name, replacement_function);
}
