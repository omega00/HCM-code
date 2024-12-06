# include <linux/module.h>  
# include <linux/init.h>    
# include <linux/kernel.h>  
# include "vars.h"
# include "hsotcc_response_siganl.h"

extern bool terminate_hcc;

struct workqueue_struct *poll_iio_queue, *poll_pcie_queue;
struct work_struct poll_iio, poll_pcie;

void thread_fun_poll_iio(struct work_struct *work) {
  int cpu = IIO_CORE;
  uint32_t budget = WORKER_BUDGET;
  while (budget) {

      sample_counters_iio_wr(cpu); 
      update_iio_wr_occ();         
    budget--;
  }
  if(!terminate_hcc){
    queue_work_on(cpu,poll_iio_queue, &poll_iio);
  }
  else{
    return;
  }
}

void thread_fun_poll_pcie(struct work_struct *work) {  
  int cpu = PCIE_CORE;
  uint32_t budget = WORKER_BUDGET;
  while (budget) {

    latest_measured_avg_occ_wr = smoothed_avg_occ_wr >> 10; //to reflect a consistent IIO occupancy value in log and MBA update logic
    // } else{
    // latest_measured_avg_occ_rd = smoothed_avg_occ_rd >> 10; //to reflect a consistent IIO occupancy value in log and MBA update logic
    // }
    //printk("smoothed_avg_occ_wr==%lld",smoothed_avg_occ_wr);
    if(1){//enable_local_response){
      host_local_response();
      //printk("smoothed_avg_occ_wr==%lld",smoothed_avg_occ_wr);
      printk("latest_measured_avg_occ_wr==%d",latest_measured_avg_occ_wr);
      //printk("budget: %ld", budget);
    }
    // if(!terminate_hcc_logging && PCIE_LOGGING){
    //   update_log_pcie(cpu);
    // }
    budget--;
  }
  if(!terminate_hcc){
    queue_work_on(cpu,poll_pcie_queue, &poll_pcie);
  }
  else{
    return;
  }
  
}

void poll_iio_init(void) {
    //initialize the log
    printk(KERN_INFO "Starting IIO Occupancy measurement");
    //if(mode == 0){   
      //init_iio_wr_log();
    update_iio_wr_occ_ctl_reg();
   // }
    //else{
   //   init_iio_rd_log();
   //   update_iio_rd_occ_ctl_reg();
   // }
}

void poll_pcie_init(void) {
    // #if USE_PROCESS_SCHEDULER
    // init_mba_process_scheduler();
    // #endif
    //initialize the log
    printk(KERN_INFO "Starting PCIe Bandwidth Measurement");
    // init_pcie_log();
}

void poll_iio_exit(void) {
    //dump log info
    printk(KERN_INFO "Ending IIO Occupancy measurement");
    flush_workqueue(poll_iio_queue);
    flush_scheduled_work();
    destroy_workqueue(poll_iio_queue);
    // if(mode == 0){
    //   if(IIO_LOGGING){
    //     dump_iio_wr_log();
    //   }
    // }
    // else{
    //   if(IIO_LOGGING){
    //     dump_iio_rd_log();
    //   }
    // }
}

void poll_pcie_exit(void) {
    //dump log info
    printk(KERN_INFO "Ending PCIe Bandwidth Measurement");
    flush_workqueue(poll_pcie_queue);
    flush_scheduled_work();
    destroy_workqueue(poll_pcie_queue);
    if(latest_mba_val > 0){
        latest_mba_val = 0;
        update_mba_msr_register();
        // #if USE_PROCESS_SCHEDULER
        // update_mba_process_scheduler();
        // #endif
    }
    // if(PCIE_LOGGING){
    //   dump_pcie_log();
    // }
}


static int __init hostcc_init(void) {  		

    printk("Initializing hostcc_my");
     //Start IIO occupancy measurement
    poll_iio_queue = alloc_workqueue("poll_iio_queue",  WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
    if (!poll_iio_queue) {
        printk(KERN_ERR "Failed to create IIO workqueue\n");
        return -ENOMEM;
    }
    INIT_WORK(&poll_iio, thread_fun_poll_iio);
    poll_iio_init();
    queue_work_on(IIO_CORE,poll_iio_queue, &poll_iio);
   

      //Start PCIe bandwidth measurement
    poll_pcie_queue = alloc_workqueue("poll_pcie_queue", WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
    if (!poll_pcie_queue) {
        printk(KERN_ERR "Failed to create PCIe workqueue\n");
        return -ENOMEM;
    }
    INIT_WORK(&poll_pcie, thread_fun_poll_pcie);
    poll_pcie_init();
    queue_work_on(PCIE_CORE,poll_pcie_queue, &poll_pcie);





	printk(KERN_CRIT "Hello kernel\n");		
	return 0;
	
}  
static void __exit hostcc_exit(void) {			

    //sysfs_remove_group(my_kobj, &attr_group);
    // kobject_put(my_kobj);
    // terminate_hcc_logging = true;
    msleep(5000);
    terminate_hcc = true;
    //nf_exit();
    poll_iio_exit();
    poll_pcie_exit();
    printk("latest_measured_avg_occ_wr=%d",latest_measured_avg_occ_wr);
	  printk(KERN_ALERT "Goodbye kernel\n");  
}	
module_init(hostcc_init);   					
module_exit(hostcc_exit);   					
MODULE_LICENSE("GPL");   					
MODULE_AUTHOR("hostcc");	 					
MODULE_DESCRIPTION("for fun");  			 