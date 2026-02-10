#pragma once

#include "QLibbsarch.h"

class BSArchiveEntries
{
public:
    BSArchiveEntries();
    explicit BSArchiveEntries(const QStringList& QSLEntries);
    explicit BSArchiveEntries(const bsa_entry_list_t& entries);
    ~BSArchiveEntries();

    void add(const QString& filepath);
    void free();
    void reset();
    uint32_t count() ;
    QStringList list();

    bsa_entry_list_t getEntries() const;

private:
    bsa_entry_list_t _entries;
};
