#pragma once

//#include <linux/fs.h>
#include <linux/debugfs.h>

#define TELEMETRY_DEBUGFS_DIR "telemetryd"
#define TELEMETRY_STATS_FILE "stats"

struct device;
struct telemetry_dev;

struct telemetry_dbgfs
{
	struct dentry *root_dir;
	//struct dentry *stats_file;
	
	//void *private_data;
	//struct seq_file *s;
	
	//char *stats_file_name;
	//char *parent_dir_name;
	
	/*seq_file read callback */
	//int (*dump_session_data)(struct seq_file *s,void *data);
};

int telemetry_dbgfs_init(/*struct telemetry_dbgfs *tdbg, */ struct telemetry_dev *tdev);


void telemetry_dbgfs_cleanup(/*struct telemetry_dbgfs *tdbg*/ struct telemetry_dev *tdev);
