#pragma once

#include "libbsarch.hpp"
#include <vector>

namespace libbsarch {
class bs_archive_entries
{
public:
    bs_archive_entries();
    explicit bs_archive_entries(const std::vector<convertible_string> &entries);
    explicit bs_archive_entries(const bsa_entry_list_t &entries);
    ~bs_archive_entries();

    void add(const convertible_string &filepath);
    uint32_t count();
    std::vector<convertible_string> list();

    bsa_entry_list_t getEntries() const;

protected:
    bsa_entry_list_t _entries;
};
} // namespace libbsarch
