#include "bs_archive_entries.h"

namespace libbsarch {
bs_archive_entries::bs_archive_entries()
    : _entries(bsa_entry_list_create())
{}

bs_archive_entries::bs_archive_entries(const std::vector<convertible_string> &entries)
    : _entries(bsa_entry_list_create())
{
    for (const auto &entry : entries)
        add(entry);
}

bs_archive_entries::bs_archive_entries(const bsa_entry_list_t &entries)
    : _entries(entries)
{}

bs_archive_entries::~bs_archive_entries()
{
    bsa_entry_list_free(_entries);
}

void bs_archive_entries::add(const convertible_string &filepath)
{
    debug_log() << "Adding to entries: " << filepath;
    const auto &result = bsa_entry_list_add(_entries, filepath);
    libbsarch::checkResult(result);
}

uint32_t bs_archive_entries::count()
{
    return bsa_entry_list_count(_entries);
}

std::vector<convertible_string> bs_archive_entries::list()
{
    std::vector<convertible_string> list;
    for (uint32_t i = 0; i < count(); ++i)
    {
        wchar_t buffer[max_string_buffer_size];
        bsa_entry_list_get(_entries, i, max_string_buffer_size, buffer);
        list.push_back(buffer);
    }
    return list;
}

bsa_entry_list_t bs_archive_entries::getEntries() const
{
    return _entries;
}
} // namespace libbsarch
