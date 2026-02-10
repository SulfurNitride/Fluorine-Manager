#ifndef BSA_FFI_H
#define BSA_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char **items;
  size_t count;
  char *error;
} BsaFfiStringList;

typedef void (*BsaProgressCallback)(uint32_t done, uint32_t total,
                                    const char *current_path);

/* Returns list of paths in archive. On error, error is non-null and must be freed with
 * bsa_ffi_string_list_free(). */
BsaFfiStringList bsa_ffi_list_files(const char *archive_path);

void bsa_ffi_string_list_free(BsaFfiStringList list);

/* Returns NULL on success, else an allocated error string (free with bsa_ffi_string_free). */
char *bsa_ffi_extract_all(const char *archive_path, const char *output_dir,
                          BsaProgressCallback progress_cb, const int *cancel_flag);

/* game_id uses CLI ids from GameVersion::cli_name():
 * morrowind, oblivion, fo3, fonv, skyrimle, skyrimse,
 * fo4-fo76, fo4ng-v7, fo4ng-v8, starfield-v2, starfield-v3
 */
char *bsa_ffi_pack_dir(const char *input_dir, const char *output_archive,
                       const char *game_id, BsaProgressCallback progress_cb,
                       const int *cancel_flag);

/* include_mode:
 * 0 = all files
 * 1 = exclude .dds
 * 2 = only .dds
 */
char *bsa_ffi_pack_dir_filtered(const char *input_dir, const char *output_archive,
                                const char *game_id, int include_mode,
                                BsaProgressCallback progress_cb,
                                const int *cancel_flag);

void bsa_ffi_string_free(char *s);

#ifdef __cplusplus
}
#endif

#endif
