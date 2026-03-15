#ifndef ENCRYPTION_H
#define ENCRYPTION_H

void GetEncryptedData(unsigned long *data, char *encrypted);

/* Provided by BLE module (ble_gatt_server.c) */
extern char encrypted_data[10];
extern unsigned long rnd;

#endif
