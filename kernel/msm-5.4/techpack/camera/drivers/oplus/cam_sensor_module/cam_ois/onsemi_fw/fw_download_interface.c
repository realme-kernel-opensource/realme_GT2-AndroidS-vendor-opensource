#include <linux/kfifo.h>
#include <asm/arch_timer.h>
#include "fw_download_interface.h"
#include "LC898124/Ois.h"
#include "linux/proc_fs.h"

extern unsigned char SelectDownload(uint8_t GyroSelect, uint8_t ActSelect, uint8_t MasterSlave, uint8_t FWType);
extern uint8_t FlashDownload128( uint8_t ModuleVendor, uint8_t ActVer, uint8_t MasterSlave, uint8_t FWType);

#define MAX_DATA_NUM 64
extern bool chip_version_old;

struct mutex ois_mutex;
struct cam_ois_ctrl_t *ois_ctrl = NULL;
struct cam_ois_ctrl_t *ois_ctrls[CAM_OIS_TYPE_MAX] = {NULL};
enum cam_ois_state_vendor ois_state[CAM_OIS_TYPE_MAX] = {0};
struct proc_dir_entry *face_common_dir = NULL;
struct proc_dir_entry *proc_file_entry = NULL;
struct proc_dir_entry *proc_file_entry_tele = NULL;
//bool is_wirte=false;

#define OIS_REGISTER_SIZE 100
#define OIS_READ_REGISTER_DELAY 10
#define COMMAND_SIZE 255
static struct kobject *cam_ois_kobj;
bool dump_ois_registers = false;
uint32_t ois_registers_124[OIS_REGISTER_SIZE][2] = {
	{0xF010, 0x0000},//Servo On/Off
	{0xF012, 0x0000},//Enable/Disable OIS
	{0xF013, 0x0000},//OIS Mode
	{0xF015, 0x0000},//Select Gyro vendor
	{0x82B8, 0x0000},//Gyro Gain X
	{0x8318, 0x0000},//Gyro Gain Y
	{0x0338, 0x0000},//Gyro Offset X
	{0x033c, 0x0000},//Gyro Offset Y
	{0x01C0, 0x0000},//Hall Offset X
	{0x0214, 0x0000},//Hall Offset Y
	{0x0310, 0x0000},//Gyro Raw Data X
	{0x0314, 0x0000},//Gyro Raw Data Y
	{0x0268, 0x0000},//Hall Raw Data X
	{0x026C, 0x0000},//Hall Raw Data Y
	{0xF100, 0x0000},//OIS status
	{0xF112, 0x0000},//spi status
	{0x8000, 0x0000},
	{0x8004, 0x0000},//fw version
	{0x8880, 0x0000},
	{0x82A0, 0x0000},
	{0x8300, 0x0000},
	{0x02E8, 0x0000},
	{0x030C, 0x0000},
	{0x8884, 0x0000},
	{0x8888, 0x0000},//slow pan/tilt parameter.
	{0x84F8, 0x0000},//pan/tilt parameter
	{0x035C, 0x0000},
	{0x8360, 0x0000},
	{0x889C, 0x0000},
        {0x82E8, 0x0000},
        {0x82E0, 0x0000},
        {0x01c4, 0x0000},
        {0x0218, 0x0000},
	{0x0000, 0x0000},
};

uint32_t ois_registers_128[OIS_REGISTER_SIZE][2] = {
	{0xF010, 0x0000},//Servo On/Off
	{0xF018, 0x0000},//Damping detection On/Off
	{0xF012, 0x0000},//Enable/Disable OIS
	{0xF013, 0x0000},//OIS Mode
	{0xF015, 0x0000},//Select Gyro vendor
	{0x82B8, 0x0000},//Gyro Gain X
	{0x8318, 0x0000},//Gyro Gain Y
	{0x0240, 0x0000},//Gyro Offset X
	{0x0244, 0x0000},//Gyro Offset Y
	{0x00D8, 0x0000},//Hall Offset X
	{0x0128, 0x0000},//Hall Offset Y
	{0x0220, 0x0000},//Gyro Raw Data X
	{0x0224, 0x0000},//Gyro Raw Data Y
	{0x0178, 0x0000},//Hall Raw Data X
	{0x017C, 0x0000},//Hall Raw Data Y
	{0xF01D, 0x0000},//SPI IF read access command
	{0xF01E, 0x0000},//SPI IF Write access command
	{0xF100, 0x0000},//OIS status
	{0x01E0, 0x0000},
	{0x0204, 0x0000},
	{0x82B4, 0x0000},
	{0x8314, 0x0000},
	{0x0248, 0x0000},
	{0x024C, 0x0000},//Gyro limit X/Y
	{0x82A0, 0x0000},
	{0x8300, 0x0000},//Gyro coeff3 X/Y
	{0x82BC, 0x0000},
	{0x831C, 0x0000},//Gyro coeff4 X/Y
	{0x0258, 0x0000},//Gyro switch
	{0x01F8, 0x0000},
	{0x021C, 0x0000},//Gyro out X/Y
	{0x06CC, 0x0000},//spi status
	{0x8000, 0x0000},
	{0x8004, 0x0000},//fw version
	{0x0220, 0x0000},
	{0x0224, 0x0000},
	{0x01F8, 0x0000},
	{0x021C, 0x0000},
	{0x82B4, 0x0000},
	{0x8314, 0x0000},
	{0x0000, 0x0000},
};

int RamWrite32A(    uint32_t addr, uint32_t data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
		.delay = 0x00,
	};

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,write 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

int RamRead32A(    uint32_t addr, uint32_t* data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,read 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

static ssize_t ois_read(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
    return 1;
}

static ssize_t ois_write(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	char data[COMMAND_SIZE] = {0};
	char* const delim = " ";
	int iIndex = 0;
	char *token = NULL, *cur = NULL;
	uint32_t addr =0, value = 0;
	int result = 0;

	if(puser_buf) {
		if (copy_from_user(&data, puser_buf, count)) {
			CAM_ERR(CAM_OIS, "copy from user buffer error");
			return -EFAULT;
		}
	}

	cur = data;
	while ((token = strsep(&cur, delim))) {
		//CAM_ERR(CAM_OIS, "string = %s iIndex = %d, count = %d", token, iIndex, count);
                int ret=0;
		if (iIndex  == 0) {
		    ret = kstrtouint(token, 16, &addr);
		} else if (iIndex == 1) {
		    ret = kstrtouint(token, 16, &value);
		}
		if(ret < 0) CAM_ERR(CAM_OIS,"String conversion to unsigned int failed");
                iIndex++;
	}
	if (ois_ctrls[CAM_OIS_MASTER] && addr != 0) {
		result = RamWrite32A_oneplus(ois_ctrls[CAM_OIS_MASTER], addr, value);
		if (result < 0) {
			CAM_ERR(CAM_OIS, "write addr = 0x%x, value = 0x%x fail", addr, value);
		} else {
			CAM_INFO(CAM_OIS, "write addr = 0x%x, value = 0x%x success", addr, value);
		}
	}
	return count;
}

static ssize_t ois_read_tele(struct file *p_file,
	char __user *puser_buf, size_t count, loff_t *p_offset)
{
    return 1;
}

static ssize_t ois_write_tele(struct file *p_file,
	const char __user *puser_buf,
	size_t count, loff_t *p_offset)
{
	char data[COMMAND_SIZE] = {0};
	char* const delim = " ";
	int iIndex = 0;
	char *token = NULL, *cur = NULL;
	uint32_t addr =0, value = 0;
	int result = 0;

	if(puser_buf) {
		if (copy_from_user(&data, puser_buf, count)) {
			CAM_ERR(CAM_OIS, "copy from user buffer error");
			return -EFAULT;
		}
	}

	cur = data;
	while ((token = strsep(&cur, delim))) {
		//CAM_ERR(CAM_OIS, "string = %s iIndex = %d, count = %d", token, iIndex, count);
		int ret=0;
		if (iIndex  == 0) {
			ret = kstrtouint(token, 16, &addr);
		} else if (iIndex == 1) {
			ret = kstrtouint(token, 16, &value);
		}
		if(ret < 0) CAM_ERR(CAM_OIS,"String conversion to unsigned int failed");
		iIndex++;
	}
	if (ois_ctrls[CAM_OIS_SLAVE] && addr != 0) {
		result = RamWrite32A_oneplus(ois_ctrls[CAM_OIS_SLAVE], addr, value);
		if (result < 0) {
			CAM_ERR(CAM_OIS, "write addr = 0x%x, value = 0x%x fail", addr, value);
		} else {
			CAM_INFO(CAM_OIS, "write addr = 0x%x, value = 0x%x success", addr, value);
		}
	}
	return count;
}



static const struct file_operations proc_file_fops = {
	.owner = THIS_MODULE,
	.read  = ois_read,
	.write = ois_write,
};
static const struct file_operations proc_file_fops_tele = {
	.owner = THIS_MODULE,
	.read  = ois_read_tele,
	.write = ois_write_tele,
};

int ois_start_read(void *arg, bool start)
{
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	mutex_lock(&(o_ctrl->ois_read_mutex));
	o_ctrl->ois_read_thread_start_to_read = start;
	mutex_unlock(&(o_ctrl->ois_read_mutex));

	msleep(OIS_READ_REGISTER_DELAY);

	return 0;
}

int ois_read_thread(void *arg)
{
	int rc = 0;
	int i;
	char buf[OIS_REGISTER_SIZE*2*4] = {0};
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "failed: o_ctrl %pK", o_ctrl);
		return -EINVAL;
	}

	CAM_ERR(CAM_OIS, "ois_read_thread created");

	while (!kthread_should_stop()) {
		memset(buf, 0, sizeof(buf));
		mutex_lock(&(o_ctrl->ois_read_mutex));
		if (o_ctrl->ois_read_thread_start_to_read) {
			if (strstr(o_ctrl->ois_name, "124")) {
				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_124[i][0]) {
						ois_registers_124[i][1] = 0;
						camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_registers_124[i][0], (uint32_t *)&ois_registers_124[i][1],
						                   CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
					}
				}

				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_124[i][0]) {
						snprintf(buf+strlen(buf), sizeof(buf), "0x%x,0x%x,", ois_registers_124[i][0], ois_registers_124[i][1]);
					}
				}
			} else if (strstr(o_ctrl->ois_name, "128")) {
				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_128[i][0]) {
						ois_registers_128[i][1] = 0;
						camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)ois_registers_128[i][0], (uint32_t *)&ois_registers_128[i][1],
						                   CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
					}
				}

				for (i = 0; i < OIS_REGISTER_SIZE; i++) {
					if (ois_registers_128[i][0]) {
						snprintf(buf+strlen(buf), sizeof(buf), "0x%x,0x%x,", ois_registers_128[i][0], ois_registers_128[i][1]);
					}
				}
			}
			CAM_ERR(CAM_OIS, "%s OIS register data: %s", o_ctrl->ois_name, buf);
		}
		mutex_unlock(&(o_ctrl->ois_read_mutex));

		msleep(OIS_READ_REGISTER_DELAY);
	}

	CAM_ERR(CAM_OIS, "ois_read_thread exist");

	return rc;
}

int ois_start_read_thread(void *arg, bool start)
{
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
		return -1;
	}

	if (start) {
		if (o_ctrl->ois_read_thread) {
			CAM_ERR(CAM_OIS, "ois_read_thread is already created, no need to create again.");
		} else {
			o_ctrl->ois_read_thread = kthread_run(ois_read_thread, o_ctrl, o_ctrl->ois_name);
			if (!o_ctrl->ois_read_thread) {
				CAM_ERR(CAM_OIS, "create ois read thread failed");
				mutex_unlock(&(o_ctrl->ois_read_mutex));
				return -2;
			}
		}
	} else {
		if (o_ctrl->ois_read_thread) {
			mutex_lock(&(o_ctrl->ois_read_mutex));
			o_ctrl->ois_read_thread_start_to_read = 0;
			mutex_unlock(&(o_ctrl->ois_read_mutex));
			kthread_stop(o_ctrl->ois_read_thread);
			o_ctrl->ois_read_thread = NULL;
		} else {
			CAM_ERR(CAM_OIS, "ois_read_thread is already stopped, no need to stop again.");
		}
	}

	return 0;
}

#define OIS_ATTR(_name, _mode, _show, _store) \
    struct kobj_attribute ois_attr_##_name = __ATTR(_name, _mode, _show, _store)

static ssize_t dump_registers_store(struct kobject *kobj,
                             struct kobj_attribute * attr,
                             const char * buf,
                             size_t count)
{
        unsigned int if_start_thread;
        CAM_WARN(CAM_OIS, "%s", buf);
        if (sscanf(buf, "%u", &if_start_thread) != 1) {
                return -1;
        }
        if(if_start_thread) {
                dump_ois_registers = true;
        } else {
                dump_ois_registers = false;
        }

        return count;
}

static OIS_ATTR(dump_registers, 0644, NULL, dump_registers_store);
static struct attribute *ois_node_attrs[] = {
        &ois_attr_dump_registers.attr,
        NULL,
};
static const struct attribute_group ois_common_group = {
        .attrs = ois_node_attrs,
};
static const struct attribute_group *ois_groups[] = {
        &ois_common_group,
        NULL,
};

void WitTim( uint16_t time)
{
	msleep(time);
}

void CntRd(uint32_t addr, void *data, uint16_t size)
{
	int i = 0;
	int32_t rc = 0;
	int retry = 3;
	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read_seq(&(o_ctrl->io_master_info), addr, (uint8_t *)data,
		                            CAMERA_SENSOR_I2C_TYPE_WORD,
		                            CAMERA_SENSOR_I2C_TYPE_BYTE,
		                            size);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,Continue read failed, rc:%d, retry:%d",o_ctrl->ois_type, rc, i+1);
		} else {
			break;
		}
	}
}

void CntWrt(  void *register_data, uint16_t size)
{
	uint8_t *data = (uint8_t *)register_data;
	int32_t rc = 0;
	int i = 0;
	int reg_data_cnt = size - 1;
	int continue_cnt = 0;
	int retry = 3;
	static struct cam_sensor_i2c_reg_array *i2c_write_setting_gl = NULL;

	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

	struct cam_sensor_i2c_reg_setting i2c_write;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return;
	}

	if (i2c_write_setting_gl == NULL) {
		i2c_write_setting_gl = (struct cam_sensor_i2c_reg_array *)kzalloc(
		                           sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2, GFP_KERNEL);
		if(!i2c_write_setting_gl) {
			CAM_ERR(CAM_OIS, "Alloc i2c_write_setting_gl failed");
			return;
		}
	}

	memset(i2c_write_setting_gl, 0, sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2);

	for(i = 0; i< reg_data_cnt; i++) {
		if (i == 0) {
			i2c_write_setting_gl[continue_cnt].reg_addr = data[0];
			i2c_write_setting_gl[continue_cnt].reg_data = data[1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		} else {
			i2c_write_setting_gl[continue_cnt].reg_data = data[i+1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		}
		continue_cnt++;
	}
	i2c_write.reg_setting = i2c_write_setting_gl;
	i2c_write.size = continue_cnt;
	i2c_write.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_write.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
	i2c_write.delay = 0x00;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
		                                    &i2c_write, 1);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,Continue write failed, rc:%d, retry:%d",o_ctrl->ois_type, rc, i+1);
		} else {
			break;
		}
	}
}


// int RamWrite32A(    uint32_t addr, uint32_t data)
// {
// 	int32_t rc = 0;
// 	int retry = 3;
// 	int i;

// 	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

// 	struct cam_sensor_i2c_reg_array i2c_write_setting = {
// 		.reg_addr = addr,
// 		.reg_data = data,
// 		.delay = 0x00,
// 		.data_mask = 0x00,
// 	};
// 	struct cam_sensor_i2c_reg_setting i2c_write = {
// 		.reg_setting = &i2c_write_setting,
// 		.size = 1,
// 		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
// 		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
// 		.delay = 0x00,
// 	};

// 	if (o_ctrl == NULL) {
// 		CAM_ERR(CAM_OIS, "Invalid Args");
// 		return -EINVAL;
// 	}

// 	for(i = 0; i < retry; i++) {
// 		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
// 		if (rc < 0) {
// 			CAM_ERR(CAM_OIS, "ois type=%d,write 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
// 		} else {
// 			return rc;
// 		}
// 	}
// 	return rc;
// }

// int RamRead32A(    uint32_t addr, uint32_t* data)
// {
// 	int32_t rc = 0;
// 	int retry = 3;
// 	int i;

// 	struct cam_ois_ctrl_t *o_ctrl = ois_ctrl;

// 	if (o_ctrl == NULL) {
// 		CAM_ERR(CAM_OIS, "Invalid Args");
// 		return -EINVAL;
// 	}
// 	for(i = 0; i < retry; i++) {
// 		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
// 		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
// 		if (rc < 0) {
// 			CAM_ERR(CAM_OIS, "ois type=%d,read 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
// 		} else {
// 			return rc;
// 		}
// 	}
// 	return rc;
// }

int RamWrite32A_oneplus(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
		.delay = 0x00,
	};

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,write 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

int RamRead32A_oneplus(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t* data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,read 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

void OISCountinueRead(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, void *data, uint16_t size)
{
	int i = 0;
	int32_t rc = 0;
	int retry = 3;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read_seq(&(o_ctrl->io_master_info), addr, (uint8_t *)data,
		                            CAMERA_SENSOR_I2C_TYPE_WORD,
		                            CAMERA_SENSOR_I2C_TYPE_WORD,
		                            size);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,Continue read failed, rc:%d, retry:%d",o_ctrl->ois_type, rc, i+1);
		} else {
			break;
		}
	}
}

void OISCountinueWrite(  struct cam_ois_ctrl_t *o_ctrl, void *register_data, uint16_t size)
{
	uint32_t *data = (uint32_t *)register_data;
	int32_t rc = 0;
	int i = 0;
	int reg_data_cnt = size - 1;
	int continue_cnt = 0;
	int retry = 3;
	static struct cam_sensor_i2c_reg_array *i2c_write_setting_gl = NULL;

	struct cam_sensor_i2c_reg_setting i2c_write;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return;
	}

	if (i2c_write_setting_gl == NULL) {
		i2c_write_setting_gl = (struct cam_sensor_i2c_reg_array *)kzalloc(
		                           sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2, GFP_KERNEL);
		if(!i2c_write_setting_gl) {
			CAM_ERR(CAM_OIS, "Alloc i2c_write_setting_gl failed");
			return;
		}
	}

	memset(i2c_write_setting_gl, 0, sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2);

	for(i = 0; i< reg_data_cnt; i++) {
		if (i == 0) {
			i2c_write_setting_gl[continue_cnt].reg_addr = data[0];
			i2c_write_setting_gl[continue_cnt].reg_data = data[1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		} else {
			i2c_write_setting_gl[continue_cnt].reg_data = data[i+1];
			i2c_write_setting_gl[continue_cnt].delay = 0x00;
			i2c_write_setting_gl[continue_cnt].data_mask = 0x00;
		}
		continue_cnt++;
	}
	i2c_write.reg_setting = i2c_write_setting_gl;
	i2c_write.size = continue_cnt;
	i2c_write.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	i2c_write.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
	i2c_write.delay = 0x00;

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),
		                                    &i2c_write, 1);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,Continue write failed, rc:%d, retry:%d",o_ctrl->ois_type, rc, i+1);
		} else {
			break;
		}
	}
	kfree(i2c_write_setting_gl);
}

int OISWrite(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = 0x00,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
		.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD,
		.delay = 0x00,
	};

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_write(&(o_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,write 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

int OISRead(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t* data)
{
	int32_t rc = 0;
	int retry = 3;
	int i;

	if (o_ctrl == NULL) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}
	for(i = 0; i < retry; i++) {
		rc = camera_io_dev_read(&(o_ctrl->io_master_info), (uint32_t)addr, (uint32_t *)data,
		                        CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_DWORD);
		if (rc < 0) {
			CAM_ERR(CAM_OIS, "ois type=%d,read 0x%04x failed, retry:%d",o_ctrl->ois_type, addr, i+1);
		} else {
			return rc;
		}
	}
	return rc;
}

void Set124Or128GyroAccelCoef(struct cam_ois_ctrl_t *o_ctrl)
{
	CAM_ERR(CAM_OIS, "SetGyroAccelCoef SelectAct 0x%x GyroPostion 0x%x\n", o_ctrl->ois_actuator_vendor, o_ctrl->ois_gyro_position);

	if (strstr(o_ctrl->ois_name, "124")) {
		if(o_ctrl->ois_gyro_position==3) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x80000001);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		} else if(o_ctrl->ois_gyro_position==2) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		} else if(o_ctrl->ois_gyro_position==4) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x80000001);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x80000001);
			RamWrite32A( GCNV_ZP, (UINT32) 0x7FFFFFFF);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		} else if(o_ctrl->ois_gyro_position==5) {
			RamWrite32A( GCNV_XX, (UINT32) 0x00000000);
			RamWrite32A( GCNV_XY, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_YY, (UINT32) 0x00000000);
			RamWrite32A( GCNV_YX, (UINT32) 0x7FFFFFFF);
			RamWrite32A( GCNV_ZP, (UINT32) 0x80000001);

			RamWrite32A( ACNV_XX, (UINT32) 0x00000000);
			RamWrite32A( ACNV_XY, (UINT32) 0x80000001 );
			RamWrite32A( ACNV_YY, (UINT32) 0x00000000);
			RamWrite32A( ACNV_YX, (UINT32) 0x80000001 );
			RamWrite32A( ACNV_ZP, (UINT32) 0x80000001);
		}
	} else if (strstr(o_ctrl->ois_name, "128")) {

	}
}


static int Download124Or128FW(struct cam_ois_ctrl_t *o_ctrl)
{
	uint32_t UlReadValX, UlReadValY;
	uint32_t spi_type;
	unsigned char rc = 0;
	struct timespec64 mStartTime, mEndTime, diff;
	uint64_t mSpendTime = 0;

	if (!o_ctrl) {
		CAM_ERR(CAM_OIS, "Invalid Args");
		return -EINVAL;
	}

	ois_ctrl = o_ctrl;

	ktime_get_real_ts64(&mStartTime);

	CAM_INFO(CAM_OIS, "MasterSlave 0x%x, GyroVendor 0x%x, GyroPosition 0x%x, ModuleVendor 0x%x, ActVer 0x%x, FWType 0x%x\n",
	         o_ctrl->ois_type, o_ctrl->ois_gyro_vendor, o_ctrl->ois_gyro_position, o_ctrl->ois_module_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_fw_flag);

	if (strstr(o_ctrl->ois_name, "124")) {
		rc = SelectDownload(o_ctrl->ois_gyro_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_type, o_ctrl->ois_fw_flag);

		if (0 == rc) {
			Set124Or128GyroAccelCoef(ois_ctrl);

			//remap
			RamWrite32A(0xF000, 0x00000000 );
			//msleep(120);

			//SPI-Master ( Act1 )  Check gyro signal
			RamRead32A(0x061C, & UlReadValX );
			RamRead32A(0x0620, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_X:0x%x, Gyro_Y:0x%x", UlReadValX, UlReadValY);

			spi_type = 0;
			RamRead32A(0x8880, & spi_type );
			CAM_INFO(CAM_OIS, "spi_status:0x%x", spi_type);

			//SPI-Master ( Act1 )  Check gyro gain
			RamRead32A(0x82b8, & UlReadValX );
			RamRead32A(0x8318, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_gain_X:0x%x, Gyro_gain_Y:0x%x", UlReadValX, UlReadValY);

			//SPI-Master ( Act1 )  start gyro signal transfer. ( from Master to slave. )
			if (CAM_OIS_MASTER == o_ctrl->ois_type) {
				RamWrite32A(0x8970, 0x00000001 );
				//msleep(5);
				RamWrite32A(0xf111, 0x00000001 );
				//msleep(5);
			}
		} else {
			switch (rc) {
			case 0x01:
				CAM_ERR(CAM_OIS, "H/W error");
				break;
			case 0x02:
				CAM_ERR(CAM_OIS, "Table Data & Program download verify error");
				break;
			case 0xF0:
				CAM_ERR(CAM_OIS, "Download code select error");
				break;
			case 0xF1:
				CAM_ERR(CAM_OIS, "Download code information read error");
				break;
			case 0xF2:
				CAM_ERR(CAM_OIS, "Download code information disagreement");
				break;
			case 0xF3:
				CAM_ERR(CAM_OIS, "Download code version error");
				break;
			default:
				CAM_ERR(CAM_OIS, "Unkown error code");
				break;
			}
		}
	} else if (strstr(o_ctrl->ois_name, "128")) {
		rc = FlashDownload128(o_ctrl->ois_module_vendor, o_ctrl->ois_actuator_vendor, o_ctrl->ois_type, o_ctrl->ois_fw_flag);

		if (0 == rc) {
			Set124Or128GyroAccelCoef(ois_ctrl);

			//LC898128 don't need to do remap
			//RamWrite32A(0xF000, 0x00000000 );
			//msleep(120);

			//select gyro vendor
			RamWrite32A(0xF015, o_ctrl->ois_gyro_vendor);
			msleep(10);

			//SPI-Master ( Act1 )  Check gyro signal
			RamRead32A(0x0220, & UlReadValX );
			RamRead32A(0x0224, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_X:0x%x, Gyro_Y:0x%x", UlReadValX, UlReadValY);
/*
			spi_type = 0;
			RamRead32A(0xf112, & spi_type );
			CAM_INFO(CAM_OIS, "spi_type:0x%x", spi_type);
*/
			//SPI-Master ( Act1 )  Check gyro gain
			/*RamWrite32A(0x82b8, 0xbe147a00 );
                        RamWrite32A(0x8318, 0xba1cab00 );*/
			RamRead32A(0x82b8, & UlReadValX );
			RamRead32A(0x8318, & UlReadValY );
			CAM_INFO(CAM_OIS, "Gyro_gain_X:0x%x, Gyro_gain_Y:0x%x", UlReadValX, UlReadValY);

			//SPI-Master ( Act1 )  start gyro signal transfer. ( from Master to slave. )
			if (CAM_OIS_MASTER == o_ctrl->ois_type) {
				RamWrite32A(0xF017, 0x01);
                                mutex_lock(&(o_ctrl->ois_hall_data_mutex));
                                kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
                                kfifo_reset(&(o_ctrl->ois_hall_data_fifoV2));
                                mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
			}
		} else {
			switch (rc&0xF0) {
			case 0x00:
				CAM_ERR(CAM_OIS, "Error ; during the rom boot changing. Also including 128 power off issue.");
				break;
			case 0x20:
				CAM_ERR(CAM_OIS, "Error ; during Initial program for updating to program memory.");
				break;
			case 0x30:
				CAM_ERR(CAM_OIS, "Error ; during User Mat area erasing.");
				break;
			case 0x40:
				CAM_ERR(CAM_OIS, "Error ; during User Mat area programing.");
				break;
			case 0x50:
				CAM_ERR(CAM_OIS, "Error ; during the verification.");
				break;
			case 0x90:
				CAM_ERR(CAM_OIS, "Error ; during the drive offset confirmation.");
				break;
			case 0xA0:
				CAM_ERR(CAM_OIS, "Error ; during the MAT2 re-write process.");
				break;
			case 0xF0:
				if (rc == 0xF0)
					CAM_ERR(CAM_OIS, "mistake of module vendor designation.");
				else if (rc == 0xF1)
					CAM_ERR(CAM_OIS, "mistake size of From Code.");
				break;
			default:
				CAM_ERR(CAM_OIS, "Unkown error code");
				break;
			}
		}
	} else {
		CAM_ERR(CAM_OIS, "Unsupported OIS");
	}
	ktime_get_real_ts64(&mEndTime);
	diff = timespec64_sub(mEndTime, mStartTime);
	mSpendTime = (timespec64_to_ns(&diff))/1000000;

	CAM_INFO(CAM_OIS, "cam_ois_fw_download rc=%d, (Spend: %d ms)", rc, mSpendTime);

	return 0;
}

int DownloadFW(struct cam_ois_ctrl_t *o_ctrl)
{
	uint8_t rc = 0;

	if (o_ctrl) {
		mutex_lock(&ois_mutex);
                if(o_ctrl->opcode.prog == 1){
                        CAM_INFO(CAM_OIS, "update gyro vendor to =%d",o_ctrl->opcode.prog);
                        if(o_ctrl->ois_type == CAM_OIS_MASTER)
                                o_ctrl->ois_gyro_vendor = 6;
                        else if(o_ctrl->ois_type == CAM_OIS_SLAVE)
                                o_ctrl->ois_gyro_vendor = 3;
                        ois_ctrls[CAM_OIS_MASTER]->ois_gyro_vendor = 6;
			if(ois_ctrls[CAM_OIS_SLAVE])
				ois_ctrls[CAM_OIS_SLAVE]->ois_gyro_vendor = 3;
                }else if(o_ctrl->opcode.prog == 3){
                        CAM_INFO(CAM_OIS, "update gyro vendor to =%d",o_ctrl->opcode.prog);
                        if(o_ctrl->ois_type == CAM_OIS_MASTER)
                                o_ctrl->ois_gyro_vendor = 0;
                        else if(o_ctrl->ois_type == CAM_OIS_SLAVE)
                                o_ctrl->ois_gyro_vendor = 0;
                        ois_ctrls[CAM_OIS_MASTER]->ois_gyro_vendor = 0;
			if(ois_ctrls[CAM_OIS_SLAVE])
				ois_ctrls[CAM_OIS_SLAVE]->ois_gyro_vendor = 0;
                }

		if (CAM_OIS_INVALID == ois_state[o_ctrl->ois_type]) {

			if (CAM_OIS_MASTER == o_ctrl->ois_type) {
				rc = Download124Or128FW(ois_ctrls[CAM_OIS_MASTER]);
				if (rc) {
					CAM_ERR(CAM_OIS, "ois type=%d,Download %s FW failed",o_ctrl->ois_type, o_ctrl->ois_name);
				} else {
					if (dump_ois_registers && !ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 1)) {
						ois_start_read(ois_ctrls[CAM_OIS_MASTER], 1);
					}
				}
			} else if (CAM_OIS_SLAVE == o_ctrl->ois_type) {
				if (CAM_OIS_INVALID == ois_state[CAM_OIS_MASTER]) {
					rc = Download124Or128FW(ois_ctrls[CAM_OIS_MASTER]);
					/*donot start main camera thread when switch tele
					if (!rc&&dump_ois_registers&&!ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 1)) {
						ois_start_read(ois_ctrls[CAM_OIS_MASTER], 1);
					}*/
					//msleep(120);// Need to check whether we need a long time delay
				}
				if (rc) {
					CAM_ERR(CAM_OIS, "ois type=%d,Download %s FW failed",o_ctrl->ois_type, ois_ctrls[CAM_OIS_MASTER]->ois_name);
				} else {
					rc = Download124Or128FW(ois_ctrls[CAM_OIS_SLAVE]);
					if (rc) {
						CAM_ERR(CAM_OIS, "ois type=%d,Download %s FW failed",o_ctrl->ois_type, o_ctrl->ois_name);
					} else {
						if (dump_ois_registers&&!ois_start_read_thread(ois_ctrls[CAM_OIS_SLAVE], 1)) {
							ois_start_read(ois_ctrls[CAM_OIS_SLAVE], 1);
						}
					}
				}
			}
			ois_state[o_ctrl->ois_type] = CAM_OIS_FW_DOWNLOADED;
		} else {
			CAM_ERR(CAM_OIS, "ois type=%d,OIS state 0x%x is wrong",o_ctrl->ois_type, ois_state[o_ctrl->ois_type]);
		}
		mutex_unlock(&ois_mutex);
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}

	return rc;
}

int OISPollThread124(void *arg)
{
#define SAMPLE_COUNT_IN_OIS_124 7
#define SAMPLE_INTERVAL     4000
	int32_t i = 0;
	uint32_t *data = NULL;
	uint32_t kfifo_in_len = 0;
	uint32_t fifo_size_in_ois = SAMPLE_COUNT_IN_OIS_124*SAMPLE_SIZE_IN_DRIVER;
	uint32_t fifo_size_in_driver = SAMPLE_COUNT_IN_DRIVER*SAMPLE_SIZE_IN_DRIVER;
	unsigned long long timestampQ = 0;

	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;
	uint32_t ois_hall_registers[SAMPLE_COUNT_IN_OIS_124] = {0x89C4, 0x89C0, 0x89BC, 0x89B8, 0x89B4, 0x89B0, 0x89AC};

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

	data = kzalloc(fifo_size_in_ois, GFP_KERNEL);
	if (!data) {
		CAM_ERR(CAM_OIS, "failed to kzalloc");
		return -1;
	}

	CAM_DBG(CAM_OIS, "ois type=%d,OISPollThread124 creat",o_ctrl->ois_type);

	while(1) {
		mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
		if (o_ctrl->ois_poll_thread_exit) {
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			goto exit;
		}
		mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
		timestampQ = __arch_counter_get_cntvct();
		//CAM_ERR(CAM_OIS, "trace timestamp:%lld in Qtime", timestampQ);

		memset(data, 0, fifo_size_in_ois);

		//Read OIS HALL data
		for (i = 0; i < SAMPLE_COUNT_IN_OIS_124; i++) {
			data[3*i] = timestampQ >> 32;
			data[3*i+1] = timestampQ & 0xFFFFFFFF;
			OISRead(o_ctrl, ois_hall_registers[i], &(data[3*i+2]));
			timestampQ -= 2*CLOCK_TICKCOUNT_MS;
		}

		for (i = SAMPLE_COUNT_IN_OIS_124 - 1; i >= 0; i--) {
			CAM_DBG(CAM_OIS, "ois type=%d,OIS HALL data %lld (0x%x 0x%x)",o_ctrl->ois_type, ((uint64_t)data[3*i] << 32)+(uint64_t)data[3*i+1], data[3*i+2]&0xFFFF0000>>16, data[3*i+2]&0xFFFF);
		}

		mutex_lock(&(o_ctrl->ois_hall_data_mutex));
		if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + fifo_size_in_ois) > fifo_size_in_driver) {
			CAM_DBG(CAM_OIS, "ois type=%d,ois_hall_data_fifo is full, fifo size %d, file len %d, will reset FIFO",o_ctrl->ois_type, kfifo_size(&(o_ctrl->ois_hall_data_fifo)), kfifo_len(&(o_ctrl->ois_hall_data_fifo)));
			kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
		}

		if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + fifo_size_in_ois) <= fifo_size_in_driver) {
			kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifo), data, fifo_size_in_ois);
			if (kfifo_in_len != fifo_size_in_ois) {
				CAM_DBG(CAM_OIS, "ois type=%d,kfifo_in %d Bytes, FIFO maybe full, some OIS Hall sample maybe dropped.",o_ctrl->ois_type, kfifo_in_len);
			} else {
				CAM_DBG(CAM_OIS, "ois type=%d,kfifo_in %d Bytes",o_ctrl->ois_type, fifo_size_in_ois);
			}
		}
		mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

		usleep_range(SAMPLE_COUNT_IN_OIS_124*SAMPLE_INTERVAL-5, SAMPLE_COUNT_IN_OIS_124*SAMPLE_INTERVAL);
	}

exit:
	kfree(data);
	CAM_DBG(CAM_OIS, "ois type=%d,OISPollThread124 exit",o_ctrl->ois_type);
	return 0;
}


int OISPollThread128(void *arg)
{
	uint32_t i = 0;
	uint32_t j = 0;
	uint32_t kfifo_in_len = 0;
	uint32_t fifo_size_in_ois = SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_OIS;
	uint32_t fifo_size_in_ois_aligned = SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_OIS_ALIGNED;
	uint32_t fifo_size_in_driver = SAMPLE_COUNT_IN_DRIVER*SAMPLE_SIZE_IN_DRIVER;
	uint16_t *p_hall_data_in_ois = NULL;
	struct cam_ois_hall_data_in_ois_aligned *p_hall_data_in_ois_aligned = NULL;
	struct cam_ois_hall_data_in_driver *p_hall_data_in_driver = NULL;
	struct cam_ois_ctrl_t *o_ctrl = (struct cam_ois_ctrl_t *)arg;
	uint64_t first_QTimer = 0;      // This will be used for the start QTimer to calculate the QTimer interval
	uint64_t prev_QTimer = 0;       // This is the last QTimer in the last CCI read
	uint64_t current_QTimer = 0;    // This will be used for the end QTimer to calculate the QTimer interval
	uint64_t interval_QTimer = 0;   // This is the QTimer interval between two sample
	uint64_t sample_offset = 0;     // This is timestamp offset between IC and system
	uint64_t readout_time = (((1+2+1+SAMPLE_SIZE_IN_OIS*SAMPLE_COUNT_IN_OIS)*8)/1000)*CLOCK_TICKCOUNT_MS;      // This is the time of CCI read
	uint16_t sample_count = 0;      // This is the sample count for one CCI read
	uint16_t sample_num = 0;        // This will be used to detect whehter some HALL data was dropped
	uint32_t total_sample_count = 0;// This will be used to calculate the QTimer interval
	uint16_t threshold = 2;         // This is the threshold to trigger Timestamp calibration, this means 2ms
	uint16_t tmp = 0;
	uint64_t real_QTimer;
	uint64_t real_QTimer_after;
	uint64_t i2c_read_offset;
	static uint64_t pre_real_QTimer = 0;
	static uint64_t pre_QTimer_offset =0 ;
	uint64_t estimate_QTimer = 0;	// This is the QTimer interval between two sample
	uint32_t vaild_cnt = 0;
	uint32_t is_add_Offset = 0;
	uint32_t offset_cnt;
        uint32_t data=0;

	p_hall_data_in_ois = kzalloc(fifo_size_in_ois, GFP_KERNEL);
	if (!p_hall_data_in_ois) {
		CAM_ERR(CAM_OIS, "failed to kzalloc p_hall_data_in_ois");
		return -1;
	}

	p_hall_data_in_ois_aligned = kzalloc(fifo_size_in_ois_aligned, GFP_KERNEL);
	if (!p_hall_data_in_ois_aligned) {
		CAM_ERR(CAM_OIS, "failed to kzalloc p_hall_data_in_ois_aligned");
		kfree(p_hall_data_in_ois);
		return -1;
	}

	p_hall_data_in_driver = kzalloc(SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_DRIVER, GFP_KERNEL);
	if (!p_hall_data_in_driver) {
		CAM_ERR(CAM_OIS, "failed to kzalloc p_hall_data_in_driver");
		kfree(p_hall_data_in_ois);
		kfree(p_hall_data_in_ois_aligned);
		return -1;
	}

	CAM_DBG(CAM_OIS, "ois type=%d,OISPollThread128 creat",o_ctrl->ois_type);

        mutex_lock(&(o_ctrl->ois_power_down_mutex));
        if (o_ctrl->ois_power_state == CAM_OIS_POWER_OFF) {
                mutex_unlock(&(o_ctrl->ois_power_down_mutex));
                goto exit;
        }
        mutex_unlock(&(o_ctrl->ois_power_down_mutex));

        RamWrite32A_oneplus(o_ctrl,0xF110, 0x0);//Clear buffer to all "0" & enable buffer update function.
        RamRead32A_oneplus(o_ctrl, 0x82B8, &data);
	CAM_ERR(CAM_OIS, "ois addr=0x82B8 data=0x%x",data);

        RamRead32A_oneplus(o_ctrl, 0x8318, &data);
        CAM_ERR(CAM_OIS, "ois addr=0X8318 data=0x%x",data);

        RamRead32A_oneplus(o_ctrl, 0x8800, &data);
        CAM_ERR(CAM_OIS, "ois addr=0x8800 data=0x%x",data);

        RamWrite32A_oneplus(o_ctrl,0x82B8, 0x40000000);
        RamWrite32A_oneplus(o_ctrl,0x8318, 0x40000000);
        RamWrite32A_oneplus(o_ctrl,0x8800, 0x10);

        RamRead32A_oneplus(o_ctrl, 0x82B8, &data);
	CAM_ERR(CAM_OIS, "ois addr=0x82B8 data=0x%x",data);

        RamRead32A_oneplus(o_ctrl, 0x8318, &data);
        CAM_ERR(CAM_OIS, "ois addr=0X8318 data=0x%x",data);

        RamRead32A_oneplus(o_ctrl, 0x8800, &data);
        CAM_ERR(CAM_OIS, "ois addr=0x8800 data=0x%x",data);


	while(1) {

		mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
		if (o_ctrl->ois_poll_thread_exit) {
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));
			goto exit;
		}
		mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));

		sample_count = 0;
		tmp = sample_num;
		memset(p_hall_data_in_ois, 0, fifo_size_in_ois);
		memset(p_hall_data_in_ois_aligned, 0, fifo_size_in_ois_aligned);
		memset(p_hall_data_in_driver, 0, SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_DRIVER);

		usleep_range(1995, 2000);

		real_QTimer = __arch_counter_get_cntvct();

		//Read OIS HALL data
		OISCountinueRead(o_ctrl, 0xF111, (void *)p_hall_data_in_ois, fifo_size_in_ois);
		real_QTimer_after = __arch_counter_get_cntvct();
		i2c_read_offset = real_QTimer_after - real_QTimer;
		//Covert the data from unaligned to aligned
		for(i = 0, j = 0; i < SAMPLE_COUNT_IN_OIS; i++) {
			if(((p_hall_data_in_ois[3*i] == 0) && (p_hall_data_in_ois[3*i+1] == 0) && (p_hall_data_in_ois[3*i+2] == 0)) || \
				(p_hall_data_in_ois[3*i] == OIS_MAGIC_NUMBER && p_hall_data_in_ois[3*i+1] == OIS_MAGIC_NUMBER)) {
				CAM_DBG(CAM_OIS, "ois type=%d,OIS HALL RAW data %d %d (0x%x 0x%x)",o_ctrl->ois_type, i,
				        p_hall_data_in_ois[3*i],
				        p_hall_data_in_ois[3*i+1],
				        p_hall_data_in_ois[3*i+2]);
			} else {
				p_hall_data_in_ois_aligned[j].hall_data_cnt = p_hall_data_in_ois[3*i];
				p_hall_data_in_ois_aligned[j].hall_data = ((uint32_t)p_hall_data_in_ois[3*i+1] << 16) + p_hall_data_in_ois[3*i+2];
				CAM_DBG(CAM_OIS, "ois type=%d,OIS HALL RAW data %d %d (0x%x 0x%x)",o_ctrl->ois_type, i,
				        p_hall_data_in_ois_aligned[j].hall_data_cnt,
				        p_hall_data_in_ois_aligned[j].hall_data&0xFFFF0000>>16,
				        p_hall_data_in_ois_aligned[j].hall_data&0xFFFF);
				j++;
			}
		}

		sample_offset = (uint64_t)((p_hall_data_in_ois[3*(SAMPLE_COUNT_IN_OIS-1)+2] & 0xFF) * CLOCK_TICKCOUNT_MS * 2 / OIS_MAX_COUNTER);

		if(first_QTimer == 0) {
			//Init some parameters
			for(i = 0; i < SAMPLE_COUNT_IN_OIS; i++) {
				if((p_hall_data_in_ois_aligned[i].hall_data == 0) && (p_hall_data_in_ois_aligned[i].hall_data_cnt == 0)) {
					break;
				}
			}
			if ((i >= 1) && (i <= SAMPLE_COUNT_IN_OIS)) {
				first_QTimer = __arch_counter_get_cntvct() - readout_time - sample_offset;
				prev_QTimer = first_QTimer;
				sample_num = p_hall_data_in_ois_aligned[i-1].hall_data_cnt;
			}
			continue;
		} else {
			vaild_cnt = 0;
			current_QTimer = __arch_counter_get_cntvct() - readout_time - sample_offset;
			//calculate sample_count and total_sample_count, and detect whether some hall data was dropped.
			for(i = 0; i < SAMPLE_COUNT_IN_OIS; i++) {
				if((p_hall_data_in_ois_aligned[i].hall_data != 0) || (p_hall_data_in_ois_aligned[i].hall_data_cnt != 0)) {
					total_sample_count++;
					sample_count++;
					while (++tmp != p_hall_data_in_ois_aligned[i].hall_data_cnt) {
						total_sample_count++;
						CAM_DBG(CAM_OIS, "ois type=%d,One sample was droped, %d %d %d",o_ctrl->ois_type, i, tmp, p_hall_data_in_ois_aligned[i].hall_data_cnt);
					}
				}
			}
			if(sample_count > 0) {
				if (total_sample_count > 1) {
					interval_QTimer = (current_QTimer - first_QTimer)/(total_sample_count - 1);
				} else if(total_sample_count == 1) {
					interval_QTimer = threshold*CLOCK_TICKCOUNT_MS;
				}

				//Calculate the TS for every sample, if some sample were dropped, the TS of this sample will still be calculated, but will not report to UMD.
				for(i = 0; i < SAMPLE_COUNT_IN_OIS; i++) {
					if((p_hall_data_in_ois_aligned[i].hall_data != 0) || (p_hall_data_in_ois_aligned[i].hall_data_cnt != 0)) {
						if (i == 0) {
							//p_hall_data_in_driver[i].timestamp = prev_QTimer;
							estimate_QTimer = prev_QTimer;
							while (++sample_num != p_hall_data_in_ois_aligned[i].hall_data_cnt) {
								//p_hall_data_in_driver[i].timestamp += interval_QTimer;
								estimate_QTimer += interval_QTimer;
							}
							//p_hall_data_in_driver[i].timestamp += interval_QTimer;
							estimate_QTimer += interval_QTimer;
							p_hall_data_in_driver[i].high_dword = estimate_QTimer >> 32;
							p_hall_data_in_driver[i].low_dword  = estimate_QTimer & 0xFFFFFFFF;
							p_hall_data_in_driver[i].hall_data  = p_hall_data_in_ois_aligned[i].hall_data;
						} else {
							//p_hall_data_in_driver[i].timestamp = p_hall_data_in_driver[i-1].timestamp;
							estimate_QTimer = ((uint64_t)p_hall_data_in_driver[i-1].high_dword << 32) + (uint64_t)p_hall_data_in_driver[i-1].low_dword;
							while (++sample_num != p_hall_data_in_ois_aligned[i].hall_data_cnt) {
								//p_hall_data_in_driver[i].timestamp += interval_QTimer;
								estimate_QTimer += interval_QTimer;
							}
							//p_hall_data_in_driver[i].timestamp += interval_QTimer;
							estimate_QTimer += interval_QTimer;
							p_hall_data_in_driver[i].high_dword = estimate_QTimer >> 32;
							p_hall_data_in_driver[i].low_dword  = estimate_QTimer & 0xFFFFFFFF;
							p_hall_data_in_driver[i].hall_data  = p_hall_data_in_ois_aligned[i].hall_data;
						}

						CAM_DBG(CAM_OIS, "ois type=%d,OIS HALL data %lld (0x%x 0x%x)",o_ctrl->ois_type,
						        ((uint64_t)p_hall_data_in_driver[i].high_dword << 32) + (uint64_t)p_hall_data_in_driver[i].low_dword,
						        (p_hall_data_in_driver[i].hall_data&0xFFFF0000)>>16,
						        p_hall_data_in_driver[i].hall_data&0xFFFF);
						vaild_cnt ++ ;
					} else {
						break;
					}
				}

				if ((i >= 1) && (i <= SAMPLE_COUNT_IN_OIS)) {
					prev_QTimer = ((uint64_t)p_hall_data_in_driver[i-1].high_dword << 32) + (uint64_t)p_hall_data_in_driver[i-1].low_dword;
				}
				real_QTimer -= sample_offset;


				CAM_DBG(CAM_OIS, "OIS HALL data before %lld %lld",
							real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer,
							pre_real_QTimer - (real_QTimer - (vaild_cnt-1) * interval_QTimer) );

				if ( pre_real_QTimer != 0 &&  vaild_cnt > 0 &&
					((real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer <= CLOCK_TICKCOUNT_MS) ||
					 (pre_real_QTimer - (real_QTimer - (vaild_cnt-1) * interval_QTimer) <= CLOCK_TICKCOUNT_MS))){
					real_QTimer += interval_QTimer;
					}

				for (i =0; i < 5; i++){
					if ( pre_real_QTimer != 0 &&  vaild_cnt > 0 &&
						((int64_t)(real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer )< 0)){
						real_QTimer += interval_QTimer;
						is_add_Offset = 1;
					}
				}

				if ( pre_real_QTimer != 0 &&  vaild_cnt > 0 &&
					((real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer <= CLOCK_TICKCOUNT_MS) ||
					 (pre_real_QTimer - (real_QTimer - (vaild_cnt-1) * interval_QTimer) <= CLOCK_TICKCOUNT_MS))){
					real_QTimer += interval_QTimer;

				}

				if ((pre_real_QTimer != 0)
					&& ((int64_t)(real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer) > 42000 
					|| (int64_t)(real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer) < 34000)) {

					if (total_sample_count > 100 ) {
					real_QTimer =  pre_real_QTimer + vaild_cnt * interval_QTimer;
						CAM_DBG(CAM_OIS, "OIS HALL data force calate  %d ",offset_cnt);
						offset_cnt ++ ;
						if (offset_cnt > 3) {
							is_add_Offset = 1;
				}
					}
				} else {
						offset_cnt = 0;
				}

				CAM_DBG(CAM_OIS, "OIS HALL data after %lld  %lld",
							real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer,
							pre_real_QTimer - (real_QTimer - (vaild_cnt-1) * interval_QTimer));

				pre_QTimer_offset = real_QTimer - pre_real_QTimer - (vaild_cnt-1) * interval_QTimer;

				for (i = 0; i < vaild_cnt ;i++){
					p_hall_data_in_driver[vaild_cnt - i -1].high_dword = real_QTimer >> 32;
					p_hall_data_in_driver[vaild_cnt - i -1].low_dword  = real_QTimer & 0xFFFFFFFF;
					real_QTimer -= interval_QTimer;
				}

				for ( i = 0; i < vaild_cnt;i++){
					CAM_DBG(CAM_OIS, "OIS HALL data %lld (0x%x 0x%x) pre :%lld offset reg:%d i2c_read_offset %lld",
						((uint64_t)p_hall_data_in_driver[i].high_dword << 32) + (uint64_t)p_hall_data_in_driver[i].low_dword,
						(p_hall_data_in_driver[i].hall_data&0xFFFF0000)>>16,
						p_hall_data_in_driver[i].hall_data&0xFFFF,
						        pre_real_QTimer,
						        (p_hall_data_in_ois[3*(SAMPLE_COUNT_IN_OIS-1)+2] & 0xFF),
						        i2c_read_offset);

					}
				if (!is_add_Offset){
				pre_real_QTimer = ((uint64_t)p_hall_data_in_driver[vaild_cnt -1].high_dword << 32) |
					(uint64_t)p_hall_data_in_driver[vaild_cnt -1].low_dword;
				} else {
					pre_real_QTimer = 0;
					is_add_Offset = 0;
				}
				//Do Timestamp calibration
				//Put the HALL data into the FIFO
				mutex_lock(&(o_ctrl->ois_hall_data_mutex));
				if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + vaild_cnt*SAMPLE_SIZE_IN_DRIVER) > fifo_size_in_driver) {
					CAM_DBG(CAM_OIS, "ois type=%d,ois_hall_data_fifo is full, fifo size %d, file len %d, will reset FIFO",o_ctrl->ois_type,
					        kfifo_size(&(o_ctrl->ois_hall_data_fifo)),
					        kfifo_len(&(o_ctrl->ois_hall_data_fifo)));
					kfifo_reset(&(o_ctrl->ois_hall_data_fifo));
				}

				if ((kfifo_len(&(o_ctrl->ois_hall_data_fifoV2)) + vaild_cnt*SAMPLE_SIZE_IN_DRIVER) > fifo_size_in_driver) {
					CAM_DBG(CAM_OIS, "ois type=%d,ois_hall_data_fifoV2 is full, fifo size %d, file len %d, will reset FIFO",o_ctrl->ois_type,
					        kfifo_size(&(o_ctrl->ois_hall_data_fifoV2)),
					        kfifo_len(&(o_ctrl->ois_hall_data_fifoV2)));
					kfifo_reset(&(o_ctrl->ois_hall_data_fifoV2));
				}
				//Store ois data for EISV3
				if ((kfifo_len(&(o_ctrl->ois_hall_data_fifo)) + vaild_cnt*SAMPLE_SIZE_IN_DRIVER) <= fifo_size_in_driver) {
					kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifo), p_hall_data_in_driver, vaild_cnt*SAMPLE_SIZE_IN_DRIVER);

					if (kfifo_in_len != vaild_cnt*SAMPLE_SIZE_IN_DRIVER) {
						CAM_DBG(CAM_OIS, "ois type=%d,kfifo_in %d Bytes, FIFO maybe full, some OIS Hall sample maybe dropped.",o_ctrl->ois_type, kfifo_in_len);
					} else {
						CAM_DBG(CAM_OIS, "ois type=%d,kfifo_in %d Bytes",o_ctrl->ois_type, vaild_cnt*SAMPLE_SIZE_IN_DRIVER);
					}
				}
				//Store ois data for EISv2
				if ((kfifo_len(&(o_ctrl->ois_hall_data_fifoV2)) + vaild_cnt*SAMPLE_SIZE_IN_DRIVER) <= fifo_size_in_driver) {
					kfifo_in_len = kfifo_in(&(o_ctrl->ois_hall_data_fifoV2), p_hall_data_in_driver, vaild_cnt*SAMPLE_SIZE_IN_DRIVER);

					if (kfifo_in_len != vaild_cnt*SAMPLE_SIZE_IN_DRIVER) {
						CAM_DBG(CAM_OIS, "ois type=%d,kfifo_in %d Bytes, FIFOV2 maybe full, some OIS Hall sample maybe dropped.",o_ctrl->ois_type, kfifo_in_len);
					} else {
						CAM_DBG(CAM_OIS, "ois type=%d,kfifo_inV2 %d Bytes",o_ctrl->ois_type, vaild_cnt*SAMPLE_SIZE_IN_DRIVER);
					}
				}
				mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
			}
		}
	}

exit:
	pre_real_QTimer = 0;
	is_add_Offset = 0;
	total_sample_count = 0;
	kfree(p_hall_data_in_ois);
	kfree(p_hall_data_in_ois_aligned);
	kfree(p_hall_data_in_driver);
	CAM_DBG(CAM_OIS, "ois type=%d,OISPollThread128 exit",o_ctrl->ois_type);
	return 0;
}

int WRITE_QTIMER_TO_OIS (struct cam_ois_ctrl_t *o_ctrl) {
        uint64_t qtime_ns = 0,qtime_ns_after=0,value=351000;
        int32_t  rc = 0,i=0,j=0;
	static struct cam_sensor_i2c_reg_array *i2c_write_setting_gl = NULL;
	struct cam_sensor_i2c_reg_setting i2c_write;

	if (i2c_write_setting_gl == NULL) {
		i2c_write_setting_gl = (struct cam_sensor_i2c_reg_array *)kzalloc(
		                           sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2, GFP_KERNEL);
		if(!i2c_write_setting_gl) {
			CAM_ERR(CAM_OIS, "Alloc i2c_write_setting_gl failed");
			return -1;
		}
	}
        /*if(is_wirte==TRUE){
                CAM_ERR(CAM_OIS, "have write Qtimer");
                return 0;
        }*/
	memset(i2c_write_setting_gl, 0, sizeof(struct cam_sensor_i2c_reg_array)*MAX_DATA_NUM*2);
        while(value>350000 && j<5 ){
	        rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns);
	        if (rc < 0) {
		        CAM_ERR(CAM_OIS,
			        "Failed to get current qtimer value: %d",
			        rc);
		        return rc;
	        }
                CAM_DBG(CAM_OIS,"qtime_ns: ms=0x%x us=0x%x",(uint32_t)((qtime_ns>>32)&0xffffffff),(uint32_t)(qtime_ns&0xffffffff));

	        for(i = 0; i< 2; i++) {
		        if (i == 0) {
			        i2c_write_setting_gl[i].reg_addr = 0xF112;
			        i2c_write_setting_gl[i].reg_data = (uint32_t)((qtime_ns>>32)&0xffffffff);
			        i2c_write_setting_gl[i].delay = 0X00;
			        i2c_write_setting_gl[i].data_mask = 0x00;
		        } else {
			        i2c_write_setting_gl[i].reg_data = (uint32_t)(qtime_ns&0xffffffff);
			        i2c_write_setting_gl[i].delay = 0x00;
			        i2c_write_setting_gl[i].data_mask = 0x00;
		        }
	        }
	        i2c_write.reg_setting = i2c_write_setting_gl;
	        i2c_write.size = 2;
	        i2c_write.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
	        i2c_write.data_type = CAMERA_SENSOR_I2C_TYPE_DWORD;
	        i2c_write.delay = 0x00;
                rc = camera_io_dev_write_continuous(&(o_ctrl->io_master_info),&i2c_write, 1);
	        if (rc < 0) {
		        CAM_ERR(CAM_OIS,
			        "Failed to write qtimer value: %d",
			        rc);
		        return rc;
	        }
	        rc = cam_sensor_util_get_current_qtimer_ns(&qtime_ns_after);
	        if (rc < 0) {
		        CAM_ERR(CAM_OIS,
			        "Failed to get current qtime_ns_after value: %d",
			        rc);
		        return rc;
	        }
                CAM_DBG(CAM_OIS,"qtime_ns_after: ms=0x%x us=0x%x",(uint32_t)((qtime_ns_after>>32)&0xffffffff),(uint32_t)(qtime_ns_after&0xffffffff));
                value = qtime_ns_after-qtime_ns;
                //is_wirte=TRUE;
                if(j>0)
                        CAM_DBG(CAM_OIS,"write time=%d",value);
                j++;
        }
        return rc;
}

int OIS_READ_HALL_DATA_TO_UMD_NEW (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings) {

        int32_t                         rc = 0,i=0;
        struct i2c_settings_list       *i2c_list;
        uint8_t                        *read_buff = NULL;
        uint32_t                        buff_length = 0;
        uint32_t                        read_length = 0;
        uint64_t                        fifo_count = 0,qtime_ms=0,qtime_us=0;
        uint64_t                        preqtime_ms=0,newqtime=0;
        uint32_t                        hall_data_x = 0,hall_data_y = 0;
        int                             j=0;
        uint8_t                        *temp_buff = NULL;



        list_for_each_entry(i2c_list,
                        &(i2c_settings->list_head), list) {
                        read_buff = i2c_list->i2c_settings.read_buff;
                        buff_length = i2c_list->i2c_settings.read_buff_len;
                        if ((read_buff == NULL) || (buff_length == 0)) {
                                CAM_ERR(CAM_OIS,
                                        "Invalid input buffer, buffer: %pK, length: %d",
                                        read_buff, buff_length);
                                return -EINVAL;
                        }
                        read_length = i2c_list->i2c_settings.size;
                        temp_buff = kzalloc(read_length, GFP_KERNEL);
                        memset(temp_buff, 0, read_length);
                        CAM_DBG(CAM_OIS,"buffer: %pK, fbufer_length: %d  read_length=%d",read_buff, buff_length,read_length);
                        CAM_DBG(CAM_OIS,"start read");
                        OISCountinueRead(o_ctrl, 0xF111, (void *)temp_buff, read_length);
                        CAM_DBG(CAM_OIS,"read done");
                       //加log
                        /* ois data count is 144 Bytes and have max sample is 32; last Bytes is sample count,and 32/33 Bytes means Qtimer */

                        fifo_count = temp_buff[143];
                        read_buff[143]=temp_buff[143];
                        if(fifo_count > SAMPLE_COUNT_IN_NCS_DATA) {
                                CAM_ERR(CAM_OIS,"ois have drop data fifo_count=%d",fifo_count);
                                fifo_count = SAMPLE_COUNT_IN_NCS_DATA;
                        }

                        for(j=0;j<fifo_count;j++){
                                read_buff[j*4]=temp_buff[(fifo_count-j-1)*4];
                                read_buff[j*4+1]=temp_buff[(fifo_count-j-1)*4+1];
                                read_buff[j*4+2]=temp_buff[(fifo_count-j-1)*4+2];
                                read_buff[j*4+3]=temp_buff[(fifo_count-j-1)*4+3];
                        }
                        for(j=0;j<8;j++)
                                read_buff[128+j]=temp_buff[128+j];



                        for ( j = 0; j < 8; j++)
                        {
                                 newqtime = (newqtime << 8) | (read_buff[128+j]);
                        }
                        qtime_ms = ((read_buff[128]<<24)+(read_buff[129]<<16)+(read_buff[130]<<8)+read_buff[131]);
                        qtime_us = ((read_buff[132]<<24)+(read_buff[133]<<16)+(read_buff[134]<<8)+read_buff[135]);
                        if((newqtime-(fifo_count-1)*2000000)<preqtime_ms)
                                CAM_ERR(CAM_OIS,"time error");
                        preqtime_ms=newqtime;
                        CAM_DBG(CAM_OIS,"READ fifo_count=%d",fifo_count);
                        CAM_DBG(CAM_OIS,"Rqtimer value: ms=0x%x us=0x%x",qtime_ms,qtime_us);

                        if(fifo_count > 0) {
                                for(i=0 ; i<SAMPLE_COUNT_IN_NCS_DATA; i++){
                                        hall_data_x=((read_buff[i*4]<<8)+read_buff[i*4+1]);
                                        hall_data_y=((read_buff[i*4+2]<<8)+read_buff[i*4+3]);
                                        CAM_DBG(CAM_OIS,"hall_data_x=0x%x hall_data_y=0x%x ",hall_data_x,hall_data_y);
                                }
                        }
        }
        kfree(temp_buff);
        return rc;
}

int OIS_READ_HALL_DATA_TO_UMD (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings)
{
        struct i2c_settings_list       *i2c_list;
        uint8_t                        *read_buff = NULL;
        uint32_t                        buff_length = 0;
        uint32_t                        read_length = 0;
        uint32_t                        read_fifo_length = 0;
        uint32_t                        fifo_len_in_ois_driver=0;
        uint8_t                        *temp_buff = NULL;
        uint8_t                         j=0,sample_count=0;
        uint64_t                        ticks = 0;
        uint64_t                        qtimer=0,pre_qtimer=0;

        temp_buff = kzalloc(SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_DRIVER, GFP_KERNEL);
        memset(temp_buff, 0, SAMPLE_COUNT_IN_OIS*SAMPLE_SIZE_IN_DRIVER);

        list_for_each_entry(i2c_list,
                &(i2c_settings->list_head), list) {
                read_buff = i2c_list->i2c_settings.read_buff;
                buff_length = i2c_list->i2c_settings.read_buff_len;
                if ((read_buff == NULL) || (buff_length == 0)) {
                        CAM_ERR(CAM_OIS,
                                "Invalid input buffer, buffer: %pK, length: %d",
                                read_buff, buff_length);
                        kfree(temp_buff);
                        return -EINVAL;
                }
                read_length = i2c_list->i2c_settings.size;
                CAM_DBG(CAM_OIS,"read length: %d addr type=%d data type=%d",read_length,i2c_list->i2c_settings.addr_type,i2c_list->i2c_settings.data_type);
                mutex_lock(&(o_ctrl->ois_hall_data_mutex));
                fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifoV2));
                CAM_DBG(CAM_OIS,"ffio length=%d",fifo_len_in_ois_driver);
                if (fifo_len_in_ois_driver > SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_NCS_DATA) {
                        fifo_len_in_ois_driver = SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_NCS_DATA;
                }
                if(fifo_len_in_ois_driver>0){
                       read_fifo_length = kfifo_out(&(o_ctrl->ois_hall_data_fifoV2),temp_buff, fifo_len_in_ois_driver);
                       sample_count=(read_fifo_length/SAMPLE_SIZE_IN_DRIVER);
                       CAM_DBG(CAM_OIS,"read sample count=%d", sample_count);
                       if(sample_count>0){
                            for(j=0;j<sample_count;j++){
                                read_buff[j*4]   = temp_buff[12*j+11];
                                read_buff[j*4+1] = temp_buff[12*j+10];
                                read_buff[j*4+2] = temp_buff[12*j+9];
                                read_buff[j*4+3] = temp_buff[12*j+8];
                                CAM_DBG(CAM_OIS,"hall data=0x%x 0x%x",((read_buff[j*4]<<8)+read_buff[j*4+1]),((read_buff[j*4+2]<<8)+read_buff[j*4+3]));
                            }
                            //set the Byte as sample count;all is 204ByTes
                            read_buff[143] = sample_count;
                            //set last 8 BYTES as last sample Qtimer
                            for ( j = 0; j < 8; j++)
                            {
                                if(j<4)
                                        ticks = (ticks << 8) | (temp_buff[(sample_count-1)*12+3-j]);
                                else
                                        ticks = (ticks << 8) | (temp_buff[(sample_count-1)*12+11-j]);
                            }

                            //ticks=((uint64_t)(hight<<32))+(uint64_t)low;

                            CAM_DBG(CAM_OIS,"ticks=%lld 0x%llx",ticks,ticks);
                            qtimer=mul_u64_u32_div(ticks,QTIMER_MUL_FACTOR, QTIMER_DIV_FACTOR);
                            CAM_DBG(CAM_OIS,"qtimer=%lld  0x%llx",qtimer,qtimer);
                            pre_qtimer = qtimer;

                            for (j = 0; j < 8; j++) {
                                    CAM_DBG(CAM_OIS, "time: reg_data[%d]: 0x%x",
                                            135-j, (qtimer & 0xFF));
                                    read_buff[135-j] =
                                            (qtimer & 0xFF);
                                    qtimer >>= 8;
                            }
                            CAM_DBG(CAM_OIS,"Qtimer=%x %x %x %x %x %x %x %x",read_buff[128],read_buff[129],read_buff[130],read_buff[131],read_buff[132],read_buff[133],read_buff[134],read_buff[135]);
                       }
                }else {
                        CAM_DBG(CAM_OIS,"no fifo data ,set Qtimer as before,sample count=0");
                        read_buff[143] = 0;
                        for (j = 0; j < 8; j++) {
                                    CAM_DBG(CAM_OIS, "time: reg_data[%d]: 0x%x",
                                            135-j, (pre_qtimer & 0xFF));
                                    read_buff[135-j] =
                                            (pre_qtimer & 0xFF);
                                    pre_qtimer >>= 8;
                        }
                }
                mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
        }
        kfree(temp_buff);
        return 0;
}

void ReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data)
{
	uint32_t data_size = 0;
	uint32_t fifo_len_in_ois_driver;

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifo));
	if (fifo_len_in_ois_driver > 0) {
                int ret;
		if (fifo_len_in_ois_driver > SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER) {
			fifo_len_in_ois_driver = SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER;
		}
                ret = kfifo_to_user(&(o_ctrl->ois_hall_data_fifo), data, fifo_len_in_ois_driver, &data_size);
		CAM_DBG(CAM_OIS, "ois type=%d,Copied %d Bytes to UMD with return value %d",o_ctrl->ois_type, data_size,ret);
	} else {
		CAM_DBG(CAM_OIS, "ois type=%d,fifo_len is %d, no need copy to UMD",o_ctrl->ois_type, fifo_len_in_ois_driver);
	}

	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
}

void ReadOISHALLDataV2(struct cam_ois_ctrl_t *o_ctrl, void *data)
{
	uint32_t data_size = 0;
	uint32_t fifo_len_in_ois_driver;

	mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	fifo_len_in_ois_driver = kfifo_len(&(o_ctrl->ois_hall_data_fifoV2));
	if (fifo_len_in_ois_driver > 0) {
                int ret;
		if (fifo_len_in_ois_driver > SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER) {
			fifo_len_in_ois_driver = SAMPLE_SIZE_IN_DRIVER*SAMPLE_COUNT_IN_DRIVER;
		}
                ret = kfifo_to_user(&(o_ctrl->ois_hall_data_fifoV2), data, fifo_len_in_ois_driver, &data_size);
		CAM_DBG(CAM_OIS, "ois type=%d,Copied %d Bytes to UMD EISv2 with return value %d",o_ctrl->ois_type, data_size,ret);
	} else {
		CAM_DBG(CAM_OIS, "ois type=%d,fifo_len is %d, no need copy to UMD EISv2",o_ctrl->ois_type, fifo_len_in_ois_driver);
	}

	mutex_unlock(&(o_ctrl->ois_hall_data_mutex));
}

void ReadOISHALLDataV3(struct cam_ois_ctrl_t *o_ctrl, void *data)
{

	//mutex_lock(&(o_ctrl->ois_hall_data_mutex));
	//mutex_unlock(&(o_ctrl->ois_hall_data_mutex));

}

int OISControl(struct cam_ois_ctrl_t *o_ctrl)
{
        if(o_ctrl->ois_eis_function != 1)
                return 0;
	if (o_ctrl && (o_ctrl->ois_type != CAM_OIS_MASTER)){
		CAM_INFO(CAM_OIS, "ois type=%d, don't create OIS thread",o_ctrl->ois_type);
		return 0;
	}
	if (o_ctrl && (CAM_OIS_READY == ois_state[o_ctrl->ois_type])) {
		switch (o_ctrl->ois_poll_thread_control_cmd) {
		case CAM_OIS_START_POLL_THREAD:
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread) {
				CAM_INFO(CAM_OIS, "ois type=%d,ois_poll_thread is already created, no need to create again.",o_ctrl->ois_type);
			} else {
				o_ctrl->ois_poll_thread_exit = false;
				if (strstr(o_ctrl->ois_name, "128")) {
					o_ctrl->ois_poll_thread = kthread_run(OISPollThread128, o_ctrl, o_ctrl->ois_name);
				} else if (strstr(o_ctrl->ois_name, "124")) {
					//o_ctrl->ois_poll_thread = kthread_run(OISPollThread124, o_ctrl, o_ctrl->ois_name);
				}
				if (!o_ctrl->ois_poll_thread) {
					o_ctrl->ois_poll_thread_exit = true;
					CAM_ERR(CAM_OIS, "ois type=%d,create ois poll thread failed",o_ctrl->ois_type);
				}
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));

			break;
		case CAM_OIS_STOP_POLL_THREAD:
			mutex_lock(&(o_ctrl->ois_poll_thread_mutex));
			if (o_ctrl->ois_poll_thread) {
				o_ctrl->ois_poll_thread_exit = true;
				o_ctrl->ois_poll_thread = NULL;
			}
			mutex_unlock(&(o_ctrl->ois_poll_thread_mutex));

			break;
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl=%p,ois_type=%d ois_state=%d",o_ctrl,o_ctrl->ois_type,ois_state[o_ctrl->ois_type]);
		return -1;
	}

	return 0;
}

bool IsOISReady(struct cam_ois_ctrl_t *o_ctrl)
{
	uint32_t temp, retry_cnt;
	retry_cnt = 3;

	if (o_ctrl) {
                do {
		        RamRead32A_oneplus(o_ctrl,0xF100, &temp);
		        CAM_ERR(CAM_OIS, "OIS %d 0xF100 = 0x%x", o_ctrl->ois_type, temp);
		        if (temp == 0) {
			        ois_state[o_ctrl->ois_type] = CAM_OIS_READY;
			        return true;
		        }
		        retry_cnt--;
		        msleep(10);
		} while(retry_cnt);
		return false;
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
		return false;
	}
}

void InitOIS(struct cam_ois_ctrl_t *o_ctrl)
{
	if (o_ctrl) {
		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			ois_state[CAM_OIS_MASTER] = CAM_OIS_INVALID;
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			ois_state[CAM_OIS_SLAVE] = CAM_OIS_INVALID;
			if (ois_ctrls[CAM_OIS_MASTER]) {
                                if(ois_ctrls[CAM_OIS_MASTER]->ois_change_cci) {
                                        if(chip_version_old){
                                                ois_ctrls[CAM_OIS_MASTER]->io_master_info.cci_client->cci_device = CCI_DEVICE_0;
                                                ois_ctrls[CAM_OIS_MASTER]->io_master_info.cci_client->cci_i2c_master = MASTER_0;
                                                ois_ctrls[CAM_OIS_MASTER]->cci_i2c_master = MASTER_0;
                                                CAM_INFO(CAM_OIS, "change old module cci");
                                        }
                                }
				if (camera_io_init(&(ois_ctrls[CAM_OIS_MASTER]->io_master_info))) {
					CAM_ERR(CAM_OIS, "cci_init failed");
				}
			}
		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}
}

void DeinitOIS(struct cam_ois_ctrl_t *o_ctrl)
{
	if (o_ctrl) {
		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			if (o_ctrl->ois_read_thread_start_to_read&&ois_ctrls[CAM_OIS_MASTER]) {
				ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 0);
			}
			ois_state[CAM_OIS_MASTER] = CAM_OIS_INVALID;
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			if (ois_ctrls[CAM_OIS_MASTER]) {
				/*donot start main camera thread when switch tele
				if(o_ctrl->ois_read_thread_start_to_read) {
					ois_start_read_thread(ois_ctrls[CAM_OIS_MASTER], 0);
				}*/
				if (camera_io_release(&(ois_ctrls[CAM_OIS_MASTER]->io_master_info))) {
					CAM_ERR(CAM_OIS, "ois type=%d,cci_deinit failed",o_ctrl->ois_type);
				}
			}
			if (ois_ctrls[CAM_OIS_SLAVE]) {
				if(o_ctrl->ois_read_thread_start_to_read) {
					ois_start_read_thread(ois_ctrls[CAM_OIS_SLAVE], 0);
				}
			}
			ois_state[CAM_OIS_SLAVE] = CAM_OIS_INVALID;

		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}
	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}
}

void InitOISResource(struct cam_ois_ctrl_t *o_ctrl)
{
        int rc;
	mutex_init(&ois_mutex);
	if (o_ctrl) {
		if (o_ctrl->ois_type == CAM_OIS_MASTER) {
			ois_ctrls[CAM_OIS_MASTER] = o_ctrl;
			//Hardcode the parameters of main OIS, and those parameters will be overrided when open main camera
			o_ctrl->io_master_info.cci_client->sid = 0x24;
			o_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
			o_ctrl->io_master_info.cci_client->retries = 3;
			o_ctrl->io_master_info.cci_client->id_map = 0;
			CAM_INFO(CAM_OIS, "ois type=%d,ois_ctrls[%d] = %p",o_ctrl->ois_type, CAM_OIS_MASTER, ois_ctrls[CAM_OIS_MASTER]);
		} else if (o_ctrl->ois_type == CAM_OIS_SLAVE) {
			ois_ctrls[CAM_OIS_SLAVE] = o_ctrl;
			CAM_INFO(CAM_OIS, "ois type=%d,ois_ctrls[%d] = %p",o_ctrl->ois_type, CAM_OIS_SLAVE, ois_ctrls[CAM_OIS_SLAVE]);
		} else {
			CAM_ERR(CAM_OIS, "ois_type 0x%x is wrong", o_ctrl->ois_type);
		}

                if(!cam_ois_kobj) {
                        cam_ois_kobj = kobject_create_and_add("ois_control", kernel_kobj);
                        rc = sysfs_create_groups(cam_ois_kobj, ois_groups);
                        if (rc != 0) {
                                CAM_ERR(CAM_OIS,"Error creating sysfs ois group");
                                sysfs_remove_groups(cam_ois_kobj, ois_groups);
                        }
                } else {
                        CAM_ERR(CAM_OIS, "ois_control node exist");
                }

	} else {
		CAM_ERR(CAM_OIS, "o_ctrl is NULL");
	}

        //Create OIS control node
        if(face_common_dir == NULL){
                face_common_dir =  proc_mkdir("OIS", NULL);
                if(!face_common_dir) {
                        CAM_ERR(CAM_OIS, "create dir fail CAM_ERROR API");
                        //return FACE_ERROR_GENERAL;
                }
        }

        if(proc_file_entry == NULL){
                proc_file_entry = proc_create("OISControl", 0777, face_common_dir, &proc_file_fops);
                if(proc_file_entry == NULL) {
                        CAM_ERR(CAM_OIS, "Create fail");
                } else {
                        CAM_INFO(CAM_OIS, "Create successs");
                }
        }

        if(proc_file_entry_tele == NULL){
                proc_file_entry_tele = proc_create("OISControl_tele", 0777, face_common_dir, &proc_file_fops_tele);
                if(proc_file_entry_tele == NULL) {
                        CAM_ERR(CAM_OIS, "Create fail");
                } else {
                CAM_INFO(CAM_OIS, "Create successs");
                }
        }
}

int32_t oplus_cam_ois_construct_default_power_setting(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 4;
	power_info->power_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting)*4,
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VAF;
	power_info->power_setting[0].seq_val = CAM_VAF;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 0;

	power_info->power_setting[1].seq_type = SENSOR_VIO;
	power_info->power_setting[1].seq_val = CAM_VIO;
	power_info->power_setting[1].config_val = 1;
	power_info->power_setting[1].delay = 0;

	power_info->power_setting[2].seq_type = SENSOR_VDIG;
	power_info->power_setting[2].seq_val = CAM_VDIG;
	power_info->power_setting[2].config_val = 1;
	power_info->power_setting[2].delay = 0;

	power_info->power_setting[3].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_setting[3].seq_val = CAM_V_CUSTOM1;
	power_info->power_setting[3].config_val = 1;
	power_info->power_setting[3].delay = 10;

	power_info->power_down_setting_size = 4;
	power_info->power_down_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting)*4,
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_VAF;
	power_info->power_down_setting[0].seq_val = CAM_VAF;
	power_info->power_down_setting[0].config_val = 0;

	power_info->power_down_setting[1].seq_type = SENSOR_VIO;
	power_info->power_down_setting[1].seq_val = CAM_VIO;
	power_info->power_down_setting[1].config_val = 0;

	power_info->power_setting[2].seq_type = SENSOR_VDIG;
	power_info->power_down_setting[2].seq_val = CAM_VDIG;
	power_info->power_down_setting[2].config_val = 0;

	power_info->power_down_setting[3].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_down_setting[3].seq_val = CAM_V_CUSTOM1;
	power_info->power_down_setting[3].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;
	return rc;
}


