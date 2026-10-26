/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host-side safety test. Build with empty linux/{dmi,errno,i2c,string}.h stubs;
 * the kernel-facing types/functions below intentionally model only reads. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Some libc errno headers obtain these from linux/errno.h, stubbed below. */
#ifndef ENODEV
#define ENODEV 19
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef EREMOTEIO
#define EREMOTEIO 121
#endif

typedef unsigned char u8;
struct i2c_adapter { int unused; };
struct device { int unused; };
struct i2c_client {
	unsigned short flags;
	unsigned short addr;
	struct i2c_adapter *adapter;
	struct device dev;
};

#define UGREENCTL_N76_MODEL "DH2600"
#define DMI_PRODUCT_NAME 1
#define I2C_CLIENT_TEN 0x10
#define I2C_FUNC_SMBUS_BYTE_DATA 0x180000
#define I2C_ADDR 0x31
#define I2C_ADDR2 0x26
#define MCU_CHIP_ID 0x5a
#define MCU_FW_VER 0x5d
#define udelay(usec) ((void)(usec))
#define dev_info(...) ((void)0)

static const char *product;
static int reads;
static int capable;
static int values[3];
static const int registers[] = { 0x5a, 0x5b, 0x5d };

static const char *dmi_get_system_info(int field)
{
	assert(field == DMI_PRODUCT_NAME);
	return product;
}

static int i2c_check_functionality(struct i2c_adapter *adapter, int function)
{
	(void)adapter;
	assert(function == I2C_FUNC_SMBUS_BYTE_DATA);
	return capable;
}

static int i2c_smbus_read_byte_data(struct i2c_client *client, u8 reg)
{
	(void)client;
	assert(reads < 3);
	assert(reg == registers[reads]);
	return values[reads++];
}

#include "ugreenctl_n76_identity.h"

static void reset(struct i2c_client *client)
{
	client->flags = 0;
	client->addr = 0x31;
	product = "DH2600";
	capable = 1;
	reads = 0;
	values[0] = 0xa5;
	values[1] = 0xb5;
	values[2] = 17;
}

int main(void)
{
	struct i2c_client client = { 0 };
	const char *unsupported[] = { NULL, "DH2600 ", "dh2600", "DXP480T Plus", "DX4600" };
	unsigned int i;

	for (i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
		reset(&client);
		product = unsupported[i];
		assert(mcu_verify_identity(&client) == -ENODEV && reads == 0);
	}
	reset(&client);
	client.addr = 0x30;
	assert(mcu_verify_identity(&client) == -ENODEV && reads == 0);
	reset(&client);
	client.flags = I2C_CLIENT_TEN;
	assert(mcu_verify_identity(&client) == -ENODEV && reads == 0);
	reset(&client);
	capable = 0;
	assert(mcu_verify_identity(&client) == -EOPNOTSUPP && reads == 0);
	for (i = 0; i < 3; i++) {
		reset(&client);
		values[i] = -EREMOTEIO;
		assert(mcu_verify_identity(&client) == -EREMOTEIO);
		assert(reads == (int)i + 1);
	}
	reset(&client);
	values[0] = 0;
	assert(mcu_verify_identity(&client) == -ENODEV && reads == 2);
	reset(&client);
	values[1] = 0;
	assert(mcu_verify_identity(&client) == -ENODEV && reads == 2);
	reset(&client);
	assert(mcu_verify_identity(&client) == 0 && reads == 3);
	reset(&client);
	client.addr = 0x26;
	assert(mcu_verify_identity(&client) == 0 && reads == 3);
	puts("N76 identity gate: all assertions passed");
	return 0;
}
