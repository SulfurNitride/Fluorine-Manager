#include "bs_archive.h"

namespace libbsarch {

bs_archive::bs_archive()
    : archive_(bsa_create())
{}

bs_archive::bs_archive(const bsa_archive_type_t &type)
    : archive_(bsa_create())
    , type_(type)
{}

bs_archive::~bs_archive()
{
    bsa_close(archive_);
    bsa_free(archive_);
}

/* Properties */
convertible_string bs_archive::get_filename()
{
    wchar_t name[max_string_buffer_size];
    bsa_filename_get(archive_, max_string_buffer_size, name);
    return name;
}

bsa_archive_type_t bs_archive::get_type()
{
    return bsa_archive_type_get(archive_);
}

uint32_t bs_archive::get_version()
{
    return bsa_version_get(archive_);
}

convertible_string bs_archive::get_format_name()
{
    wchar_t format_name[max_string_buffer_size];
    bsa_format_name_get(archive_, max_string_buffer_size, format_name);
    return format_name;
}

uint32_t bs_archive::get_file_count()
{
    return bsa_file_count_get(archive_);
}

uint32_t bs_archive::get_archive_flags()
{
    return bsa_archive_flags_get(archive_);
}

void bs_archive::set_archive_flags(uint32_t flags)
{
    bsa_archive_flags_set(archive_, flags);
}

uint32_t bs_archive::get_file_flags()
{
    return bsa_file_flags_get(archive_);
}

void bs_archive::set_file_flags(uint32_t flags)
{
    bsa_file_flags_set(archive_, flags);
}

void bs_archive::set_compressed(bool value)
{
    bsa_compress_set(archive_, value);
}

void bs_archive::set_share_data(bool value)
{
    bsa_share_data_set(archive_, value);
}

bool bs_archive::get_compressed()
{
    return bsa_compress_get(archive_);
}

bool bs_archive::get_share_data()
{
    return bsa_share_data_get(archive_);
}

void bs_archive::load_from_disk(const convertible_string &archive_path)
{
    debug_log() << "Opening archive: " << archive_path;
    const std::wstring str = archive_path;
    const auto &result = bsa_load_from_file(archive_, str.c_str());
    libbsarch::checkResult(result);
}

void bs_archive::create(const convertible_string &archiveName, const bs_archive_entries &entries)
{
    create(archiveName, entries, type_);
}

void bs_archive::create(const convertible_string &archiveName,
                        const bs_archive_entries &entries,
                        const bsa_archive_type_t type)
{
    debug_log() << "Creating archive. Archive name: " << archiveName << '\n'
                << "type: " << type_ << '\n'
                << "entries: " << entries.getEntries();
    bsa_create_archive(archive_, archiveName, type, entries.getEntries());
}

void bs_archive::save() const
{
    debug_log() << "Saving archive: " << archive_;

    const auto &result = bsa_save(archive_);

    libbsarch::checkResult(result);
}

void bs_archive::add_file_from_disk(const disk_blob &blob)
{
    debug_log() << "Adding file from disk root. Path in archive: " << blob.path_in_archive << '\n'
                << "Filepath: " << blob.path_on_disk << '\n'
                << "Archive: " << archive_;

    const auto &result = bsa_add_file_from_disk(archive_, blob.path_in_archive, blob.path_on_disk);

    libbsarch::checkResult(result);
}

void bs_archive::add_file_from_memory(const convertible_string &path_in_archive,
                                      const memory_blob &memory_data) //NOTE UNTESTED
{
    debug_log() << "Adding file from memory. Filename: " << path_in_archive << '\n'
                << "Data size: " << memory_data.size;
    const auto &result = bsa_add_file_from_memory(archive_, path_in_archive, memory_data.size, memory_data.data);
    libbsarch::checkResult(result);
}

bsa_file_record_t bs_archive::find_file_record(const convertible_string &filename)
{
    debug_log() << "Finding file record of: " << filename;
    const auto &result = bsa_find_file_record(archive_, filename);
    return result;
}

memory_blob bs_archive::extract_to_memory(bsa_file_record_t record)
{
    const auto &result = bsa_extract_file_data_by_record(archive_, record);
    libbsarch::checkResult(result);
    return memory_blob(result.buffer, archive_);
}

memory_blob bs_archive::extract_to_memory(const convertible_string &filename)
{
    const auto &result = bsa_extract_file_data_by_filename(archive_, filename);
    libbsarch::checkResult(result);
    return memory_blob(result.buffer, archive_);
}

void bs_archive::extract_to_disk(const convertible_string &filename, const convertible_string &save_as) const
{
    debug_log() << "Extracting: " << filename << " saved as " << save_as;
    const auto &result = bsa_extract_file(archive_, filename, save_as);

    libbsarch::checkResult(result);
}

std::vector<convertible_string> bs_archive::list_files() const
{
    bsa_entry_list_t list = bsa_entry_list_create();
    const auto &result = bsa_get_resource_list(archive_, list, L"");

    libbsarch::checkResult(result);

    return bs_archive_entries(list).list();
}

bsa_archive_t bs_archive::get_archive() const
{
    return archive_;
}
} // namespace libbsarch
