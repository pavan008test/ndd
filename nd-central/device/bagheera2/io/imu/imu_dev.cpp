/* Copyright (C) 2016 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, October 2016
 */

#include <cstdio>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <sys/syscall.h>
#include <imu_dev.h>
#include <system_utils.h>
#include <nd_task.h>
#include <assert.h>
#include "nd_factory.h"

#include <log.h>
//#include <utils.h>
#include "nd_time.h" 
#include <config_parser.h>

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "mpu_api.h"

#ifdef __cplusplus
}
#endif

#define BAGHEERACONFIG_INI "/home/ubuntu/.nddevice/latest/bagheera_config.ini"
#define finit_module(fd, param_values, flags) syscall(__NR_finit_module, fd, param_values, flags)
#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)

#define IMU_AXES_XYZ 3
#define IMU_PARAM    2 // Only Accel and Gyro Are Considered. Magneto is Excluded

#define TAG "IMUD"

extern ND_DeviceFactory *nd_device_obj;

static Imu::imu_callback_t *cb=NULL;
static Imu::imu_temperature_callback_t *temp_cb=NULL;
static int imu_handle = -1;

static const int accel_enable_mask = 1<<0;
static const int gyro_enable_mask = 1<<1;
static const int magneto_enable_mask = 1<<2;

static const string IMU_ICM20602 = "inv-mpu-iio-i2c-icm20602";
static const string IMU_ICM42670 = "inv-mpu-iio-i2c";

static const string IMU_MPU_B2 = "inv_mpu_iio_new";
static const string IMU_MPU_B3 = "inv-mpu-iio";

static int imu_enable_flag = 0;

// gyro_scale: 0 ->250dps ,1 ->500dps  ,2 ->1000dps ,3 ->2000dps -> BAG2
// gyro_scale: 3 ->250dps ,2 ->500dps  ,1 ->1000dps ,0 ->2000dps -> BAG3
int gyro_scale = 1;

// accel_scale: 0->[-2g +2g] 1->[ -4g +4g ] 2--> [-8g +8g] 3-> [-16g +16g] -> BAG2
// accel_scale: 3->[-2g +2g] 2->[ -4g +4g ] 1--> [-8g +8g] 0-> [-16g +16g] -> BAG3
int accel_scale = 1;

// samples per seconds
// Sample rates available are 10,20,50,100,200,500,1000 BAG2
// Sample rates available are 12,25,50,100,200,400,800  BAG3
int sample_rate = 20;

// DLPF rates available are 460, 184, 92, 41, 20, 10, 5   BAG2
// DLPF rates available are 180, 121, 73, 53, 34, 25, 16  BAG3
int accel_dlpf_bw = 5;

// DLPF rates available are 256, 188, 98, 42, 20, 10, 5    BAG2
// DLPF rates available are 180, 121, 73, 53, 34, 25, 16  BAG3
int gyro_dlpf_bw = 5;

int lpf_fir_order = 0; //0,26,200: 0 for Bagheera2, All three for Bagheera3
// Sign changes required
int invert_accel_x = 1;
int invert_accel_y = 1;
int invert_accel_z = 1;
int invert_gyro_x = 1;
int invert_gyro_y = 1;
int invert_gyro_z = 1;

static const int IMU_MODULE_INSERT_RETRY_COUNT = 3;
static const int IMU_MODULE_INSERT_TIMEOUT = 10;

pthread_t imu_recover_tid = -1;
pthread_mutex_t imu_count_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t imu_err_det_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t imu_recover_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t imu_recover_cond;

const unsigned int temp_decimation_factor = sample_rate;
static bool imu_dev_enable( );
static bool imu_dev_disable( );
static int imu_callback_new ( struct imu_data *data_cb_in );

using namespace std;

static const int IMU_RECOVERY_TIME_MAX = 30;

template<class T> T coeff_0[1]={1.00};
template<class T> T coeff_26[IMU_PARAM][26+1] = {

        {
                -1.15075039e-03,  1.45494159e-03,  3.64979434e-03, -3.04961009e-18,
                -9.36917501e-03, -8.80946675e-03,  1.29442788e-02,  3.00271357e-02,
                -1.09575894e-17, -6.06125550e-02, -5.51213959e-02,  8.85720138e-02,
                2.98523056e-01,  3.99784246e-01,  2.98523056e-01,  8.85720138e-02,
                -5.51213959e-02, -6.06125550e-02, -1.09575894e-17,  3.00271357e-02,
                1.29442788e-02, -8.80946675e-03, -9.36917501e-03, -3.04961009e-18,
                3.64979434e-03,  1.45494159e-03, -1.15075039e-03
        },

        {
                1.85701310e-03,  2.34789891e-03,  2.24971212e-03, -1.52075942e-18,
                -5.77510529e-03, -1.42161978e-02, -2.08887135e-02, -1.85085528e-02,
                5.46425834e-18,  3.73612284e-02,  8.89516570e-02,  1.42932291e-01,
                1.84007885e-01,  1.99361768e-01,  1.84007885e-01,  1.42932291e-01,
                8.89516570e-02,  3.73612284e-02,  5.46425834e-18, -1.85085528e-02,
                -2.08887135e-02, -1.42161978e-02, -5.77510529e-03, -1.52075942e-18,
                2.24971212e-03,  2.34789891e-03,  1.85701310e-03
        }

};

// 0 for gyro and index 1 for accel
template<class T> T coeff_200[IMU_PARAM][200+1] = {

       {
               -1.24785049e-18, -2.45410597e-04, -1.54519828e-04,  1.58300486e-04,
               2.63805938e-04, -3.37634469e-18, -2.83982771e-04, -1.83295721e-04,
               1.92150966e-04,  3.27013057e-04, -1.59902685e-18, -3.64747185e-04,
               -2.38851727e-04,  2.53493963e-04,  4.35871545e-04,  1.43407337e-18,
               -4.93547331e-04, -3.24849132e-04,  3.46044525e-04,  5.96486488e-04,
               -2.61818035e-18, -6.76700141e-04, -4.45265778e-04,  4.73860481e-04,
               8.15564005e-04, -9.82716515e-18, -9.21229631e-04, -6.04553835e-04,
               6.41514696e-04,  1.10071537e-03, -4.20554915e-18, -1.23522855e-03,
               -8.07883453e-04,  8.54360652e-04,  1.46092543e-03,  6.36161087e-18,
               -1.62841523e-03, -1.06151815e-03,  1.11894209e-03,  1.90727623e-03,
               -6.20575051e-18, -2.11299770e-03, -1.37339904e-03,  1.44363263e-03,
               2.45408111e-03, -2.65541693e-17, -2.70503951e-03, -1.75407273e-03,
               1.83965696e-03,  3.12070547e-03, -8.42299081e-18, -3.42667988e-03,
               -2.21820978e-03,  2.32277397e-03,  3.93459042e-03, -9.54543004e-18,
               -4.30988070e-03, -2.78719004e-03,  2.91616873e-03,  4.93649826e-03,
               -1.06402311e-17, -5.40306264e-03, -3.49373215e-03,  3.65569156e-03,
               6.19013106e-03, -1.16804363e-17, -6.78359455e-03, -4.39072933e-03,
               4.60000489e-03,  7.80104913e-03, -1.26404325e-17, -8.58318971e-03,
               -5.56954191e-03,  5.85199265e-03,  9.95739616e-03, -1.34965812e-17,
               -1.10450179e-02, -7.20212932e-03,  7.60934094e-03,  1.30287610e-02,
               -1.42278013e-17, -1.46709414e-02, -9.65336558e-03,  1.03043384e-02,
               1.78503323e-02, -1.48160876e-17, -2.06841671e-02, -1.38524813e-02,
               1.50931066e-02,  2.67818903e-02, -1.52469548e-17, -3.30339005e-02,
               -2.30572928e-02,  2.64413899e-02,  5.00614854e-02, -1.55097932e-17,
               -7.54344807e-02, -6.22603614e-02,  9.34966442e-02,  3.02767644e-01,
               4.00139650e-01,  3.02767644e-01,  9.34966442e-02, -6.22603614e-02,
               -7.54344807e-02, -1.55097932e-17,  5.00614854e-02,  2.64413899e-02,
               -2.30572928e-02, -3.30339005e-02, -1.52469548e-17,  2.67818903e-02,
               1.50931066e-02, -1.38524813e-02, -2.06841671e-02, -1.48160876e-17,
               1.78503323e-02,  1.03043384e-02, -9.65336558e-03, -1.46709414e-02,
               -1.42278013e-17,  1.30287610e-02,  7.60934094e-03, -7.20212932e-03,
               -1.10450179e-02, -1.34965812e-17,  9.95739616e-03,  5.85199265e-03,
               -5.56954191e-03, -8.58318971e-03, -1.26404325e-17,  7.80104913e-03,
               4.60000489e-03, -4.39072933e-03, -6.78359455e-03, -1.16804363e-17,
               6.19013106e-03,  3.65569156e-03, -3.49373215e-03, -5.40306264e-03,
               -1.06402311e-17,  4.93649826e-03,  2.91616873e-03, -2.78719004e-03,
               -4.30988070e-03, -9.54543004e-18,  3.93459042e-03,  2.32277397e-03,
               -2.21820978e-03, -3.42667988e-03, -8.42299081e-18,  3.12070547e-03,
               1.83965696e-03, -1.75407273e-03, -2.70503951e-03, -2.65541693e-17,
               2.45408111e-03,  1.44363263e-03, -1.37339904e-03, -2.11299770e-03,
               -6.20575051e-18,  1.90727623e-03,  1.11894209e-03, -1.06151815e-03,
               -1.62841523e-03,  6.36161087e-18,  1.46092543e-03,  8.54360652e-04,
               -8.07883453e-04, -1.23522855e-03, -4.20554915e-18,  1.10071537e-03,
               6.41514696e-04, -6.04553835e-04, -9.21229631e-04, -9.82716515e-18,
               8.15564005e-04,  4.73860481e-04, -4.45265778e-04, -6.76700141e-04,
               -2.61818035e-18,  5.96486488e-04,  3.46044525e-04, -3.24849132e-04,
               -4.93547331e-04,  1.43407337e-18,  4.35871545e-04,  2.53493963e-04,
               -2.38851727e-04, -3.64747185e-04, -1.59902685e-18,  3.27013057e-04,
               1.92150966e-04, -1.83295721e-04, -2.83982771e-04, -3.37634469e-18,
               2.63805938e-04,  1.58300486e-04, -1.54519828e-04, -2.45410597e-04,
               -1.24785049e-18
        },

        {
               -6.24189167e-19, -1.51736247e-04, -2.50124091e-04, -2.56243912e-04,
               -1.63110003e-04,  1.68888644e-18,  1.75585246e-04,  2.96704160e-04,
               3.11038308e-04,  2.02190675e-04, -7.99851622e-19, -2.25521513e-04,
               -3.86633690e-04, -4.10335347e-04, -2.69497379e-04, -7.17339993e-19,
               3.05158054e-04,  5.25839273e-04,  5.60148646e-04,  3.68804862e-04,
               -1.30964392e-18, -4.18400597e-04, -7.20759916e-04, -7.67046689e-04,
               -5.04259487e-04,  4.91566103e-18,  5.69592060e-04,  9.78602429e-04,
               1.03843166e-03,  6.80567270e-04, -2.10366405e-18, -7.63736153e-04,
               -1.30773583e-03, -1.38296933e-03, -9.03283497e-04, -3.18215092e-18,
               1.00684168e-03,  1.71829899e-03,  1.81125218e-03,  1.17926015e-03,
               -3.10418778e-18, -1.30645679e-03, -2.22314633e-03, -2.33683473e-03,
               -1.51734710e-03,  1.32827009e-17,  1.67251353e-03,  2.83934984e-03,
               2.97788661e-03,  1.92951789e-03, -4.21327687e-18, -2.11870047e-03,
               -3.59065703e-03, -3.75991702e-03, -2.43273923e-03,  4.77473389e-18,
               2.66477949e-03,  4.51167585e-03,  4.72045605e-03,  3.05221426e-03,
               -5.32236597e-18, -3.34068887e-03, -5.65536859e-03, -5.91753527e-03,
               -3.82732967e-03,  5.84268860e-18,  4.19426543e-03,  7.10735444e-03,
               7.44611266e-03,  4.82335293e-03, -6.32288970e-18, -5.30694687e-03,
               -9.01552008e-03, -9.47272829e-03, -6.15661243e-03,  6.75114513e-18,
               6.82908394e-03,  1.16582194e-02,  1.23173804e-02,  8.05562325e-03,
               -7.11690982e-18, -9.07097587e-03, -1.56260807e-02, -1.66798224e-02,
               -1.10367787e-02,  7.41117744e-18,  1.27889258e-02,  2.24232666e-02,
               2.44314897e-02,  1.65591201e-02, -7.62670213e-18, -2.04247093e-02,
               -3.73232646e-02, -4.28011649e-02, -3.09527871e-02,  7.75817696e-18,
               4.66407938e-02,  1.00781994e-01,  1.51344740e-01,  1.87199847e-01,
               2.00154455e-01,  1.87199847e-01,  1.51344740e-01,  1.00781994e-01,
               4.66407938e-02,  7.75817696e-18, -3.09527871e-02, -4.28011649e-02,
               -3.73232646e-02, -2.04247093e-02, -7.62670213e-18,  1.65591201e-02,
               2.44314897e-02,  2.24232666e-02,  1.27889258e-02, 7.41117744e-18,
               -1.10367787e-02, -1.66798224e-02, -1.56260807e-02, -9.07097587e-03,
               -7.11690982e-18,  8.05562325e-03,  1.23173804e-02,  1.16582194e-02,
               6.82908394e-03,  6.75114513e-18, -6.15661243e-03, -9.47272829e-03,
               -9.01552008e-03, -5.30694687e-03, -6.32288970e-18,  4.82335293e-03,
               7.44611266e-03,  7.10735444e-03,  4.19426543e-03,  5.84268860e-18,
               -3.82732967e-03, -5.91753527e-03, -5.65536859e-03, -3.34068887e-03,
               -5.32236597e-18,  3.05221426e-03,  4.72045605e-03,  4.51167585e-03,
               2.66477949e-03,  4.77473389e-18, -2.43273923e-03, -3.75991702e-03,
               -3.59065703e-03, -2.11870047e-03, -4.21327687e-18,  1.92951789e-03,
               2.97788661e-03,  2.83934984e-03,  1.67251353e-03,  1.32827009e-17,
               -1.51734710e-03, -2.33683473e-03, -2.22314633e-03, -1.30645679e-03,
               -3.10418778e-18,  1.17926015e-03,  1.81125218e-03,  1.71829899e-03,
               1.00684168e-03, -3.18215092e-18, -9.03283497e-04, -1.38296933e-03,
               -1.30773583e-03, -7.63736153e-04, -2.10366405e-18,  6.80567270e-04,
               1.03843166e-03,  9.78602429e-04,  5.69592060e-04,  4.91566103e-18,
               -5.04259487e-04, -7.67046689e-04, -7.20759916e-04, -4.18400597e-04,
               -1.30964392e-18,  3.68804862e-04,  5.60148646e-04,  5.25839273e-04,
               3.05158054e-04, -7.17339993e-19, -2.69497379e-04, -4.10335347e-04,
               -3.86633690e-04, -2.25521513e-04, -7.99851622e-19,  2.02190675e-04,
               3.11038308e-04,  2.96704160e-04,  1.75585246e-04,  1.68888644e-18,
               -1.63110003e-04, -2.56243912e-04, -2.50124091e-04, -1.51736247e-04,
               -6.24189167e-19
       }
};

template <typename T> class FIR_Filter {
        private:
        unsigned int order;
        unsigned int  *current_index;
        unsigned char *filter_enable;
        T *coeff;
        T *buffer;
	static FIR_Filter *single_instance_ptr;
        FIR_Filter()
        {
            order = 0;
            current_index = NULL;
            filter_enable = NULL;
            coeff = NULL;
            buffer= NULL;
        };

        public:
        // deleting copy constructor so that no copy instance of the class can be created...
        FIR_Filter(const FIR_Filter& copy_obj) = delete;

        static FIR_Filter* getInstance( )
        {
            // when there is no instance of the class , create one and return it. if there is an instance, just return that instance..
            if (single_instance_ptr == NULL) {
                single_instance_ptr = new FIR_Filter();
                return single_instance_ptr;
            } else {
                // implies instance is already created ..Therefore returning the same...
                return single_instance_ptr;
            }
        }

        uint8_t FIRFilter_Initialize(const unsigned int order, const unsigned int num_axes)
        {
            static bool isInit = false;
            if (true == isInit)
                return 0;

            //initializing the filter with respective order
            //for B2 filter is  PASS THROUGH Mode
            this->order = order;
            if (order) {
                //filter->filter_enable[i] checks if the axis i has atleast order number of elements in it to start the filtering.
                this->filter_enable = (unsigned char*)malloc(num_axes * sizeof(unsigned char));
                assert(filter_enable !=NULL);
                memset(filter_enable, 0, num_axes * sizeof(unsigned char));

                //current_index briefs about where we are in buffer, in respective to the axis provided.
                this->current_index = (unsigned int*)malloc(num_axes * sizeof(unsigned int));
                assert(current_index !=NULL);
                memset(current_index, 0, num_axes * sizeof(unsigned int));

                //allocating circular buffer with sufficient space to hold (order+1) * (num_axes) elements.
                this->buffer = (T*)malloc( (order+1) * num_axes * sizeof(double));
                if (NULL == buffer) {
                    isInit = false;
                    return 1;
                }

                memset(buffer, 0, (( order+1) * num_axes * sizeof(double)) );

                isInit = true;
            }

            return 0;
        }
	
        T FIRFilter_Update( double in , int axis_index)
        {
            double output=0.0f;
            output = in;

            if (order) {
                // initializing filter_co-efficients based on axis_index .. 0 indicates gyro and 1 indicates accel....
                if (26 == order) {
                    coeff = coeff_26<T>[axis_index/IMU_AXES_XYZ];
                } else if (200 == order) {
                    coeff = coeff_200<T>[axis_index/IMU_AXES_XYZ];
                } else {
                    coeff = coeff_0<T>;
                }

                unsigned int bufferIndex = current_index[axis_index];
                // Wrapping around the circular buffer to starting position when buffer gets full. 
                // This case can also occur when filter is about to start filtering the previous inputs.
                if (current_index[axis_index] == order) {
                    current_index[axis_index] = 0;
                } else {
                    current_index[axis_index]++;
                    filter_enable[axis_index] = 1;
                }

                if (order == 0)
                    filter_enable[axis_index] = 0;

                // Storing the latest sample into the circular buffer
                buffer[ (bufferIndex * IMU_AXES_XYZ * IMU_PARAM ) + axis_index ] = in;

                // Checking if filter is ready to start. ( Requires circular buffer to contain atleast order number of elements for it)
                if (filter_enable[axis_index] == 1) {
                    output = 0.0f;
                    for (int n = 0; n <= order; n++) {
                        output +=  coeff[n] * (buffer[ (bufferIndex * IMU_AXES_XYZ * IMU_PARAM) + axis_index]);
                        //decrementing buffer index and when it becomes 0 , index is wrapped around with filter order.
                        bufferIndex = ( (bufferIndex > 0 )  ? (--bufferIndex) : (order) );
                    }
                } else {
                    //LOG_D(TAG,"Filtering not required for now ");
                    //copying the input to output when filter is not active.
                    output = in;
                }
            }
            return output;
        }
};

typedef enum {
    e_Gyroindex_x  = 0,
    e_Gyroindex_y  = 1,
    e_Gyroindex_z  = 2,
    e_Accelindex_x = 3,
    e_Accelindex_y = 4,
    e_Accelindex_z = 5,
} IMU_Axes;

const char* path_to_store_filter_output1="/home/ubuntu/.nddevice/filter_less.csv";
const char* path_to_store_filter_output2="/home/ubuntu/.nddevice/filter_included.csv";
const char* path_to_store_filter_output3="/home/ubuntu/.nddevice/filter_included_dec.csv";
FILE *filter_less = fopen(path_to_store_filter_output1,"wb");
FILE *filter_included = fopen(path_to_store_filter_output2,"wb");
FILE *filter_included_dec = fopen(path_to_store_filter_output3,"wb");

template<> FIR_Filter<double> *FIR_Filter<double>::single_instance_ptr = NULL;
static struct imu_data *data_cb = NULL;

static bool imu_recover_task(void *args)
{
    LOG_I(TAG, "MSP reset getting called");

    sleep(2);

    if (imu_handle != -1) {
        if (false == imu_dev_disable_accel(imu_handle))
            LOG_E(TAG,"Cannot disable Accel");

        if (false == imu_dev_disable_gyro(imu_handle))
            LOG_E(TAG,"Cannot disable Gyro");
    }

    imu_dev_close(imu_handle);
    sleep(2);

    LOG_I(TAG, "%s: recovering IMU started \n", __func__);

    switch (nd_device_obj->getDeviceType()) {
	    case eBagheera_2:
		    nd_remove_kernel_module(IMU_ICM20602, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    nd_remove_kernel_module(IMU_MPU_B2, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    break;
	    case eBagheera_3:
		    nd_remove_kernel_module(IMU_ICM42670, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    nd_remove_kernel_module(IMU_MPU_B3, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    break;
	    default:
		    LOG_E(TAG," Unknow Device Type: %d", nd_device_obj->getDeviceType());
		    break;
    }
    sleep(1);

    switch (nd_device_obj->getDeviceType()) {
	    case eBagheera_2:
		    nd_insert_kernel_module(IMU_MPU_B2, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    nd_insert_kernel_module(IMU_ICM20602, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    break;
	    case eBagheera_3:
		    nd_insert_kernel_module(IMU_MPU_B3, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    nd_insert_kernel_module(IMU_ICM42670, IMU_MODULE_INSERT_RETRY_COUNT, IMU_MODULE_INSERT_TIMEOUT);
		    break;
	    default:
		    LOG_E(TAG," Unknow Device Type: %d", nd_device_obj->getDeviceType());
		    break;
    }
    sleep(2);

    // Logic to initialize IMU

    imu_dev_open();

    if (imu_handle == -1)
        LOG_C(TAG,"Cannot open IMU !!");

    sleep(2);

    int value = imu_register_cb(&imu_handle, imu_callback_new);
    if (value) {
        LOG_I(TAG, "Failed in imu_register_cb");
    } else {
        if (imu_handle != -1) {
            if (false == imu_dev_enable_accel(imu_handle))
                LOG_C(TAG, "Cannot enable Accel");

            if (false == imu_dev_enable_gyro(imu_handle))
                LOG_C(TAG, "Cannot enable gyro");
        } else {
            LOG_I(TAG, "IMU callback is not registered");
        }
    }
    LOG_I(TAG, "%s: recovering IMU done \n", __func__);

    return true;
}

void *imu_recover_thread(void *args) {

    struct timespec time_to_wait = {5, 0};
    struct timeval now;

    while(1) {
        gettimeofday(&now, NULL);
        time_to_wait.tv_sec = now.tv_sec + 60;
        pthread_mutex_lock(&imu_recover_mutex);
        if(ETIMEDOUT == pthread_cond_timedwait(&imu_recover_cond, &imu_recover_mutex, &time_to_wait)) {
            LOG_D(TAG, "%s: hitting timer \n", __func__);
            pthread_mutex_unlock(&imu_recover_mutex);
        } else {

            pthread_mutex_unlock(&imu_recover_mutex);

            task_result_t task_result = nd_timed_task(imu_recover_task,
                    IMU_RECOVERY_TIME_MAX, NULL, "imu_recovery");
            if (task_result != TASK_SUCCESS) {
                LOG_E (TAG, "nd_timed_task for imu_recover_task timedout");
            }
        }
    }
}

static bool update_from_config() {
    // Marks which all cameras need to be enabled
    // after reading config file.
    Config_parser c(BAGHEERACONFIG_INI);
    if (c.getParseStatus() != true)
        return false;

    if (c.isPresent ("imu","gyro_scale"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int grate;
        string_to_integer(c.getConfig("imu","gyro_scale","1", get_override_val, is_val_overridden), grate);
        if (grate >= 0 && grate <= 3) {
                gyro_scale = grate;
        }
        else
             LOG_I (TAG, "No valid gyro_scale config found, using default: %d", gyro_scale);
    }
    else
    {
        LOG_I (TAG, "No gyro_scale found, using default: %d", gyro_scale);
    }
    
    if (c.isPresent ("imu","accel_scale"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int arate;
        string_to_integer(c.getConfig("imu","accel_scale","1", get_override_val, is_val_overridden), arate);
        if (arate >= 0 && arate <= 3) {
                accel_scale = arate;
        }
        else
             LOG_I (TAG, "No valid accel_scale config found, using default: %d", accel_scale);
    }
    else
    {
        LOG_I (TAG, "No accel_scale found, using default: %d", accel_scale);
    }

    if (c.isPresent ("imu","sample_rate"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int srate = 0;
        string_to_integer(c.getConfig("imu","sample_rate","20", get_override_val, is_val_overridden), srate);
        if (!nd_device_obj->is_valid_imu_sample_rate(srate))
            LOG_I (TAG, "No valid sample_rate found, using default: %d", srate);

        sample_rate = srate;
    }
    else
    {
        nd_device_obj->is_valid_imu_sample_rate(sample_rate);
        LOG_I (TAG, "No sample-rate found, using default: %d", sample_rate);
    }

    if (c.isPresent ("imu","accel_dlpf_bw"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int drate;
        string_to_integer(c.getConfig("imu","accel_dlpf_bw","5", get_override_val, is_val_overridden), drate);
        if (drate >= -1 && drate <= 7) 
                accel_dlpf_bw = drate;
        else
             LOG_I (TAG, "No valid accel_dlpf_bw found, using default: %d", accel_dlpf_bw);
    }
    else
    {
        LOG_I (TAG, "No accel_dlpf_bw found, using default: %d", accel_dlpf_bw);
    }

    if (c.isPresent ("imu","gyro_dlpf_bw"))
    {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int grate;
        string_to_integer(c.getConfig("imu","gyro_dlpf_bw","5", get_override_val, is_val_overridden), grate);
        if (grate >= -1 && grate <= 7)
                gyro_dlpf_bw = grate;
        else
             LOG_I (TAG, "No valid gyro_dlpf_bw found, using default: %d", gyro_dlpf_bw);
    }
    else
    {
        LOG_I (TAG, "No gyro_dlpf_bw found, using default: %d", gyro_dlpf_bw);
    }

    if (c.isPresent ("imu", "invert_accel_x"))
    {
        string str_invert_accel_x = c.getConfig("imu", "invert_accel_x","");
        if (str_invert_accel_x == "true"){
		invert_accel_x = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_accel_y"))
    {
       string str_invert_accel_y = c.getConfig("imu", "invert_accel_y","");
        if (str_invert_accel_y == "true"){
		invert_accel_y = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_accel_z"))
    {
        string str_invert_accel_z = c.getConfig("imu", "invert_accel_z","");
        if (str_invert_accel_z == "true"){
		invert_accel_z = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_x"))
    {
        string str_invert_gyro_x = c.getConfig("imu", "invert_gyro_x","");
        if (str_invert_gyro_x == "true"){
		invert_gyro_x = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_y"))
    {
        string str_invert_gyro_y = c.getConfig("imu", "invert_gyro_y","");
        if (str_invert_gyro_y == "true"){
		invert_gyro_y = -1; 
        }
    }
    if (c.isPresent ("imu", "invert_gyro_z"))
    {
        string str_invert_gyro_z = c.getConfig("imu", "invert_gyro_z","");
        if (str_invert_gyro_z == "true"){
		invert_gyro_z = -1; 
        }
    }

    if (c.isPresent ("imu","lpf_fir_order")) {
        bool get_override_val = true;
        bool is_val_overridden = false;
        int lpf_order = 0;
        string_to_integer(c.getConfig("imu","lpf_fir_order","0", get_override_val, is_val_overridden), lpf_order);
        if ( lpf_order==0 || lpf_order==26 || lpf_order==200 )
                lpf_fir_order = lpf_order;
        else
             LOG_I (TAG, "No valid lpf_fir_order found, using default: %d", lpf_fir_order);
    } else {
             LOG_I (TAG, "No lpf_fir_order found, using default: %d", lpf_fir_order);
    }

    LOG_I(TAG, "Sample Rate: %d", sample_rate);
    LOG_I(TAG, "Accel Scale: %d", accel_scale);
    LOG_I(TAG, "Accel DLPF BW: %d", accel_dlpf_bw);
    LOG_I(TAG, "Gyro Scale: %d", gyro_scale);
    LOG_I(TAG, "Gyro DLPF BW: %d", gyro_dlpf_bw);
    LOG_I(TAG, "Invert Accel: x %d y %d z %d", invert_accel_x, invert_accel_y, invert_accel_z );
    LOG_I(TAG, "Invert Gyro: x %d y %d z %d", invert_gyro_x, invert_gyro_y, invert_gyro_z );
    LOG_I(TAG, "LPF FIR ORDER: %d", lpf_fir_order);

    return true;
}

int imu_dev_open()
{
    int value = -1;
    value = imu_open(&imu_handle);
    if( IMU_SUCCESS != value ) {
        LOG_E(TAG, "Failed in imu_open, %d", value);
        //return -1;
    }

    update_from_config();

    if(imu_recover_tid == -1) {
        if (!pthread_create (&imu_recover_tid, NULL, imu_recover_thread, NULL)) {
            LOG_I (TAG, "Thread created for IMU recovery");
        }
    }

    return imu_handle;
}

bool imu_dev_close(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( IMU_SUCCESS != imu_close(&imu_handle) ) {
        return false;
    }

    return true;
}


bool imu_dev_config( int handle, string key, string value ) {

    if( handle != imu_handle ) {
        return false;
    }

    return true;
}

bool set_accel_dlpf_bw() {

    imu_enable_flag = 0; // Need To Enable IMU after DLPF Setting so clearing the flag.
    nd_device_obj->is_valid_imu_dlpf_bw(gyro_dlpf_bw, accel_dlpf_bw);

    if ( IMU_SUCCESS != set_dlpf_rate(&imu_handle, gyro_dlpf_bw, accel_dlpf_bw) ) {
        return false;
    }
    return true;
}

bool set_gyro_dlpf_bw() {

    imu_enable_flag = 0; // Need To Enable IMU after DLPF Setting so clearing the flag.
    nd_device_obj->is_valid_imu_dlpf_bw(gyro_dlpf_bw, accel_dlpf_bw);

    if ( IMU_SUCCESS != set_dlpf_rate(&imu_handle, gyro_dlpf_bw, accel_dlpf_bw) ) {
        return false;
    }
    return true;
}

bool imu_dev_enable_accel(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == set_accel_dlpf_bw() ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    }

    imu_enable_flag |= accel_enable_mask;

    return true;
}

bool imu_dev_enable_gyro(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == set_gyro_dlpf_bw() ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    }

    imu_enable_flag |= gyro_enable_mask;

    return true;
}

bool imu_dev_enable_magneto( int handle ) {

    if( handle != imu_handle ) {
        return false;
    }

    if( false == imu_dev_enable() ) {
        return false;
    }

    imu_enable_flag |= magneto_enable_mask;

    return true;
}

bool imu_dev_disable_accel( int handle ) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~accel_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

bool imu_dev_disable_gyro(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~gyro_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

bool imu_dev_disable_magneto(int handle) {

    if( handle != imu_handle ) {
        return false;
    }

    imu_enable_flag &= (~magneto_enable_mask);

    if( false == imu_dev_disable() ) {
        return false;
    }

    return true;
}

static int imu_callback_new (struct imu_data *data_cb_in)
{
    if(NULL == data_cb_in ) {
        LOG_E(TAG, "NULL data_cb_in");
        return 0;
    }

    static uint64_t temp_downsample_counter = -1;
    static uint64_t imu_downsample_counter =  -1;
    static float imu_temp_f = 0.0;

    Imu::val_t val_a, val_g, val_m, val_temp;
    uint64_t epoch_time_ns = get_system_time_ns();

    if (data_cb_in->ts_valid == 0)
        LOG_E (TAG, "IMU TS might not be valid");

    uint64_t epoch_time_each_interval_before = epoch_time_ns;
    FIR_Filter<double> *filter = FIR_Filter<double> ::getInstance();
    data_cb->ts_valid   =   data_cb_in->ts_valid;
    data_cb->gyro_time  =   data_cb_in->gyro_time;
    data_cb->accel_time =   data_cb_in->accel_time;
    data_cb->wallclock_time = data_cb_in->wallclock_time;
    data_cb->gyro_x = filter->FIRFilter_Update(data_cb_in->gyro_x,e_Gyroindex_x);
    data_cb->gyro_y = filter->FIRFilter_Update(data_cb_in->gyro_y,e_Gyroindex_y);
    data_cb->gyro_z = filter->FIRFilter_Update(data_cb_in->gyro_z,e_Gyroindex_z);
    data_cb->accel_x = filter->FIRFilter_Update(data_cb_in->accel_x,e_Accelindex_x);
    data_cb->accel_y = filter->FIRFilter_Update(data_cb_in->accel_y,e_Accelindex_y);
    data_cb->accel_z = filter->FIRFilter_Update(data_cb_in->accel_z,e_Accelindex_z);

    uint64_t epoch_time_ns_after = get_system_time_ns();
    if (access( "/dev/shm/fir_filter.txt", F_OK) != -1) {

        fprintf(filter_included,"%lf, %lf, %lf, %lf, %lf, %lf, %lu, %lu\n", data_cb->accel_x,data_cb->accel_y,data_cb->accel_z,data_cb->gyro_x,data_cb->gyro_y,data_cb->gyro_z, epoch_time_ns/*data_cb->accel_time*/, (epoch_time_ns_after-epoch_time_each_interval_before ) );

        fprintf(filter_less,"%lf, %lf, %lf, %lf, %lf, %lf, %lu, %lu\n", data_cb_in->accel_x,data_cb_in->accel_y,data_cb_in->accel_z,data_cb_in->gyro_x,data_cb_in->gyro_y,data_cb_in->gyro_z, epoch_time_ns /*data_cb_in->accel_time*/, (epoch_time_ns_after-epoch_time_each_interval_before ) );
    }

    static float a_x = 0, a_y = 0, a_z = 0;
    static float g_x = 0, g_y = 0, g_z = 0;

    // for B2 there is no SW LPF.
    // SW LPF is in passthrough mode.
    if (lpf_fir_order) {
        // Decimate -> Under Sampling to 20 Hz
        ++imu_downsample_counter;

        if ((imu_downsample_counter + 1) % 5 == 0) {
            data_cb->accel_x  = ((a_x + data_cb->accel_x)/3);
            data_cb->accel_y  = ((a_y + data_cb->accel_y)/3);
            data_cb->accel_z  = ((a_z + data_cb->accel_z)/3);

            data_cb->gyro_x  = ((g_x + data_cb->gyro_x)/3);
            data_cb->gyro_y  = ((g_y + data_cb->gyro_y)/3);
            data_cb->gyro_z  = ((g_z + data_cb->gyro_z)/3);

            imu_downsample_counter = -1;
            a_x = 0; a_y = 0; a_z = 0;
            g_x = 0; g_y = 0; g_z = 0;

        } else if ((imu_downsample_counter < 3) &&  ((imu_downsample_counter + 1) % 2 == 0)) {
            data_cb->accel_x  = ((a_x + data_cb->accel_x)/2);
            data_cb->accel_y  = ((a_y + data_cb->accel_y)/2);
            data_cb->accel_z  = ((a_z + data_cb->accel_z)/2);

            data_cb->gyro_x  = ((g_x + data_cb->gyro_x)/2);
            data_cb->gyro_y  = ((g_y + data_cb->gyro_y)/2);
            data_cb->gyro_z  = ((g_z + data_cb->gyro_z)/2);

            a_x = 0; a_y = 0; a_z = 0;
            g_x = 0; g_y = 0; g_z = 0;
        } else {
            a_x += data_cb->accel_x;
            a_y += data_cb->accel_y;
            a_z += data_cb->accel_z;

            g_x += data_cb->gyro_x;
            g_y += data_cb->gyro_y;
            g_z += data_cb->gyro_z;

            return 0;
        }
    }

    if (data_cb-> ts_valid == 0) {
        LOG_E (TAG, "IMU TS might not be valid");
    }

    // change the filter_output_to_csv_required to 1 if you want the data to be stored in csv . else put it to 0.
    if (access( "/dev/shm/fir_filter.txt", F_OK ) != -1) {
        fprintf(filter_included_dec,"%lf, %lf, %lf, %lf, %lf, %lf, %lu, %lu\n", data_cb->accel_x,data_cb->accel_y,data_cb->accel_z,data_cb->gyro_x,data_cb->gyro_y,data_cb->gyro_z, epoch_time_ns /*data_cb->accel_time*/, (epoch_time_ns_after-epoch_time_each_interval_before ) );
    }

    uint64_t monotonic_time = get_system_monotonic_time();
    // changing sign and axis to align with Analytics
    val_a.x = data_cb->accel_z * 9.80665f;
    // to address the issue discussed in JIRA::BHAGEERA-439
    // observed that we need to swap left and right to inline with akhela implementation
    val_a.y = data_cb->accel_x * 9.80665f * -1;
    val_a.z = data_cb->accel_y * 9.80665f;

    val_g.x = data_cb->gyro_z;
    val_g.y = data_cb->gyro_x;
    val_g.z = data_cb->gyro_y;

    //val_m.x = data_cb_in->compass_z;
    //val_m.y = data_cb_in->compass_y;
    //val_m.z = data_cb_in->compass_x;

    //setting user space epoch time so that we have atleast one time value to compare
    //agaist video file's end time while writing to metadata
    val_a.clock_time = (data_cb->wallclock_time == 0) ? epoch_time_ns : data_cb->wallclock_time;
    val_g.clock_time = (data_cb->wallclock_time == 0) ? epoch_time_ns : data_cb->wallclock_time;
    //val_m.clock_time = epoch_time_ns;

    val_a.raw_time = data_cb->accel_time;
    val_g.raw_time = data_cb->gyro_time;
    //val_m.raw_time = data_cb_in->compass_time;

    temp_downsample_counter = (temp_downsample_counter + 1) % temp_decimation_factor;
    if (temp_downsample_counter == 0) {
        temp_downsample_counter++;
        unsigned long long tmp_ul;
        long long temp_time_monotonic_raw = 0;
        long tmp_l;
        unsigned int imu_temp = -1;

        if (!imu_temp_read(&imu_handle, &tmp_l, &tmp_ul)) {
            imu_temp = (unsigned int)tmp_l;
            imu_temp_f = (float)imu_temp;

            val_temp.x = (float)imu_temp;
            val_temp.y = 0;
            val_temp.z = 0;
            val_temp.raw_time = get_system_monotonic_time_ns();
            val_temp.clock_time = (uint64_t)tmp_ul;

            LOG_D (TAG, "IMU temperature callback");
            temp_cb(val_temp);
        } else {
            LOG_I(TAG, "imu_callback_new(): temperature read failure!");
        }
    }

    val_a.x = invert_accel_x * val_a.x;
    val_a.y = invert_accel_y * val_a.y;
    val_a.z = invert_accel_z * val_a.z;
    
    val_g.x = invert_gyro_x * val_g.x;
    val_g.y = invert_gyro_y * val_g.y;
    val_g.z = invert_gyro_z * val_g.z;

   // send a 1 message to ndc and seperate there
    if (imu_enable_flag)
        cb(val_a, val_g, val_m);

    return 0;
}

bool imu_dev_reg_cb( int handle, Imu::imu_callback_t *cb ) {

    if( handle != imu_handle ) {
        return false;
    }

    if( cb == NULL ) {
        return false;
    }

    ::cb = cb;

    // create a single instance pointer for the class FIR_Filter

    FIR_Filter<double> *filter = FIR_Filter<double> ::getInstance();
    filter->FIRFilter_Initialize( lpf_fir_order , (IMU_AXES_XYZ * IMU_PARAM ));
    assert(filter!=NULL && "Filter Initialization is successful ");

    if( NULL == data_cb) {
        data_cb = (imu_data*)malloc(sizeof(imu_data));
        assert( data_cb != NULL );
        memset( data_cb, 0, sizeof(imu_data));
    }

    int value=imu_register_cb(&imu_handle, &imu_callback_new);
    if(value) {
        LOG_I(TAG, "Failed in imu_register_cb");
        pthread_mutex_lock(&imu_recover_mutex);
        pthread_cond_signal(&imu_recover_cond);
        pthread_mutex_unlock(&imu_recover_mutex);

        return false;
    }

    return true;
}

bool imu_dev_reg_temp_cb( int handle, Imu::imu_temperature_callback_t *temp_cb ) {
    ::temp_cb = temp_cb;
    return true;
}

static bool imu_dev_enable( ) {

    if( 0 == imu_enable_flag ) {

        if( IMU_SUCCESS != set_sample_rate( &imu_handle, sample_rate ) ) {
            return false;
        }
        else {
            LOG_I(TAG, "IMU: set_sample_rate success, sample rate = %d", sample_rate);
        }

        if( IMU_SUCCESS != imu_enable( &imu_handle, gyro_scale, accel_scale ) ) {
            return false;
        }
        else {
            imu_enable_flag = ( accel_enable_mask | gyro_enable_mask );
            LOG_I(TAG, "IMU: imu_enable success, gyro_scale = %d, accel_scale = %d", gyro_scale, accel_scale);
        }
    }
    return true;
}

static bool imu_dev_disable( ) {
    if( 0 == imu_enable_flag ) {
        if( IMU_SUCCESS != imu_disable( &imu_handle ) ) {
            return false;
        }
    }

    return true;
}


