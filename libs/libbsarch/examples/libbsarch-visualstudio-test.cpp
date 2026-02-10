#include <iostream>
#include <windows.h>

#include <libbsarch/libbsarch.h>

// #pragma comment(lib, "libbsarch")

static const std::wstring separators(L"\\/");

std::wstring dirname(const std::wstring &path)
{
    size_t slash_pos = path.find_last_of(separators);
    return path.substr(0, slash_pos);
}

std::wstring basename(const std::wstring &path)
{
    size_t slash_pos = path.find_last_of(separators);
    return path.substr(slash_pos + 1);
}

bool mkdirp(const std::wstring &directory, bool basedir = true)
{
    DWORD attributes = ::GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        std::size_t slash_pos = directory.find_last_of(separators);
        if (slash_pos != std::wstring::npos)
        {
            mkdirp(directory.substr(0, slash_pos));
        }
        return ::CreateDirectoryW(directory.c_str(), nullptr);
    }
    else
    {
        return true;
    }
}

void dds_info(bsa_archive_t archive, const wchar_t *file_path, bsa_dds_info_t *dds_info, void *context)
{
    // Mock result
    dds_info->width = 2048;
    dds_info->height = 2048;
    dds_info->mipmaps = 12;

    printf("ba2 dds_info callback file: %ls, context: %s\n", file_path, (char *) context);
}

int main()
{
    bsa_result_message_t result = {0};
    bsa_archive_t archive = bsa_create();

    {
        result = bsa_load_from_file(archive, L"test_read.bsa");
        if (result.code < 0)
            printf("ba1 %ls\n", result.text);

        bsa_entry_list_t entries = bsa_entry_list_create();
        bsa_get_resource_list(archive, entries, L"");

        for (size_t index = 0; index < bsa_entry_list_count(entries); index++)
        {
            wchar_t filename[2048];
            wchar_t dest_filename[2048] = L"test_ba1\\";

            bsa_entry_list_get(entries, index, 2048, filename);
            wcscat_s(dest_filename, 2048, filename);

            printf("ba1 file: %ls %ls\n", filename, dest_filename);

            if (mkdirp(dirname(dest_filename)))
            {
                result = bsa_extract_file(archive, filename, dest_filename);
                if (result.code < 0)
                    printf("ba1 %ls\n", result.text);
            }
            else
            {
                printf("ba1 could not create: %ls\n", dirname(dest_filename).c_str());
            }
        }

        bsa_entry_list_free(entries);

        bsa_close(archive);
    }

    {
        bsa_entry_list_t entries = bsa_entry_list_create();
        bsa_entry_list_add(entries, L"textures\\grass\\test.dds");
        bsa_entry_list_add(entries, L"textures\\grass\\test2.dds");

        bsa_create_archive(archive, L"test_write.bsa", baSSE, entries);
        bsa_add_file_from_disk_root(archive, L"test_ba1\\", L"test_ba1\\textures\\grass\\test.dds");
        result = bsa_add_file_from_disk(archive, L"textures\\grass\\test2.dds", L"test_ba1\\textures\\grass\\test.dds");
        if (result.code < 0)
            printf("ba1 %ls\n", result.text);
        bsa_save(archive);
        bsa_close(archive);

        bsa_entry_list_free(entries);
    }

    bsa_file_dds_info_callback_set(archive, dds_info, (void *) "I was shared");

    {
        result = bsa_load_from_file(archive, L"test_read.ba2");
        if (result.code < 0)
            printf("ba2 %ls\n", result.text);

        bsa_entry_list_t entries = bsa_entry_list_create();
        bsa_get_resource_list(archive, entries, L"");

        for (size_t index = 0; index < bsa_entry_list_count(entries); index++)
        {
            wchar_t filename[2048];
            wchar_t dest_filename[2048] = L"test_ba2\\";

            bsa_entry_list_get(entries, index, 2048, filename);
            wcscat_s(dest_filename, 2048, filename);

            printf("ba2 file: %ls %ls\n", filename, dest_filename);

            if (mkdirp(dirname(dest_filename)))
            {
                result = bsa_extract_file(archive, filename, dest_filename);
                if (result.code < 0)
                    printf("ba2 %ls\n", result.text);
            }
            else
            {
                printf("ba2 could not create: %ls\n", dirname(dest_filename).c_str());
            }
        }

        bsa_entry_list_free(entries);

        bsa_close(archive);
    }

    {
        bsa_entry_list_t entries = bsa_entry_list_create();
        bsa_entry_list_add(entries, L"textures\\grass\\test.dds");
        bsa_entry_list_add(entries, L"textures\\grass\\test2.dds");

        bsa_create_archive(archive, L"test_write.ba2", baFO4dds, entries);
        bsa_add_file_from_disk_root(archive, L"test_ba2\\", L"test_ba2\\textures\\grass\\test.dds");
        result = bsa_add_file_from_disk(archive, L"textures\\grass\\test2.dds", L"test_ba2\\textures\\grass\\test.dds");
        if (result.code < 0)
            printf("ba2 %ls\n", result.text);
        bsa_save(archive);
        bsa_close(archive);

        bsa_entry_list_free(entries);
    }

    /*
  {
    result = bsa_load_from_file(archive, L"test_large.ba2");
    if (result.code < 0)
      printf("ba2 large %ls\n", result.text);

    bsa_entry_list_t entries = bsa_entry_list_create();
    bsa_get_resource_list(archive, entries, L"");

    for (size_t index = 0; index < bsa_entry_list_count(entries); index++) {
      wchar_t filename[2048];
      wchar_t dest_filename[2048] = L"test_ba2_large\\";

      bsa_entry_list_get(entries, index, 2048, filename);
      wcscat_s(dest_filename, 2048, filename);

      printf("ba2 file large: %ls %ls\n", filename, dest_filename);

      if (mkdirp(dirname(dest_filename))) {
        result = bsa_extract_file(archive, filename, dest_filename);
        if (result.code < 0)
          printf("ba2 large %ls\n", result.text);
      }
      else {
        printf("ba2 large could not create: %ls\n", dirname(dest_filename).c_str());
      }
    }
  }
  */

    bsa_free(archive);
}
