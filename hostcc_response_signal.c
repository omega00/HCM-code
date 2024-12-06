# include "hsotcc_response_siganl.h"
# include "vars.h"

extern bool terminate_hcc = false;


uint64_t cum_occ_sample_wr = 0;
uint64_t prev_cum_occ_wr = 0;
uint64_t cur_cum_occ_wr = 0;
uint64_t tsc_sample_iio_wr = 0;
uint64_t prev_rdtsc_iio_wr = 0;
uint64_t cur_rdtsc_iio_wr = 0;
uint64_t latest_time_delta_iio_wr_ns = 0;
uint32_t latest_measured_avg_occ_wr = 0;
uint64_t smoothed_avg_occ_wr;
int target_iio_wr_thresh = 15;
uint32_t latest_mba_val = 0;
uint64_t last_reduced_tsc = 0;
uint64_t latest_avg_occ_wr = 0;


void sample_iio_wr_occ_counter(int c){
  uint64_t msr_num;
  uint32_t low = 0;
	uint32_t high = 0;
	msr_num = 0x0A9AL;       //IRP_MSR_PMON_CTR_BASE + (0x20 * NIC_IIO_STACK) + IIO_WR_COUNTER_OFFSET;
  rdmsr_on_cpu(c,msr_num,&low,&high);
  cum_occ_sample_wr = ((uint64_t)high << 32) | low;
	prev_cum_occ_wr = cur_cum_occ_wr;
	cur_cum_occ_wr = cum_occ_sample_wr;
}
void sample_iio_wr_time_counter(void){
  tsc_sample_iio_wr = rdtscp();
	prev_rdtsc_iio_wr = cur_rdtsc_iio_wr;
	cur_rdtsc_iio_wr = tsc_sample_iio_wr;
}
// void sample_mba_time_counter(void){
//   tsc_sample_mba = rdtscp();
// 	prev_rdtsc_mba = cur_rdtsc_mba;
// 	cur_rdtsc_mba = tsc_sample_mba;
// }

void sample_counters_iio_wr(int c){
	//first sample occupancy
	sample_iio_wr_occ_counter(c);
	//sample time at the last
	sample_iio_wr_time_counter();
	return;
}

void update_iio_wr_occ(void){
	latest_time_delta_iio_wr_ns = ((cur_rdtsc_iio_wr - prev_rdtsc_iio_wr) * 10) / 33;//此处为什么*10/33作用尚不明确
	if(latest_time_delta_iio_wr_ns > 0){
        
		latest_avg_occ_wr = (cur_cum_occ_wr - prev_cum_occ_wr) / (latest_time_delta_iio_wr_ns >> 1);
    // ((occ[i] - occ[i-1]) / (((time_us[i+1] - time_us[i])) * 1e-6 * freq)); 
    // IRP counter operates at the frequency of 500MHz
    //printk("latest_avg_occ_wr==%lld",latest_avg_occ_wr);
    if(latest_avg_occ_wr >= 0){
      smoothed_avg_occ_wr = ((7*smoothed_avg_occ_wr) + (latest_avg_occ_wr << 10)) >> 3;
    }
	}
}

// IIO Write occupancy logic
void update_iio_wr_occ_ctl_reg(void){
	//program the desired CTL register to read the corresponding CTR value
  uint64_t msr_num;
	msr_num = 0x0A9CL; //IRP_MSR_PMON_CTL_BASE + (0x20 * NIC_IIO_STACK) + IIO_WR_COUNTER_OFFSET;
  uint32_t low = IRP_OCC_VAL & 0xFFFFFFFF;
	uint32_t high = IRP_OCC_VAL >> 32;
  wrmsr_on_cpu(IIO_CORE,msr_num,low,high);
}



void update_mba_msr_register(void){
  uint32_t low = 0;
  uint32_t high = 0;
  uint64_t msr_num_1 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID;
  uint64_t msr_num_2 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID+1;
  wrmsr_on_cpu(MBA_LEVEL_1_CORE,msr_num_1,low,high);
  wrmsr_on_cpu(MBA_LEVEL_2_CORE,msr_num_1,low,high);
  wrmsr_on_cpu(MBA_LEVEL_1_CORE,msr_num_2,low,high);
}


void increase_mba_val(void){
    uint64_t msr_num_1 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID;
    uint64_t msr_num_2 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID+1;
    u32 low = MBA_VAL_HIGH & 0xFFFFFFFF;
	  u32 high = MBA_VAL_HIGH >> 32;

    wrmsr_on_cpu(MBA_LEVEL_1_CORE,msr_num_1,low,high);

    // WARN_ON(!(latest_mba_val >= 0));
    // #if !(USE_PROCESS_SCHEDULER)
    // WARN_ON(!(latest_mba_val <= 3));
    // #endif
    // #if USE_PROCESS_SCHEDULER
    // WARN_ON(!(latest_mba_val <= 4)); //level 4 means infinite latency by MBA -- essentially SIGSTOP
    // #endif
    

    // #if USE_PROCESS_SCHEDULER
    // else if(latest_mba_val == 3){
    //     latest_mba_val++;
    //     update_mba_process_scheduler(); //should initiate SIGSTOP
    // }
    // #endif
}

void decrease_mba_val(void){
    uint64_t cur_tsc_val = rdtscp();
    if((cur_tsc_val - last_reduced_tsc) / 3300 < SLACK_TIME_US){
        return; 
    }
    uint64_t msr_num_1 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID;
    uint64_t msr_num_2 = PQOS_MSR_MBA_MASK_START + MBA_COS_ID+1;
    uint32_t low = MBA_VAL_LOW & 0xFFFFFFFF;
	  uint32_t high = MBA_VAL_LOW >> 32;

    if(latest_mba_val > 0){
        latest_mba_val--;
        switch(latest_mba_val){
            case 0:
                wrmsr_on_cpu(MBA_LEVEL_1_CORE,msr_num_1,low,high);
                last_reduced_tsc = rdtscp();
                break;
            case 1:
                wrmsr_on_cpu(MBA_LEVEL_2_CORE,msr_num_1,low,high);
                last_reduced_tsc = rdtscp();
                break;
            case 2:
                wrmsr_on_cpu(MBA_LEVEL_1_CORE,msr_num_2,low,high);
                last_reduced_tsc = rdtscp();
                break;
           // case 3:
                // #if !(USE_PROCESS_SCHEDULER)
                // WARN_ON(!(false));
                // #endif
                // #if USE_PROCESS_SCHEDULER
                // update_mba_process_scheduler();
                // last_reduced_tsc = rdtscp();
                // #endif
                break;
            default:
                WARN_ON(!(false));
                break;
        }
    }
}

void host_local_response(void){
//   if(mode == 0){
    //Rx side logic
    //if((smoothed_avg_pcie_bw) < (target_pcie_thresh << 10)){
        if(latest_measured_avg_occ_wr > target_iio_wr_thresh){
            increase_mba_val();
           // printk("increase_mba_val");
        }
        else if (latest_measured_avg_occ_wr <= target_iio_wr_thresh)
        {
          decrease_mba_val();
          //printk("decrease_mba_val");
        }
       
    //}

}