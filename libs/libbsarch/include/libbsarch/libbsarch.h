#pragma once

#include <dxgiformat.h>

#include <DDS.h>

#include <stdint.h>

#ifdef _WIN32
  #ifdef BSARCH_DLL_EXPORT
  #define BSARCH_DLL_API(ReturnType) extern "C" __declspec(dllexport) ReturnType __stdcall
  #else
  #define BSARCH_DLL_API(ReturnType) extern "C" __declspec(dllimport) ReturnType __stdcall
  #endif
#else
  #ifdef BSARCH_DLL_EXPORT
  #define BSARCH_DLL_API(ReturnType) extern "C" __attribute__((visibility("default"))) ReturnType
  #else
  #define BSARCH_DLL_API(ReturnType) extern "C" ReturnType
  #endif
#endif

#ifdef __GNUC__
#define PACKED(datastructure) datastructure __attribute__((__packed__))
#else
#define PACKED(datastructure) __pragma(pack(push, 1)) datastructure __pragma(pack(pop))
#endif

typedef enum bsa_result_code_e
{
    BSA_RESULT_NONE = 0,
    BSA_RESULT_EXCEPTION = -1
} bsa_result_code_t;

typedef void *bsa_buffer_t;
typedef void *bsa_archive_t;
typedef void *bsa_entry_list_t;
typedef void *bsa_file_record_t;
typedef void *bsa_folder_record_t;

typedef struct bsa_dds_info_s
{
    uint32_t width, height, mipmaps;
} bsa_dds_info_t;

PACKED(struct bsa_dds_header_s {
    uint32_t magic; // DDS_MAGIC
    DirectX::DDS_HEADER header;
});

typedef struct bsa_dds_header_s bsa_dds_header_t;

PACKED(struct bsa_result_message_s {
    int8_t code; // bsa_result_code_t
    wchar_t text[1024];
});

typedef struct bsa_result_message_s bsa_result_message_t;

PACKED(struct bsa_result_buffer_s {
    uint32_t size;
    bsa_buffer_t data;
});

typedef struct bsa_result_buffer_s bsa_result_buffer_t;

PACKED(struct bsa_result_message_buffer_s {
    bsa_result_buffer_t buffer;
    bsa_result_message_t message;
});

typedef struct bsa_result_message_buffer_s bsa_result_message_buffer_t;

typedef enum bsa_archive_state_e
{
    stReading,
    stWriting
} bsa_archive_state_t;

typedef enum bsa_archive_type_e
{
    baNone,
    baTES3,
    baTES4,
    baFO3,
    baSSE,
    baFO4,
    baFO4dds,
    baSF,
    baSFdds
} bsa_archive_type_t;

typedef void (*bsa_file_dds_info_proc_t)(bsa_archive_t archive,
                                         const wchar_t *file_path,
                                         bsa_dds_info_t *dds_info,
                                         void *context);
typedef bool (*bsa_file_iteration_proc_t)(bsa_archive_t archive,
                                          const wchar_t *file_path,
                                          bsa_file_record_t file_record,
                                          bsa_folder_record_t folder_record,
                                          void *context);

BSARCH_DLL_API(bsa_entry_list_t) bsa_entry_list_create();
BSARCH_DLL_API(bsa_result_message_t) bsa_entry_list_free(bsa_entry_list_t entry_list);
BSARCH_DLL_API(uint32_t) bsa_entry_list_count(bsa_entry_list_t entry_list);
BSARCH_DLL_API(bsa_result_message_t) bsa_entry_list_add(bsa_entry_list_t entry_list, const wchar_t *entry_string);
BSARCH_DLL_API(uint32_t)
bsa_entry_list_get(bsa_entry_list_t entry_list, uint32_t index, uint32_t string_buffer_size, wchar_t *string_buffer);

BSARCH_DLL_API(bsa_archive_t) bsa_create();
BSARCH_DLL_API(bsa_result_message_t) bsa_free(bsa_archive_t archive);
BSARCH_DLL_API(bsa_result_message_t) bsa_load_from_file(bsa_archive_t archive, const wchar_t *file_path);
BSARCH_DLL_API(bsa_result_message_t)
bsa_create_archive(bsa_archive_t archive,
                   const wchar_t *file_path,
                   bsa_archive_type_t _archivetype,
                   bsa_entry_list_t entry_list);
BSARCH_DLL_API(bsa_result_message_t) bsa_save(bsa_archive_t archive);
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_disk(bsa_archive_t archive, const wchar_t *file_path, const wchar_t *source_path);
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_disk_root(bsa_archive_t archive, const wchar_t *root_dir, const wchar_t *source_path);
BSARCH_DLL_API(bsa_result_message_t)
bsa_add_file_from_memory(bsa_archive_t archive, const wchar_t *file_path, uint32_t size, bsa_buffer_t data);
BSARCH_DLL_API(bsa_file_record_t) bsa_find_file_record(bsa_archive_t archive, const wchar_t *file_path);
BSARCH_DLL_API(bsa_result_message_buffer_t)
bsa_extract_file_data_by_record(bsa_archive_t archive, bsa_file_record_t file_record);
BSARCH_DLL_API(bsa_result_message_buffer_t)
bsa_extract_file_data_by_filename(bsa_archive_t archive, const wchar_t *file_path);
BSARCH_DLL_API(bsa_result_message_t) bsa_file_data_free(bsa_archive_t archive, bsa_result_buffer_t file_data_result);
BSARCH_DLL_API(bsa_result_message_t)
bsa_extract_file(bsa_archive_t archive, const wchar_t *file_path, const wchar_t *save_as);
BSARCH_DLL_API(bsa_result_message_t)
bsa_iterate_files(bsa_archive_t archive, bsa_file_iteration_proc_t file_iteration_proc, void *context);
BSARCH_DLL_API(bool) bsa_file_exists(bsa_archive_t archive, const wchar_t *file_path);
BSARCH_DLL_API(bsa_result_message_t)
bsa_get_resource_list(bsa_archive_t archive, bsa_entry_list_t entry_result_list, const wchar_t *folder);
BSARCH_DLL_API(bsa_result_message_t) bsa_close(bsa_archive_t archive);

BSARCH_DLL_API(uint32_t)
bsa_filename_get(bsa_archive_t archive, uint32_t string_buffer_size, wchar_t *string_buffer);
BSARCH_DLL_API(bsa_archive_type_t) bsa_archive_type_get(bsa_archive_t archive);
BSARCH_DLL_API(uint32_t) bsa_version_get(bsa_archive_t archive);
BSARCH_DLL_API(uint32_t)
bsa_format_name_get(bsa_archive_t archive, uint32_t string_buffer_size, wchar_t *string_buffer);
BSARCH_DLL_API(uint32_t) bsa_file_count_get(bsa_archive_t archive);
BSARCH_DLL_API(uint32_t) bsa_archive_flags_get(bsa_archive_t archive);
BSARCH_DLL_API(void) bsa_archive_flags_set(bsa_archive_t archive, uint32_t flags);
BSARCH_DLL_API(uint32_t) bsa_file_flags_get(bsa_archive_t archive);
BSARCH_DLL_API(void) bsa_file_flags_set(bsa_archive_t archive, uint32_t flags);
BSARCH_DLL_API(bool) bsa_compress_get(bsa_archive_t archive);
BSARCH_DLL_API(void) bsa_compress_set(bsa_archive_t archive, bool flags);
BSARCH_DLL_API(bool) bsa_share_data_get(bsa_archive_t archive);
BSARCH_DLL_API(void) bsa_share_data_set(bsa_archive_t archive, bool flags);

BSARCH_DLL_API(void)
bsa_file_dds_info_callback_set(bsa_archive_t archive, bsa_file_dds_info_proc_t file_dds_info_proc, void *context);
