#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/libata.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/synolib.h>
#include <linux/synobios.h>

#define HDDBP_TCA9555_PORT0_PRESENT 0x0
#define HDDBP_TCA9555_PORT1_PRESENT 0x1
#define HDDBP_TCA9555_PORT0_ENABLE 0x2
#define HDDBP_TCA9555_PORT1_ENABLE 0x3
#define HDDBP_TCA9555_PORT0_CONFIG 0x6
#define HDDBP_TCA9555_PORT1_CONFIG 0x7

#define HDDBP_CPLD_ENABLE 0x10
#define HDDBP_CPLD_PRESENT 0x20

#define HDDBP_MICROP_PORT0_PRESENT 0x0D
#define HDDBP_MICROP_PORT1_PRESENT 0x0E
#define HDDBP_MICROP_PORT2_PRESENT 0x0F
#define HDDBP_MICROP_PORT0_ENABLE 0x1D
#define HDDBP_MICROP_PORT1_ENABLE 0x1E
#define HDDBP_MICROP_PORT2_ENABLE 0x1F

static struct mutex smbus_hdd_powerctl_mutex_spin;
static DEFINE_MUTEX(smbus_hdd_powerctl_mutex_spin);
extern char gSynoSmbusHddType[16];
extern SYNO_SMBUS_HDD_POWERCTL SynoSmbusHddPowerCtl; 

bool gblIsTca9555Init = false;
bool gbIsTca9555Enabled = false;
static int syno_tca9555_init(int adapter, int address) {
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;

	mutex_lock(&smbus_hdd_powerctl_mutex_spin);
	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	data.byte = 0xaa;
	/*0xaa => clear port0 bit0, 2, 4, 6 as output, bit 1, 3, 5, 7 as input*/
	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_WRITE, HDDBP_TCA9555_PORT0_CONFIG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (iRet < 0) {
		printk(KERN_ERR "i2c_smbus_xfer error: failed to write i2c reg:0x%x\n", HDDBP_TCA9555_PORT0_CONFIG);
		goto END;
	}
	/*0xaa => clear port1 bit0, 2, 4, 6 as output, bit 1, 3, 5, 7 as input*/
	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_WRITE, HDDBP_TCA9555_PORT1_CONFIG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (iRet < 0) {
		printk(KERN_ERR "i2c_smbus_xfer error: failed to write i2c reg:0x%x\n", HDDBP_TCA9555_PORT1_CONFIG);
		goto END;
	}

	if (iRet >= 0) {
		gblIsTca9555Init = true;
	}
END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}
	mutex_unlock(&smbus_hdd_powerctl_mutex_spin);
	return iRet;
}
/* syno_smbus_hdd_present_read
 * Query HDD present check by SMBus.
 * output: 1 - present, 0 - not present.
 */
int syno_tca9555_hdd_present_read(int adapter, int address, int index)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;
	unsigned int iI2c_REG = 0;
	unsigned int iBitToAccess = 0;

	if(!gblIsTca9555Init) {
		syno_tca9555_init(adapter, address);
	}

	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	if (index <= 4) {
		iI2c_REG = HDDBP_TCA9555_PORT0_PRESENT;
		//disk 0~3 is on port0
		//index is start from 1, so we need -1
		//present is bit 1, 3, 5, 7. so need *2 and + 1
		iBitToAccess = (((index - 1) * 2) + 1);
	} else {
		iI2c_REG = HDDBP_TCA9555_PORT1_PRESENT;
		iBitToAccess = ((((index - 4) - 1) * 2) + 1);
	}

	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_READ, iI2c_REG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (iRet < 0) {
		printk(KERN_ERR "i2c_smbus_xfer error: failed to read i2c reg:0x%x\n", iI2c_REG);
		goto END;
	}

	//0 means on
	iRet = (data.byte >> iBitToAccess) & 1;
END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}
	/*
	 * By default present pin is low active, which means 0 is present.
	 * Also return not present when error occur.
	 */
	return !iRet;
}

int syno_tca9555_hdd_enable_read(int adapter, int address, int index)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;
	unsigned int iI2c_REG = 0;
	unsigned int iBitToAccess = 0;

	if(!gblIsTca9555Init) {
		syno_tca9555_init(adapter, address);
	}

	mutex_lock(&smbus_hdd_powerctl_mutex_spin);
	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	if (4 >= index) {
		iI2c_REG = HDDBP_TCA9555_PORT0_ENABLE;
		//disk 0~3 is on port0
		//index is start from 1, so we need -1
		//enable is bit 0, 2, 4, 6. so need *2
		iBitToAccess = (index - 1) * 2;
	} else {
		//index 5 is the bit 1 of port0, so we have to minus 4
		iI2c_REG = HDDBP_TCA9555_PORT1_ENABLE;
		iBitToAccess = ((index - 4) - 1) * 2;
	}

	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_READ, iI2c_REG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (0 > iRet) {
		printk(KERN_ERR "I2C read fail\n");
		goto END;
	}

	//bit set = disable, clear = enable
	iRet = (data.byte >> iBitToAccess) & 1;

END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}
	mutex_unlock(&smbus_hdd_powerctl_mutex_spin);

	return !iRet;
}

int syno_tca9555_hdd_enable_write(int adapter, int address, int index, int val)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;
	unsigned int iI2c_REG = 0;
	unsigned int iBitToAccess = 0;

	if(!gblIsTca9555Init) {
		syno_tca9555_init(adapter, address);
	}

	mutex_lock(&smbus_hdd_powerctl_mutex_spin);
	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	if (index <= 4) {
		iI2c_REG = HDDBP_TCA9555_PORT0_ENABLE;
		//disk 0~3 is on port0
		//index is start from 1, so we need -1
		//enable is bit 0, 2, 4, 6. so need *2
		iBitToAccess = (index - 1) * 2;
	} else {
		//index 5 is the bit 1 of port0, so we have to minus 4
		iI2c_REG = HDDBP_TCA9555_PORT1_ENABLE;
		iBitToAccess = ((index - 4) - 1) * 2;
	}

	//read current enable data from i2c
	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_READ, iI2c_REG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (0 > iRet) {
		printk(KERN_ERR "I2C read fail\n");
		goto END;
	}


	//bit set = disable, clear = enable
	if (1 == val && ((data.byte >> iBitToAccess) & 1)) {
		//if the bit is set, we need to cleart it to enable
		data.byte &= ~(1 << iBitToAccess);
		iRet = i2c_smbus_xfer(pAdapter, address, 0,
				I2C_SMBUS_WRITE, iI2c_REG,
				I2C_SMBUS_BYTE_DATA, &data);
	} else if (0 == val && !(((data.byte >> iBitToAccess) & 1))){
		data.byte |= 1 << iBitToAccess;
		iRet = i2c_smbus_xfer(pAdapter, address, 0,
				I2C_SMBUS_WRITE, iI2c_REG,
				I2C_SMBUS_BYTE_DATA, &data);
	}
	if (0 > iRet) {
		printk(KERN_ERR "I2C write fail\n");
	}

END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}

	mutex_unlock(&smbus_hdd_powerctl_mutex_spin);
	return iRet;
}

int syno_tca9555_hdd_enable_write_all_once(int adapter, int address)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;

	if(!gblIsTca9555Init) {
		syno_tca9555_init(adapter, address);
	}

	mutex_lock(&smbus_hdd_powerctl_mutex_spin);
	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	if (!gbIsTca9555Enabled) {
		data.byte = 0x00;

		iRet = i2c_smbus_xfer(pAdapter, address, 0,
				I2C_SMBUS_WRITE, HDDBP_TCA9555_PORT0_ENABLE,
				I2C_SMBUS_BYTE_DATA, &data);
		if (0 > iRet) {
			printk(KERN_ERR "TCA9555 I2C write port0 fail\n");
			goto END;
		}

		iRet = i2c_smbus_xfer(pAdapter, address, 0,
				I2C_SMBUS_WRITE, HDDBP_TCA9555_PORT1_ENABLE,
				I2C_SMBUS_BYTE_DATA, &data);
		if (0 > iRet) {
			printk(KERN_ERR "TCA9555 I2C write port1 fail\n");
			goto END;
		}

		gbIsTca9555Enabled = true;
	}

END:
	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}

	mutex_unlock(&smbus_hdd_powerctl_mutex_spin);
	return iRet;
}

/* syno_smbus_hdd_present_read
 * Query HDD present check by SMBus.
 * output: 1 - present, 0 - not present.
 */
int syno_cpld_hdd_present_read(int adapter, int address, int index)
{
    int iRet = -1;
    union i2c_smbus_data data;
    struct i2c_adapter *pAdapter = NULL;

    pAdapter = i2c_get_adapter(adapter);
    if (NULL == pAdapter) {
        printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
        goto END;
    }
    iRet = i2c_smbus_xfer(pAdapter, address, 0,
                I2C_SMBUS_READ, HDDBP_CPLD_PRESENT,
                I2C_SMBUS_BYTE_DATA, &data);
    if (iRet < 0) {
        printk(KERN_ERR "i2c_smbus_xfer error: failed to read i2c reg:0x%x\n", HDDBP_CPLD_PRESENT);
        goto END;
    }

    iRet = (data.byte >> (index -1)) & 1;
END:
    if (pAdapter) {
        i2c_put_adapter(pAdapter);
    }
	/*
	 * By default present pin is low active, which means 0 is present.
	 * Also return not present when error occur.
	 */
    return !iRet;
}

int syno_cpld_hdd_enable_read(int adapter, int address, int index)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;

	mutex_lock(&smbus_hdd_powerctl_mutex_spin);
	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	//read current enable data from i2c
	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_READ, HDDBP_CPLD_ENABLE,
			I2C_SMBUS_BYTE_DATA, &data);
	if (0 > iRet) {
		goto END;
	}
	iRet = (data.byte >> (index -1)) & 1;

END:

	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}
	mutex_unlock(&smbus_hdd_powerctl_mutex_spin);

	return iRet;
}

int syno_microp_hdd_present_read(int adapter, int address, int index)
{
    int iRet = -1;
    union i2c_smbus_data data;
    struct i2c_adapter *pAdapter = NULL;
	unsigned int iI2c_REG = 0;
	unsigned int iBitToAccess = 0;

    pAdapter = i2c_get_adapter(adapter);
    if (NULL == pAdapter) {
        printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
        goto END;
    }

	//disk  1 ~  8 is on port0
	//      9 ~ 16 is on port1
	//     17 ~ 24 is on port2   
	if (8 >= index) {
		iI2c_REG = HDDBP_MICROP_PORT0_PRESENT;
		iBitToAccess = index - 1;
	} else if (16 >= index) {
		iI2c_REG = HDDBP_MICROP_PORT1_PRESENT;
		iBitToAccess = index - 9;
	} else {
		iI2c_REG = HDDBP_MICROP_PORT2_PRESENT;
		iBitToAccess = index - 17;
	}

	// read present data from i2c
    iRet = i2c_smbus_xfer(pAdapter, address, 0,
                I2C_SMBUS_READ, iI2c_REG,
                I2C_SMBUS_BYTE_DATA, &data);
    if (iRet < 0) {
        printk(KERN_ERR "i2c_smbus_xfer error: failed to read i2c reg:0x%x\n", iI2c_REG);
        goto END;
    }

    iRet = (data.byte >> iBitToAccess) & 1;
END:
    if (pAdapter) {
        i2c_put_adapter(pAdapter);
    }
	/*
	 * By default present pin is low active, which means 0 is present.
	 * Also return not present when error occur.
	 */
    return !iRet;
}

int syno_microp_hdd_enable_read(int adapter, int address, int index)
{
	int iRet = -1;
	union i2c_smbus_data data;
	struct i2c_adapter *pAdapter = NULL;
	unsigned int iI2c_REG = 0;
	unsigned int iBitToAccess = 0;

	pAdapter = i2c_get_adapter(adapter);
	if (NULL == pAdapter) {
		printk(KERN_ERR "I2C initial error: failed to get i2c adapter\n");
		goto END;
	}

	//disk  1 ~  8 is on port0
	//      9 ~ 16 is on port1
	//     17 ~ 24 is on port2   
	if (8 >= index) {
		iI2c_REG = HDDBP_MICROP_PORT0_ENABLE;
		iBitToAccess = index - 1;
	} else if (16 >= index) {
		iI2c_REG = HDDBP_MICROP_PORT1_ENABLE;
		iBitToAccess = index - 9;
	} else {
		iI2c_REG = HDDBP_MICROP_PORT2_ENABLE;
		iBitToAccess = index - 17;
	}

	//read current enable data from i2c
	iRet = i2c_smbus_xfer(pAdapter, address, 0,
			I2C_SMBUS_READ, iI2c_REG,
			I2C_SMBUS_BYTE_DATA, &data);
	if (0 > iRet) {
		goto END;
	}
	iRet = (data.byte >> iBitToAccess) & 1;

END:

	if (pAdapter) {
		i2c_put_adapter(pAdapter);
	}

	return iRet;
}

void syno_smbus_hdd_powerctl_init(void){
	if(0 == strncmp(gSynoSmbusHddType, "tca9555", strlen("tca9555"))){
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write=syno_tca9555_hdd_enable_write;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read=syno_tca9555_hdd_enable_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read=syno_tca9555_hdd_present_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write_all_once=syno_tca9555_hdd_enable_write_all_once;
	} else if (0 == strncmp(gSynoSmbusHddType, "cpld", strlen("cpld"))){
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write=NULL;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read=syno_cpld_hdd_enable_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read=syno_cpld_hdd_present_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write_all_once=NULL;
	} else if (0 == strncmp(gSynoSmbusHddType, "microp", strlen("microp"))){
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write=NULL;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_read=syno_microp_hdd_enable_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_present_read=syno_microp_hdd_present_read;
		SynoSmbusHddPowerCtl.syno_smbus_hdd_enable_write_all_once=NULL;
	}
	SynoSmbusHddPowerCtl.bl_init = 1;
}
EXPORT_SYMBOL(syno_smbus_hdd_powerctl_init);
