#ifndef PE_TRADER_H
#define PE_TRADER_H

#include "pe_common.h"

#define SIZE 80

typedef struct {
    int id;
    char item[16];
    int quantity;
    int price;
} Order;

#endif
