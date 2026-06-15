#ifndef NOTIFIER_H
#define NOTIFIER_H

#include "storage_mgr.h"

// Define the type if needed, but we will simplify the signature 
// to match the dispatcher's actual requirements.
void notifier_init(void);
void notifier_send(int zone_id); 
void notifier_cancel_alarm(void);

#endif
