#include "../../src/windows/hook.h"

#define IMAGE_SIZE 0x1000
#define NT_OFFSET 0x80
#define IMPORT_OFFSET 0x200
#define DLL_NAME_OFFSET 0x300
#define ORIGINAL_THUNK_OFFSET 0x400
#define FIRST_THUNK_OFFSET 0x500
#define IMPORT_NAME_OFFSET 0x600

#define CHECK(condition, code)                                                \
    if (!(condition))                                                         \
        return code

typedef struct {
    BYTE *base;
    IMAGE_IMPORT_DESCRIPTOR *import;
    IMAGE_THUNK_DATA *original_thunks;
    IMAGE_THUNK_DATA *first_thunks;
} SyntheticImage;

static void WINAPI original_function(void) {}
static void WINAPI previous_detour(void) {}
static void WINAPI doorstop_detour(void) {}

#define DETOUR_PROCESS_ID ((DWORD)0xD00570F)
static DWORD WINAPI get_process_id_detour(void) { return DETOUR_PROCESS_ID; }

static void copy_ascii(char *destination, char const *source) {
    while ((*destination++ = *source++) != '\0') {
    }
}

static SyntheticImage create_image(void) {
    SyntheticImage image = {0};
    image.base = VirtualAlloc(NULL, IMAGE_SIZE, MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
    if (!image.base)
        return image;

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)image.base;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = NT_OFFSET;

    IMAGE_NT_HEADERS *nt =
        (IMAGE_NT_HEADERS *)(image.base + NT_OFFSET);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
    nt->OptionalHeader.SizeOfImage = IMAGE_SIZE;
    nt->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
        .VirtualAddress = IMPORT_OFFSET;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size =
        2 * sizeof(IMAGE_IMPORT_DESCRIPTOR);

    image.import =
        (IMAGE_IMPORT_DESCRIPTOR *)(image.base + IMPORT_OFFSET);
    image.import->Name = DLL_NAME_OFFSET;
    image.import->OriginalFirstThunk = ORIGINAL_THUNK_OFFSET;
    image.import->FirstThunk = FIRST_THUNK_OFFSET;
    copy_ascii((char *)(image.base + DLL_NAME_OFFSET), "KERNEL32.dll");

    image.original_thunks =
        (IMAGE_THUNK_DATA *)(image.base + ORIGINAL_THUNK_OFFSET);
    image.original_thunks[0].u1.AddressOfData = IMPORT_NAME_OFFSET;
    image.original_thunks[1].u1.AddressOfData = 0;

    IMAGE_IMPORT_BY_NAME *import_name =
        (IMAGE_IMPORT_BY_NAME *)(image.base + IMPORT_NAME_OFFSET);
    import_name->Hint = 0;
    copy_ascii((char *)import_name->Name, "GetProcAddress");

    image.first_thunks =
        (IMAGE_THUNK_DATA *)(image.base + FIRST_THUNK_OFFSET);
    image.first_thunks[0].u1.Function = (ULONG_PTR)&previous_detour;
    image.first_thunks[1].u1.Function = 0;
    return image;
}

static void destroy_image(SyntheticImage *image) {
    if (image->base)
        VirtualFree(image->base, 0, MEM_RELEASE);
}

int main(void) {
    // Exercise a real loaded PE image before the malformed-image cases. The
    // final PE page may be discardable/reserved, but the import table itself
    // remains a valid hook target.
    HMODULE current_module = GetModuleHandleW(NULL);
    void *real_get_process_id = NULL;
    CHECK(iat_hook(current_module, "kernel32.dll", "GetCurrentProcessId",
                   (void *)&GetCurrentProcessId,
                   (void *)&get_process_id_detour, &real_get_process_id),
          19);
    CHECK(GetCurrentProcessId() == DETOUR_PROCESS_ID, 20);
    void *replaced_detour = NULL;
    CHECK(iat_hook(current_module, "kernel32.dll", "GetCurrentProcessId",
                   (void *)&get_process_id_detour, real_get_process_id,
                   &replaced_detour),
          21);
    CHECK(replaced_detour == (void *)&get_process_id_detour, 22);

    // OriginalFirstThunk identifies the import even though another tool has
    // already replaced the initialized IAT value.
    SyntheticImage chained = create_image();
    CHECK(chained.base != NULL, 1);
    void *previous = NULL;
    CHECK(iat_hook(chained.base, "kernel32.dll", "GetProcAddress",
                   (void *)&original_function, (void *)&doorstop_detour,
                   &previous),
          2);
    CHECK(previous == (void *)&previous_detour, 3);
    CHECK(chained.first_thunks[0].u1.Function ==
              (ULONG_PTR)&doorstop_detour,
          4);
    destroy_image(&chained);

    // A caller that does not retain the previous function must not overwrite
    // an existing hook even when OriginalFirstThunk identifies the slot.
    SyntheticImage unchained = create_image();
    CHECK(unchained.base != NULL, 16);
    CHECK(!iat_hook(unchained.base, "kernel32.dll", "GetProcAddress",
                    (void *)&original_function, (void *)&doorstop_detour,
                    NULL),
          17);
    CHECK(unchained.first_thunks[0].u1.Function ==
              (ULONG_PTR)&previous_detour,
          18);
    destroy_image(&unchained);

    // An image without OriginalFirstThunk can still use the conservative
    // address fallback while the IAT is untouched.
    SyntheticImage fallback = create_image();
    CHECK(fallback.base != NULL, 5);
    fallback.import->OriginalFirstThunk = 0;
    fallback.first_thunks[0].u1.Function = (ULONG_PTR)&original_function;
    previous = NULL;
    CHECK(iat_hook(fallback.base, "kernel32.dll", "GetProcAddress",
                   (void *)&original_function, (void *)&doorstop_detour,
                   &previous),
          6);
    CHECK(previous == (void *)&original_function, 7);
    destroy_image(&fallback);

    // Without lookup metadata an already-modified slot is ambiguous and must
    // fail closed instead of overwriting an unrelated/previous hook.
    SyntheticImage ambiguous = create_image();
    CHECK(ambiguous.base != NULL, 8);
    ambiguous.import->OriginalFirstThunk = 0;
    previous = (void *)&original_function;
    CHECK(!iat_hook(ambiguous.base, "kernel32.dll", "GetProcAddress",
                    (void *)&original_function, (void *)&doorstop_detour,
                    &previous),
          9);
    CHECK(previous == NULL, 10);
    CHECK(ambiguous.first_thunks[0].u1.Function ==
              (ULONG_PTR)&previous_detour,
          11);
    destroy_image(&ambiguous);

    // Ordinal imports are skipped without treating the ordinal as a string.
    SyntheticImage ordinal = create_image();
    CHECK(ordinal.base != NULL, 12);
    ordinal.original_thunks[0].u1.Ordinal =
        IMAGE_ORDINAL_FLAG | (ULONG_PTR)7;
    CHECK(!iat_hook(ordinal.base, "kernel32.dll", "GetProcAddress",
                    (void *)&original_function, (void *)&doorstop_detour,
                    NULL),
          13);
    destroy_image(&ordinal);

    // Malformed name RVAs are rejected before any pointer is dereferenced.
    SyntheticImage malformed = create_image();
    CHECK(malformed.base != NULL, 14);
    malformed.original_thunks[0].u1.AddressOfData = IMAGE_SIZE + 1;
    CHECK(!iat_hook(malformed.base, "kernel32.dll", "GetProcAddress",
                    (void *)&original_function, (void *)&doorstop_detour,
                    NULL),
          15);
    destroy_image(&malformed);

    return 0;
}
