#pragma once

#include <libbsarch/libbsarch.h>
#include <string>
#include <stdexcept>
#include <QDebug>
#include <QDir>
#include <QStringList>

namespace QLibBsarch
{
	constexpr bool enableDebugLog = true;

#define LOG_LIBBSARCH \
	if constexpr (QLibBsarch::enableDebugLog) \
	qDebug() << "[QLIBBSARCH] " << __FUNCTION__ << ' '

#define PREPARE_PATH_LIBBSARCH(qstring) reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(qstring).utf16())

	inline const std::string wcharToString(const wchar_t *text) { return QString::fromWCharArray(text).toStdString(); }

	inline void checkResult(const bsa_result_message_s &result)
	{
		if (result.code == BSA_RESULT_EXCEPTION)
		{
			const std::string &error = QLibBsarch::wcharToString(result.text);
			LOG_LIBBSARCH << QString::fromStdString(error);
			throw std::runtime_error(error);
		}
	}

	inline void checkResult(const bsa_result_message_buffer_s &result)
	{
		if (result.message.code == BSA_RESULT_EXCEPTION)
		{
			const std::string &error = QLibBsarch::wcharToString(result.message.text);
			LOG_LIBBSARCH << QString::fromStdString(error);
			throw std::runtime_error(error);
		}
	}

} // namespace QLibBsarch
