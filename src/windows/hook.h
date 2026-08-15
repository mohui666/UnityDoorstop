/*
 * IAT hooking for Windows.
 *
 * More IAT/EAT hooking methods at
 * https://gist.github.com/denikson/93ea22c1f4e79e68466a26cbfc58af05
 */

#ifndef HOOK_H
#define HOOK_H

#include "../util/util.h"
#include <stddef.h>
#include <windows.h>

// PE format uses RVAs (Relative Virtual Addresses) to save addresses relative
// to the base of the module More info:
// https://en.wikibooks.org/wiki/X86_Disassembly/Windows_Executable_Files#Relative_Virtual_Addressing_(RVA)
//
// This helper macro converts the saved RVA to a fully valid pointer to the data
// in the PE file
#define RVA2PTR(t, base, rva) ((t)(((PCHAR)(base)) + (rva)))

static bool_t iat_rva_valid(size_t image_size, ULONG_PTR rva,
                            size_t value_size) {
    if (rva > image_size)
        return FALSE;
    return value_size <= image_size - (size_t)rva;
}

static char iat_ascii_fold(char value) {
    if (value >= 'A' && value <= 'Z')
        return (char)(value + ('a' - 'A'));
    return value;
}

static bool_t iat_string_equals(char const *candidate, size_t candidate_size,
                                char const *expected,
                                bool_t case_insensitive) {
    if (!candidate || !expected)
        return FALSE;

    for (size_t i = 0; i < candidate_size; i++) {
        char candidate_char = candidate[i];
        char expected_char = expected[i];
        if (case_insensitive) {
            candidate_char = iat_ascii_fold(candidate_char);
            expected_char = iat_ascii_fold(expected_char);
        }

        if (candidate_char != expected_char)
            return FALSE;
        if (candidate_char == '\0')
            return TRUE;
    }

    // The string in the image was not terminated inside the mapped image.
    return FALSE;
}

static bool_t iat_replace_thunk(IMAGE_THUNK_DATA *thunk,
                                void *detour_function,
                                void **previous_function) {
    if (!thunk || !detour_function)
        return FALSE;

    DWORD old_state;
    if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function),
                        PAGE_READWRITE, &old_state))
        return FALSE;

    void *previous = InterlockedExchangePointer(
        (PVOID volatile *)&thunk->u1.Function, detour_function);

    DWORD ignored_state;
    VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), old_state,
                   &ignored_state);

    if (previous_function)
        *previous_function = previous;
    return TRUE;
}

/**
 * @brief Hooks an imported function through a module's Import Address Table.
 *
 * When OriginalFirstThunk is available, the immutable import metadata is used
 * to identify the slot by DLL and function name. This continues to work when
 * another tool has already replaced the initialized IAT value, provided the
 * caller requests the previous function so it can preserve the hook chain.
 * Images without OriginalFirstThunk safely fall back to matching the current
 * IAT value.
 *
 * @param dll Module to hook
 * @param target_dll Name of the target DLL to search in the import table
 * @param target_import Name of the imported function
 * @param target_function Original function address used only for safe fallback
 * @param detour_function Address of the detour function
 * @param previous_function Receives the function currently stored in the slot
 * @return bool_t TRUE if successful, otherwise FALSE
 */
static bool_t iat_hook(void *dll, char const *target_dll,
                       char const *target_import, void *target_function,
                       void *detour_function, void **previous_function) {
    if (previous_function)
        *previous_function = NULL;
    if (!dll || !target_dll || !target_import || !target_function ||
        !detour_function)
        return FALSE;

    MEMORY_BASIC_INFORMATION memory_info;
    if (!VirtualQuery(dll, &memory_info, sizeof(memory_info)) ||
        memory_info.State != MEM_COMMIT)
        return FALSE;

    PBYTE base = (PBYTE)dll;
    PBYTE region_end = (PBYTE)memory_info.BaseAddress + memory_info.RegionSize;
    if (base < (PBYTE)memory_info.BaseAddress || base >= region_end ||
        memory_info.AllocationBase != dll)
        return FALSE;
    size_t header_region_size = (size_t)(region_end - base);
    if (header_region_size < sizeof(IMAGE_NT_HEADERS))
        return FALSE;

    IMAGE_DOS_HEADER *mz = (IMAGE_DOS_HEADER *)base;
    if (mz->e_magic != IMAGE_DOS_SIGNATURE || mz->e_lfanew <= 0 ||
        (size_t)mz->e_lfanew >
            header_region_size - sizeof(IMAGE_NT_HEADERS))
        return FALSE;

    IMAGE_NT_HEADERS *nt =
        RVA2PTR(IMAGE_NT_HEADERS *, base, (size_t)mz->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <=
            IMAGE_DIRECTORY_ENTRY_IMPORT)
        return FALSE;

    size_t image_size = nt->OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY import_directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (image_size == 0 || image_size > MAXULONG_PTR - (ULONG_PTR)base ||
        import_directory.VirtualAddress == 0 ||
        import_directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
        !iat_rva_valid(image_size, import_directory.VirtualAddress,
                       import_directory.Size))
        return FALSE;

    MEMORY_BASIC_INFORMATION image_end_info;
    if (!VirtualQuery(base + image_size - 1, &image_end_info,
                      sizeof(image_end_info)) ||
        image_end_info.AllocationBase != memory_info.AllocationBase)
        return FALSE;

    IMAGE_IMPORT_DESCRIPTOR *imports = RVA2PTR(
        IMAGE_IMPORT_DESCRIPTOR *, base, import_directory.VirtualAddress);
    size_t import_count =
        import_directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);

    for (size_t i = 0; i < import_count; i++) {
        IMAGE_IMPORT_DESCRIPTOR *import = &imports[i];
        if (import->Name == 0 && import->FirstThunk == 0 &&
            import->OriginalFirstThunk == 0)
            break;
        if (import->Name == 0 || import->FirstThunk == 0 ||
            !iat_rva_valid(image_size, import->Name, 1))
            return FALSE;

        char *dll_name = RVA2PTR(char *, base, import->Name);
        if (!iat_string_equals(dll_name, image_size - import->Name,
                               target_dll, TRUE))
            continue;

        if (!iat_rva_valid(image_size, import->FirstThunk,
                           sizeof(IMAGE_THUNK_DATA)))
            return FALSE;

        IMAGE_THUNK_DATA *first_thunks = RVA2PTR(
            IMAGE_THUNK_DATA *, base, import->FirstThunk);
        size_t first_thunk_count =
            (image_size - import->FirstThunk) / sizeof(IMAGE_THUNK_DATA);

        if (import->OriginalFirstThunk == 0) {
            // Bound imports can omit OriginalFirstThunk. In that case only an
            // untouched slot can be identified safely by its current value.
            if (!target_function)
                return FALSE;
            for (size_t j = 0; j < first_thunk_count; j++) {
                ULONG_PTR current = first_thunks[j].u1.Function;
                if (current == 0)
                    break;
                if ((void *)current == target_function)
                    return iat_replace_thunk(&first_thunks[j],
                                             detour_function,
                                             previous_function);
            }
            return FALSE;
        }

        if (!iat_rva_valid(image_size, import->OriginalFirstThunk,
                           sizeof(IMAGE_THUNK_DATA)))
            return FALSE;

        IMAGE_THUNK_DATA *original_thunks = RVA2PTR(
            IMAGE_THUNK_DATA *, base, import->OriginalFirstThunk);
        size_t original_thunk_count =
            (image_size - import->OriginalFirstThunk) /
            sizeof(IMAGE_THUNK_DATA);
        size_t thunk_count = first_thunk_count < original_thunk_count
                                 ? first_thunk_count
                                 : original_thunk_count;

        for (size_t j = 0; j < thunk_count; j++) {
            ULONG_PTR lookup = original_thunks[j].u1.AddressOfData;
            if (lookup == 0)
                break;
            if (IMAGE_SNAP_BY_ORDINAL(lookup))
                continue;

            size_t name_offset = offsetof(IMAGE_IMPORT_BY_NAME, Name);
            if (!iat_rva_valid(image_size, lookup, name_offset + 1))
                return FALSE;

            char *import_name =
                RVA2PTR(char *, base, lookup + name_offset);
            if (!iat_string_equals(import_name,
                                   image_size - (size_t)lookup - name_offset,
                                   target_import, FALSE))
                continue;

            if (first_thunks[j].u1.Function == 0)
                return FALSE;
            if ((void *)first_thunks[j].u1.Function != target_function &&
                !previous_function) {
                // The slot is already hooked, but the caller is not prepared
                // to retain and call through the previous implementation.
                return FALSE;
            }
            return iat_replace_thunk(&first_thunks[j], detour_function,
                                     previous_function);
        }

        return FALSE;
    }

    return FALSE;
}

#endif
