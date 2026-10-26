/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Local safety overlay; the upstream sources are downloaded separately. */
#ifndef UGREENCTL_N76_IDENTITY_H
#define UGREENCTL_N76_IDENTITY_H

#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/string.h>

#ifndef UGREENCTL_N76_MODEL
#error "The source preparation step must select one exact DMI product name"
#endif

/* SMBus helpers return signed values: a failed read must remain an errno. */
static int mcu_read16(struct i2c_client *client, u8 reg)
{
	int high = i2c_smbus_read_byte_data(client, reg);
	int low;

	if (high < 0)
		return high;
	low = i2c_smbus_read_byte_data(client, reg + 1);
	if (low < 0)
		return low;
	return (high << 8) | low;
}

static int mcu_read8(struct i2c_client *client, u8 reg)
{
	udelay(1000);
	return i2c_smbus_read_byte_data(client, reg);
}

/* Called by probe, including explicit/manual I2C binding, before any write. */
static int mcu_verify_identity(struct i2c_client *client)
{
	const char *product = dmi_get_system_info(DMI_PRODUCT_NAME);
	int chipid;
	int version;

	if (!product || strcmp(product, UGREENCTL_N76_MODEL))
		return -ENODEV;
	if ((client->flags & I2C_CLIENT_TEN) ||
	    (client->addr != I2C_ADDR && client->addr != I2C_ADDR2))
		return -ENODEV;
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;
	chipid = mcu_read16(client, MCU_CHIP_ID);
	if (chipid < 0)
		return chipid;
	if (chipid != 0xa5b5)
		return -ENODEV;
	version = mcu_read8(client, MCU_FW_VER);
	if (version < 0)
		return version;
	dev_info(&client->dev, "Verified %s N76 MCU, version v1.0.%d\n",
		 UGREENCTL_N76_MODEL, version);
	return 0;
}

#endif
