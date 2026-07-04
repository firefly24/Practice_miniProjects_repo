
#include <linux/err.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/module.h>

#include "telemetry_debugfs.h"
#include "telemetry_stats.h"
#include "telemetry_dev.h"




static int dump_session_data(struct seq_file *s, void *unused)
{
	//seq_printf(s,"Hello Telemetry!\n");
	//seq_printf(s,"This is my first debugfs file.\n");
	
	struct telemetry_dev *tdev = s->private;
	
	if (!tdev)
		return -ENODEV;
	
	telemetry_stats_show(&tdev->stats,s);
	
	return 0;
}

static int telemetry_dbgfs_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, dump_session_data, inode->i_private);
}


static const struct file_operations stats_debugfs_fops = {
	.owner  = THIS_MODULE, 
	.open = telemetry_dbgfs_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int telemetry_dbgfs_init(/* struct telemetry_dbgfs *tdbg,*/ struct telemetry_dev* tdev)
{
	int ret=0;
	struct dentry *stats = NULL;
	struct telemetry_dbgfs *tdbg;
			
	if(!tdev)
		return -ENODEV;
		
	tdbg = &tdev->dbgfs;
	
	/* Create debugfs parent directory */
	tdbg->root_dir = debugfs_create_dir(TELEMETRY_DEBUGFS_DIR, NULL);
	
	if (IS_ERR(tdbg->root_dir))
	{
		ret = PTR_ERR(tdbg->root_dir);
		tdbg->root_dir = NULL;
		return ret;
	}
	
	stats = debugfs_create_file(TELEMETRY_STATS_FILE,
				0444,
				tdbg->root_dir,
				tdev,
				&stats_debugfs_fops
				 );
	if (!stats)
	{
		ret = -ENOMEM;
		goto failure_cleanup;
	}
	
	// Create debugfs session dump file 
	//debugfs_create_devm_seqfile(dev,TELEMETRY_STATS_FILE,tdbg->root_dir,dump_session_data);
	return ret;
	
failure_cleanup:
	debugfs_remove(tdbg->root_dir);
	tdbg->root_dir = NULL;
	return ret;
}


void telemetry_dbgfs_cleanup(/*struct telemetry_dbgfs *tdbg*/ struct telemetry_dev *tdev)
{
	struct telemetry_dbgfs *tdbg;
	if (!tdev)
		return;
		
	tdbg = &tdev->dbgfs;
		
	/* Cleanup debugfs directory */	
	if (tdbg->root_dir)
		debugfs_remove_recursive(tdbg->root_dir);
}


