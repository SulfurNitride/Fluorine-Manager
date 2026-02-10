#include <qlibbsarch/BSArchiveEntries.h>

BSArchiveEntries::BSArchiveEntries()
	: _entries(bsa_entry_list_create())
{
}

BSArchiveEntries::BSArchiveEntries(const QStringList &QSLEntries)
	: _entries(bsa_entry_list_create())
{
	for (const auto &entry : QSLEntries)
		add (entry);
}

BSArchiveEntries::BSArchiveEntries(const bsa_entry_list_t &entries)
	: _entries(entries)
{
}

BSArchiveEntries::~BSArchiveEntries()
{
	free();
}

void BSArchiveEntries::free()
{
	bsa_entry_list_free(_entries);
}

void BSArchiveEntries::reset()
{
	free();
	_entries = bsa_entry_list_create();
}

void BSArchiveEntries::add(const QString &filepath)
{
	LOG_LIBBSARCH << "Adding to entries: " << filepath;

	const auto &result = bsa_entry_list_add(_entries, PREPARE_PATH_LIBBSARCH(filepath));

	QLibBsarch::checkResult(result);
}

uint32_t BSArchiveEntries::count()
{
	return bsa_entry_list_count(_entries);
}

QStringList BSArchiveEntries::list()
{
	QStringList list;
	for(uint32_t i = 0 ; i < count() ; ++i)
	{
		wchar_t buffer[1024];
		bsa_entry_list_get(_entries, i, 1024, buffer);
		list << QString::fromWCharArray(buffer);
	}
	return list;
}

bsa_entry_list_t BSArchiveEntries::getEntries() const
{
	return _entries;
}
