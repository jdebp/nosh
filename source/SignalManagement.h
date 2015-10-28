/* COPYING ******************************************************************
For copyright and licensing terms, see the file named COPYING.
// **************************************************************************
*/

#if !defined(INCLUDE_SIGNAL_MANAGEMENT_H)
#define INCLUDE_SIGNAL_MANAGEMENT_H

#include <csignal>

class ReserveSignalsForKQueue {
public:
	ReserveSignalsForKQueue(int, ...);
	~ReserveSignalsForKQueue();
protected:
	sigset_t original;
};
class PreventDefaultForFatalSignals {
public:
	PreventDefaultForFatalSignals(int, ...);
	~PreventDefaultForFatalSignals();
protected:
	sigset_t signals;
};

#endif
