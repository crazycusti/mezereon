#include "config.h"
#ifndef NETWORK_H
#define NETWORK_H

#include "drivers/ne2000.h"

void network_init();
void network_poll();

#endif // NETWORK_H
