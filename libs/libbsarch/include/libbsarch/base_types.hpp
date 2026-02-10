/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#pragma once

#include "libbsarch.h"
#include "utils/convertible_string.hpp"
#include <variant>

namespace libbsarch {
struct memory_blob : public bsa_result_buffer_t
{
    memory_blob(uint32_t buffer_size, bsa_buffer_t buffer_data, bsa_archive_t parent)
        : bsa_result_buffer_t{buffer_size, buffer_data}
        , parent_(parent)
    {}

    memory_blob(bsa_result_buffer_t buffer, bsa_archive_t parent)
        : bsa_result_buffer_t(buffer)
        , parent_(parent)
    {}

    bsa_archive_t parent_;

    memory_blob(memory_blob const &) = default;
    ~memory_blob() { bsa_file_data_free(parent_, *this); }
};

struct disk_blob
{
    disk_blob(const convertible_string &root_dir, const convertible_string &absolute_path)
        : path_in_archive(absolute_path)
        , path_on_disk(absolute_path)
    {
        path_in_archive.remove_substring(root_dir);
    }

    disk_blob(const convertible_string &path_in_archive,
              const convertible_string &absolute_path,
              [[maybe_unused]] bool decoy_parameter)
        : path_in_archive(path_in_archive)
        , path_on_disk(absolute_path)
    {}

    convertible_string path_in_archive;
    convertible_string path_on_disk;
};

struct bs_packed_file
{
    bs_packed_file(const disk_blob &blob)
        : path_in_archive(blob.path_in_archive)
        , data(blob.path_on_disk)
    {}

    bs_packed_file(convertible_string path_in_archive, memory_blob blob)
        : path_in_archive(std::move(path_in_archive))
        , data(blob)
    {}

    convertible_string path_in_archive;
    std::variant<memory_blob, convertible_string> data;
};

} // namespace libbsarch
