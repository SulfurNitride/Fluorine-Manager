#ifndef IEMITSVALUECHANGED_H
#define IEMITSVALUECHANGED_H

#include <QObject>

namespace BsaPacker
{
	class IEmitsValueChanged : public QObject
	{
		Q_OBJECT

	public:
		virtual ~IEmitsValueChanged() = default;

	public Q_SLOTS:
		virtual void cancel() = 0;

	Q_SIGNALS:
		void valueChanged(int);
	};
}  // namespace BsaPacker

Q_DECLARE_INTERFACE(BsaPacker::IEmitsValueChanged, "IEmitsValueChanged");

#endif // IEMITSVALUECHANGED_H
