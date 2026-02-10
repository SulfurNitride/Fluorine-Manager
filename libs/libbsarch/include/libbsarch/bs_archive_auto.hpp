/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#pragma once

#include "bs_archive.h"

namespace libbsarch {
/*!
 * \brief The bs_archive_auto class is a convenience class. It manages both bs_archive and bs_archive_entries.
 * It is recommended when packing from disk/extracting to disk
 * Otherwise, if using memory, the use of these two separate classes is recommended
 */
class bs_archive_auto : public bs_archive
{
protected:
    bs_archive_entries entries_;

public:
    bs_archive_auto() = default;
    bs_archive_auto(const bsa_archive_type_t &type);

    void add_file_from_disk(const disk_blob &blob) override;
    void add_file_from_disk(const std::vector<disk_blob> &blobs);
    void add_file_from_memory(const convertible_string &path_in_archive, const memory_blob &memory_data) override;
    void add_file(const bs_packed_file &file);

    void extract_all(const convertible_string &directory, bool overwrite_current_files);

    void load_from_disk(const convertible_string &archive_path) override;
    void save_to_disk(const convertible_string &archive_path);

private:
    using bs_archive::create; //Use save_to_disk instead
    using bs_archive::save;   //Use save_to_disk instead
    std::vector<bs_packed_file> files_;
};
} // namespace libbsarch
