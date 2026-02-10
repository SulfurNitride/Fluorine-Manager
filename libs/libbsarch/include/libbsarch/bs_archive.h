#pragma once

#include "bs_archive_entries.h"
#include "libbsarch.hpp"

namespace libbsarch {
class bs_archive
{
public:
    bs_archive();
    bs_archive(const bsa_archive_type_t &type);
    virtual ~bs_archive();

    /* Properties */
    convertible_string get_filename();
    bsa_archive_type_t get_type();
    uint32_t get_version();
    convertible_string get_format_name();
    uint32_t get_file_count();

    uint32_t get_archive_flags();
    void set_archive_flags(uint32_t flags);

    uint32_t get_file_flags();
    void set_file_flags(uint32_t flags);

    void set_compressed(bool value);
    bool get_compressed();

    void set_share_data(bool value);
    bool get_share_data();

    /* Input-Output */
    virtual void load_from_disk(const convertible_string &archive_path);
    virtual void create(const convertible_string &archiveName, const bs_archive_entries &entries);
    virtual void create(const convertible_string &archiveName,
                        const bs_archive_entries &entries,
                        const bsa_archive_type_t type);
    void save() const;

    /* Add to archive */
    virtual void add_file_from_disk(const disk_blob &blob);
    virtual void add_file_from_memory(const convertible_string &path_in_archive, const memory_blob &memory_data);

    /* Extract */
    memory_blob extract_to_memory(bsa_file_record_t record);
    memory_blob extract_to_memory(const convertible_string &filename);
    void extract_to_disk(const convertible_string &filename, const convertible_string &save_as) const;

    /* Selectors */
    bsa_file_record_t find_file_record(const convertible_string &filename);
    std::vector<convertible_string> list_files() const;

    /* Other */

    /*! \brief Sets the DDS callback. It is required to use Fallout 4 DDS archives. You do not have
     *  to manage the lifetime of the provided context
     * \example
    void dds_callback([[maybe_unused]] bsa_archive_t archive,
                                   const wchar_t *file_path,
                                   bsa_dds_info_t *dds_info,
                                   void *context)
    {
        const auto &path = *static_cast<convertible_string *>(context) + '/' + convertible_string(file_path);

        auto image = std::make_unique<DirectX::ScratchImage>();
        DirectX::TexMetadata info;

        const auto hr = LoadFromDDSFile(PREPARE_PATH_LIBBSARCH(path), DirectX::DDS_FLAGS_BAD_DXTN_TAILS, &info, *image);
        if (FAILED(hr))
        {
            //Do not throw here. Exceptions will be ignored
            return;
        }

        dds_info->width = info.width;
        dds_info->height = info.height;
        dds_info->mipmaps = info.mipLevels;
    }
    */
    template<typename context_type>
    void set_dds_callback(bsa_file_dds_info_proc_t dds_function, const context_type &context)
    {
        debug_log() << "Setting DDS callback for archive: " << archive_ << "\nCallback adress: " << &dds_function;

        auto heap_context = new context_type(context);
        callback_contexts_.emplace_back(heap_context);
        bsa_file_dds_info_callback_set(archive_, dds_function, callback_contexts_.back());
    }

    bsa_archive_t get_archive() const;

protected:
    bsa_archive_t archive_;
    bsa_archive_type_t type_;

private:
    std::vector<void *> callback_contexts_;
};
} // namespace libbsarch
