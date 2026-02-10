/* Copyright (C) 2019 G'k
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "bs_archive_auto.hpp"
#include <filesystem>
namespace libbsarch {
bs_archive_auto::bs_archive_auto(const bsa_archive_type_t &type)
    : bs_archive(type)
{}

void bs_archive_auto::add_file_from_disk(const disk_blob &blob)
{
    files_.emplace_back(blob);
}

void bs_archive_auto::add_file_from_disk(const std::vector<disk_blob> &blobs)
{
    files_.reserve(blobs.size());
    files_.insert(files_.end(), blobs.cbegin(), blobs.cend());
}

void bs_archive_auto::add_file_from_memory(const convertible_string &path_in_archive, const memory_blob &memory_data)
{
    files_.emplace_back(bs_packed_file(path_in_archive, memory_data));
}

void bs_archive_auto::add_file(const bs_packed_file &file)
{
    files_.emplace_back(file);
}

void bs_archive_auto::extract_all(const convertible_string &directory, bool overwrite_current_files)
{
    namespace fs = std::filesystem;
    for (const auto &file : list_files())
    {
        convertible_string path_string = std::string(directory) + "/" + std::string(file);
        fs::path current_file(path_string);
        fs::path current_path = fs::path(current_file).remove_filename();

        fs::create_directories(current_path);
        if (fs::exists(current_file) && overwrite_current_files)
        {
            fs::remove(current_file);
            extract_to_disk(file, current_file.string());
        }
        else if (!fs::exists(current_file))
            extract_to_disk(file, current_file.string());
    }
}

void bs_archive_auto::load_from_disk(const convertible_string &archive_path)
{
    bs_archive::load_from_disk(archive_path);
    for (const auto &file : list_files())
        entries_.add(file);
}

void bs_archive_auto::save_to_disk(const convertible_string &archive_path)
{
    for (const auto &file : files_)
        entries_.add(file.path_in_archive);

    bs_archive::create(archive_path, entries_);

    for (const auto &file : files_)
    {
        switch (file.data.index())
        {
            case 0: {
                const auto &blob = std::get<memory_blob>(file.data);
                bs_archive::add_file_from_memory(file.path_in_archive, blob);
                break;
            }
            case 1: {
                const auto &file_path = std::get<convertible_string>(file.data);
                const auto &blob = disk_blob(file.path_in_archive, file_path, bool());
                bs_archive::add_file_from_disk(blob);
            }
        }
    }
    bs_archive::save();
}

} // namespace libbsarch
