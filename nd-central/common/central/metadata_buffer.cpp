#include "metadata_buffer.h"
#include <sstream>
#include "nd_central.h"
#include <atomic>
#include <iomanip>

#define TAG "META"
#define NUM_LOGS_CAP 50

extern nd_central_ctx ctx;
extern can_status_t can_status_updated;
// Below atomic variables are used for getting partial data
std::atomic<int> accel_size_atomic;
std::atomic<int> imu_temp_size_atomic;
std::atomic<int> gyro_size_atomic;
std::atomic<int> gps_size_atomic;
std::atomic<int> gps_pps_size_atomic;
std::atomic<int> ublox_size_atomic;
std::atomic<int> ublox_constellation_size_atomic;
std::atomic<int> obd_size_atomic;
std::atomic<int> freport_size_atomic;
std::atomic<int> ireport_size_atomic;
std::atomic<int> frameinfo_size_atomic;
std::atomic<int> audio_size_atomic;
std::atomic<int> usralert_size_atomic;

meta_buff_t::meta_buff_t() {
    meta_buff_mutex = PTHREAD_MUTEX_INITIALIZER;
    audio_mutex = PTHREAD_MUTEX_INITIALIZER;
}

bool meta_buff_t::push(Imu::imu_sensor_t imu, Imu::val_t &val, bool hdmaps_enabled) {
    switch( imu ) {
        case Imu::IMU_ACCEL:
            pthread_mutex_lock(&meta_buff_mutex);
            accel.push_back(val);
            accel_size_atomic = accel.size();
            pthread_mutex_unlock(&meta_buff_mutex);
            break;

        case Imu::IMU_TEMP:
        	if ((hdmaps_enabled == true) && (ctx.imu_data == true)) {
                pthread_mutex_lock(&meta_buff_mutex);
                imu_temperature.push_back(val);
                imu_temp_size_atomic = imu_temperature.size();
                pthread_mutex_unlock(&meta_buff_mutex);
        	}
            break;
        case Imu::IMU_GYRO:
            pthread_mutex_lock(&meta_buff_mutex);
            gyro.push_back(val);
            gyro_size_atomic = gyro.size();
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        case Imu::IMU_MAGNETO:
            pthread_mutex_lock(&meta_buff_mutex);
            magneto.push_back(val);
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        default:
            LOG_E(TAG, "Invalid imu sensor: %d", imu);
            return false;
    }

    return true;
}

bool meta_buff_t::push(Gps::gps_pps_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    gps_pps.push_back(val);
    gps_pps_size_atomic = gps_pps.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Gps::gps_metadata_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    gps.push_back(val);
    gps_size_atomic = gps.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Obd::obd_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    obd.push_back(val);
    obd_size_atomic = obd.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Obd::fr_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    freport.push_back(val);
    freport_size_atomic = freport.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Obd::ir_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ireport.push_back(val);
    ireport_size_atomic = ireport.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Audio::audio_t &val) {
    pthread_mutex_lock(&audio_mutex);
    audio.push_back(val);
    audio_size_atomic = audio.size();
    pthread_mutex_unlock(&audio_mutex);
    return true;
}

bool meta_buff_t::push(FrameInfo::frameinfo_t &val) {

    pthread_mutex_lock(&meta_buff_mutex);
    frameinfo.push_back(val);
    frameinfo_size_atomic = frameinfo.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;

}

bool meta_buff_t::push(Ublox::ublox_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ublox.push_back(val);
    ublox_size_atomic = ublox.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(Ublox::ublox_constellation_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ublox_constellation.push_back(val);
    ublox_constellation_size_atomic = ublox_constellation.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push(genmeta_usralert_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    usralert.push_back(val);
    usralert_size_atomic = usralert.size();
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Imu::imu_sensor_t imu, Imu::val_t &val ) {
    switch( imu ) {
        case Imu::IMU_ACCEL:
            pthread_mutex_lock(&meta_buff_mutex);
            accel.push_front(val);
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        case Imu::IMU_TEMP:
            pthread_mutex_lock(&meta_buff_mutex);
            imu_temperature.push_front(val);
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        case Imu::IMU_GYRO:
            pthread_mutex_lock(&meta_buff_mutex);
            gyro.push_front(val);
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        case Imu::IMU_MAGNETO:
            pthread_mutex_lock(&meta_buff_mutex);
            magneto.push_front(val);
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        default:
            LOG_E(TAG, "Invalid imu sensor: %d", imu);
            return false;
    }

    return true;
}

bool meta_buff_t::push_front(Gps::gps_metadata_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    gps.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Gps::gps_pps_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    gps_pps.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Obd::obd_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    obd.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Obd::fr_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    freport.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Obd::ir_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ireport.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Audio::audio_t &val) {
    pthread_mutex_lock(&audio_mutex);
    audio.push_front(val);
    pthread_mutex_unlock(&audio_mutex);
    return true;
}

bool meta_buff_t::push_front(FrameInfo::frameinfo_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    frameinfo.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Ublox::ublox_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ublox.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::push_front(Ublox::ublox_constellation_data_t &val) {
    pthread_mutex_lock(&meta_buff_mutex);
    ublox_constellation.push_front(val);
    pthread_mutex_unlock(&meta_buff_mutex);
    return true;
}

bool meta_buff_t::clear( ) {

    pthread_mutex_lock(&meta_buff_mutex);
    accel.clear();
    accel_size_atomic = 0 ;
    imu_temperature.clear();
    imu_temp_size_atomic = 0;
    gyro.clear();
    gyro_size_atomic = 0 ;
    magneto.clear();
    gps.clear();
    gps_size_atomic = 0 ;
    gps_pps.clear();
    gps_pps_size_atomic = 0 ;
    ublox.clear();
    ublox_size_atomic = 0;
    ublox_constellation.clear();
    ublox_constellation_size_atomic = 0;
    obd.clear();
    obd_size_atomic = 0 ;
    freport.clear();
    freport_size_atomic = 0 ;
    ireport.clear();
    ireport_size_atomic = 0 ;
    frameinfo.clear();
    frameinfo_size_atomic = 0;
    usralert.clear();
    usralert_size_atomic = 0;
    pthread_mutex_unlock(&meta_buff_mutex);
    pthread_mutex_lock(&audio_mutex);
    audio.clear();
    audio_size_atomic = 0 ;
    pthread_mutex_unlock(&audio_mutex);
}

void meta_buff_t::add_ignition_status_records(bool status, int64_t time) {
    ignition_status_records_t ignition_data ;
    ignition_data.status = status;
    ignition_data.time =  time;
    ignition_status_records.push_back( ignition_data );
}

void meta_buff_t::pick_last_second_metadata(bool hdmaps_mode_enabled, bool imu_data, bool ublox_enabled)
{
    stringstream ss_meta;
    static bool is_gps_data_present = false, is_gps_pps_data_present = false, onetime = true;
    static int64_t gps_indx_val = 0, gps_pps_indx_val = 0;

    /* ACCEL starts */
    static int accel_size_before = 0;
    int accel_size_now = accel_size_atomic ;
    if(accel_size_before > accel_size_now){
        accel_size_before = 0;
    }
    /* ImuData starts */
    for(int i = accel_size_before; i < accel_size_now; i++){
        Imu::val_t &val_a = accel[i];
        Imu::val_t &val_g = gyro[i];
        ctx.fstream_partial_meta_file << endl ;
        ss_meta.str("");
        ss_meta << "ImuData" << ", " << val_a.x << ", " << val_a.y << ", " <<  val_a.z <<", " << val_g.x << ", " << val_g.y << ", " <<  val_g.z <<", " << val_a.raw_time ;
        ctx.fstream_partial_meta_file << ss_meta.str() ;
        ctx.fstream_partial_meta_file << endl ;
    }
    /* ImuData ends */

    for(int i = accel_size_before; i < accel_size_now; i++){
        ctx.fstream_partial_meta_file << endl ;
        ss_meta.str("");
        Imu::val_t &val_a = accel[i];
        ss_meta << "Acel" << ", " << val_a.x << ", " << val_a.y << ", " <<  val_a.z <<", " << static_cast <int64_t>(val_a.clock_time / 1000)  ;
        ctx.fstream_partial_meta_file << ss_meta.str() ;
        ctx.fstream_partial_meta_file << endl ;
    }
    accel_size_before = accel_size_now ;
    /* ACCEL ends */

    /* GYRO starts */
    static int gyro_size_before = 0;
    int gyro_size_now = gyro_size_atomic ;
    if(gyro_size_before > gyro_size_now){
        gyro_size_before = 0;
    }
    for(int i = gyro_size_before; i < gyro_size_now; i++){
        ctx.fstream_partial_meta_file << endl ;
        ss_meta.str("");
        Imu::val_t &val_g = gyro[i];
        ss_meta << "Gyro" << ", " << val_g.x << ", " << val_g.y << ", " <<  val_g.z <<", " << static_cast <int64_t>(val_g.clock_time / 1000)  ;
        ctx.fstream_partial_meta_file << ss_meta.str() ;
        ctx.fstream_partial_meta_file << endl ;
    }
    gyro_size_before = gyro_size_now ;
    /* GYRO ends */

    /* IMUTEMP starts */
    if (hdmaps_mode_enabled == true && (imu_data == true)) {
        static int imutemp_size_before = 0;
        int imutemp_size_now = imu_temp_size_atomic;
        if(imutemp_size_before > imutemp_size_now){
            imutemp_size_before = 0;
        }
        for(int i = imutemp_size_before; i < imutemp_size_now; i++){
            ctx.fstream_partial_meta_file << endl ;
            ss_meta.str("");
            Imu::val_t &val_temp = imu_temperature[i];
            ss_meta << "imutemp" << ", " << val_temp.x << ", " << val_temp.y << ", " <<  val_temp.z <<", " << val_temp.raw_time  ;

            ctx.fstream_partial_meta_file << ss_meta.str() ;
            ctx.fstream_partial_meta_file << endl ;
        }
        imutemp_size_before = imutemp_size_now;
    }
    /* IMUTEMP ends */

    /* GPS starts */
    while (1) {
    	if (ctx.off_duty_privacy == true || ctx.geofence_privacy == true) {
    		break;
    	}

    	if ((ctx.fused_privacy == true) && (ctx.privacy_params.gps_privacy == true)) {
    		break;
    	}

        static int gps_size_before = 0;
        int gps_size_now = gps_size_atomic ;
        if (gps_size_before > gps_size_now)
			gps_size_before = 0;

        for(int i = gps_size_before; i < gps_size_now; i++){
            is_gps_data_present = true;
            ctx.fstream_partial_meta_file << endl ;
            ss_meta.str("");
            Gps::gps_metadata_t &val = gps[i];
            ss_meta <<"gps" << ", " << val.gps_data.latitude << ", " << val.gps_data.longitude << ", " \
                    << val.gps_data.altitude << ", " << val.altitudeMSL << ", " << val.gps_data.accuracy << ", " << val.gps_data.bearing << ", " \
                    << val.gps_data.speed << ", " << val.gps_data.timestamp << ", " << val.raw_time_micro << ", " << val.gps_data.valid;
            ctx.fstream_partial_meta_file << ss_meta.str() ;
            ctx.fstream_partial_meta_file << endl ;
            if (i == 0)
                gps_indx_val = val.gps_index;
        }
        gps_size_before = gps_size_now ;

        break;
    }
    /* GPS ends */

    if (hdmaps_mode_enabled == true) {
        /* GPS_PPS starts */
        static int gps_pps_size_before = 0;
        int gps_pps_size_now = gps_pps_size_atomic;

        if (gps_pps_size_before > gps_pps_size_now) {
           gps_pps_size_before = 0;
           is_gps_pps_data_present = false;
        }
        for (int i = gps_pps_size_before; i < gps_pps_size_now; i++) {
            is_gps_pps_data_present = true;
            ctx.fstream_partial_meta_file << endl ;

            ss_meta.str("");
            Gps::gps_pps_data_t &val = gps_pps[i];
            ss_meta << "gps_pps" << ", " << val.pps_index << ", " << val.pps_raw_time << ", " << val.pps_clock_time;

            ctx.fstream_partial_meta_file << ss_meta.str() ;
            ctx.fstream_partial_meta_file << endl ;

            if (i == 0)
                gps_pps_indx_val = val.pps_index;
        }
        gps_pps_size_before = gps_pps_size_now ;
        /* GPS_PPS ends */

        /* GPS_START_IDX, PPS_START_IDX starts */
        static int count = 0;
        if ((is_gps_pps_data_present != false) && (is_gps_data_present != false)) {
            if (onetime == true) {
                if (ctx.session_number == 0) {
                    if (count <= 3)
                        onetime = true;
                    else
                        onetime = false;

                    count++;
                } else {
                    onetime = false;
                }

                ctx.fstream_partial_meta_file << endl ;

                ss_meta.str("");
                ss_meta <<"gps_start_idx" << ", " << gps_indx_val;

                ctx.fstream_partial_meta_file << ss_meta.str() ;
                ctx.fstream_partial_meta_file << endl ;
                ctx.fstream_partial_meta_file << endl ;

                ss_meta.str("");
                ss_meta <<"pps_start_idx" << ", " << gps_pps_indx_val;

                ctx.fstream_partial_meta_file << ss_meta.str() ;
                ctx.fstream_partial_meta_file << endl ;
            }
        }
        /* GPS_START_IDX, PPS_START_IDX ends */

        /* UBLOX starts */
        if (ublox_enabled == true) {
            static int ublox_size_before = 0;
        	int ublox_size_now = ublox_size_atomic;
        	if(ublox_size_before > ublox_size_now){
        	    ublox_size_before = 0;
        	}

        	for(int i = ublox_size_before; i < ublox_size_now; i++){
        	    ctx.fstream_partial_meta_file << endl ;
        		ss_meta.str("");
        		Ublox::ublox_data_t &val = ublox[i];
        		ss_meta <<"ublox" << ", " << val.system_time << ", " << val.clock_time << ", " \
        			    <<  val.Class << ", " << val.id << ", " << val.length << ", " \
        			    << val.payload;

        		ctx.fstream_partial_meta_file << ss_meta.str() ;
        		ctx.fstream_partial_meta_file << endl ;
        	}
        	ublox_size_before = ublox_size_now ;
        }
        /* UBLOX ends */

        static Ublox::ublox_constellation_data_t val = {0};
	    static int curr_session_num = 0;
	    static unsigned  int first_iters = 0;
	    // sometime ot takes iterations to latch the config values, ignore ,utliptle config in csv_to_json if present.
	    if((ctx.session_number == 0 && first_iters <= 5) || ctx.session_number != curr_session_num)
	    {
		    curr_session_num = ctx.session_number;
		    ctx.fstream_partial_meta_file << endl ;
		    ss_meta.str("");
		    if(ctx.session_number == 0) {
			    Ublox::ublox_constellation_data_t &ublox_costell_temp = ublox_constellation[0];
			    val = ublox_costell_temp;
		    }
		    ss_meta <<"ublox_constellation" << ", " << val.enable_GPS << ", " << val.enable_SBAS << ", " \
			    <<  val.enable_Galileo << ", " << val.enable_BeiDou << ", " << val.enable_IMES << ", " \
			    <<  val.enable_QZSS << ", " << val.enable_GLONASS;

		    std::string strr = ss_meta.str();
		    ctx.fstream_partial_meta_file << ss_meta.str() ;
		    ctx.fstream_partial_meta_file << endl ;
		    first_iters++;
	    }

        /* FRAMEINFO starts */
        static int frameinfo_size_before = 0;
        int frameinfo_size_now = frameinfo_size_atomic;

        if (frameinfo_size_before > frameinfo_size_now)
	        frameinfo_size_before = 0;

        for (int i = frameinfo_size_before; i < frameinfo_size_now; i++) {
	        ctx.fstream_partial_meta_file << endl ;

	        ss_meta.str("");
	        FrameInfo::frameinfo_t &val = frameinfo[i];
	        ss_meta << "frameinfo" << ", " << val.raw_time_micro << ", " << val.epoch_time_micro << ", "  <<  val.frame_number;

	        ctx.fstream_partial_meta_file << ss_meta.str() ;
	        ctx.fstream_partial_meta_file << endl ;
        }
        frameinfo_size_before = frameinfo_size_now ;
        /* FRAMEINFO ends */
    }

    /* OBD starts */
    static int obd_size_before = 0;
    int obd_size_now = obd_size_atomic ;
    if(obd_size_before > obd_size_now){
        obd_size_before = 0;
    }
    for(int i = obd_size_before; i < obd_size_now; i++){
        ctx.fstream_partial_meta_file << endl ;
        ss_meta.str("");
        Obd::obd_data_t obd_val = obd[i];
        ss_meta << "obd" << ", "<< obd_val.time <<", " << obd_val.spn_id <<", " << fixed << setprecision(OBD_DATA_PRECISION) << obd_val.value;
        ctx.fstream_partial_meta_file << ss_meta.str() ;
        ctx.fstream_partial_meta_file << endl ;
    }
    obd_size_before = obd_size_now ;
    /* OBD ends */

    if ((can_status_updated != CAN_UNKNOWN_ERROR) && (can_status_updated != CAN_STATUS_OK))
    {
        ss_meta.str("");
        ss_meta << endl << "obd_status" << ", " << can_status_updated;
        ctx.fstream_partial_meta_file << ss_meta.str() << endl;
    }

    /* Audio starts */
    static int audio_size_before = 0;
    int audio_size_now = audio_size_atomic ;
    if(audio_size_before > audio_size_now){
        audio_size_before = 0;
    }
    for(int i = audio_size_before; i < audio_size_now; i++){
        Audio::audio_t audio_val = audio[i];
        ctx.audio->audio_partial_pcm_buff_vec.push_back(audio_val);
    }
    audio_size_before = audio_size_now ;
    /* Audio ends */

    /* USER ALERT starts */
    static int usralert_size_before = 0;
    int usralert_size_now = usralert_size_atomic ;

    if (usralert_size_before > usralert_size_now)
        usralert_size_before = 0;

    for (int i = usralert_size_before; i < usralert_size_now; i++) {
        ctx.fstream_partial_meta_file << endl ;

        ss_meta.str("");
        genmeta_usralert_t &val = usralert[i];
        ss_meta << "usrAlert" << ", " << val.time << ", " << val.btn << ", " << val.source;

        ctx.fstream_partial_meta_file << ss_meta.str() ;
        ctx.fstream_partial_meta_file << endl ;
    }
    usralert_size_before = usralert_size_now ;
    /* USER ALERT ends */

    stringstream ss_p_meta("");
    ss_p_meta << "privacy" << ", " << to_string(ctx.off_duty_privacy) << ", " << to_string(ctx.privacy_params.enhanced_privacy) << ", " << to_string(ctx.fused_privacy) << ", " << to_string(ctx.geofence_privacy);
    ctx.fstream_partial_meta_file << ss_p_meta.str() << endl;

    stringstream ss_i_meta("");
    ss_i_meta << "ignition_status" << ", " << to_string(ctx.crank_level_RT_thread) ;
    ctx.fstream_partial_meta_file << ss_i_meta.str() << endl;

    stringstream ss_ir_meta("");
    ss_ir_meta << "irled_state" << ", " << ctx.ir_led_status;
    ctx.fstream_partial_meta_file << ss_ir_meta.str() << endl;
}

bool meta_buff_t::split_after( uint64_t epoch_time_micro, uint64_t raw_time_micro, meta_buff_t &buff, bool hdmaps_mode_enabled ) {
    int c_accel=0;
    int c_imu_temperature=0;
    int c_gyro=0;
    int c_magneto=0;
    int c_gps=0;
    int c_gps_pps = 0;
    int c_obd=0;
    int c_freport=0;
    int c_ireport=0;
    int c_audio=0;
    int c_ublox=0;
    int c_frameinfo=0;
    int log_count = 0;

    LOG_I(TAG, "%s: raw_time_micro = %lld", __func__, raw_time_micro);
    //Push extra values out to buff
    while( !accel.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Imu::val_t &val = accel.back();
        if (val.raw_time > raw_time_micro) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Accel:Record %ld %ld ", val.raw_time, raw_time_micro);
                log_count++;
            }
            buff.push_front(Imu::IMU_ACCEL, val);
            accel.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_accel++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    accel_size_atomic = 0 ;

    log_count = 0;
    if ((hdmaps_mode_enabled == true) && (ctx.imu_data == true)) {
        while( !imu_temperature.empty() ) {
            pthread_mutex_lock(&meta_buff_mutex);
            Imu::val_t &val = imu_temperature.back();
            if( val.raw_time > raw_time_micro ) {
                if (log_count < NUM_LOGS_CAP )
                {
                    LOG_I(TAG, "Temperature:Record %ld %ld", val.raw_time, raw_time_micro );
                    log_count++;
                }
                buff.push_front(Imu::IMU_TEMP, val);
                imu_temperature.pop_back();
                pthread_mutex_unlock(&meta_buff_mutex);
                c_imu_temperature++;
            } else {
                pthread_mutex_unlock(&meta_buff_mutex);
                break;
            }
        }
        imu_temp_size_atomic = 0;
        log_count = 0;
    }

    while( !gyro.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Imu::val_t &val = gyro.back();
        if (val.raw_time > raw_time_micro) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Gyro:Record %ld %ld ", val.raw_time, raw_time_micro);
                log_count++;
            }
            buff.push_front(Imu::IMU_GYRO, val);
            gyro.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_gyro++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    gyro_size_atomic = 0 ;

    log_count = 0;
    while( !magneto.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Imu::val_t &val = magneto.back();
        if (val.raw_time > raw_time_micro) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Magneto:Record %ld %ld ", val.raw_time, raw_time_micro);
                log_count++;
            }
            buff.push_front(Imu::IMU_MAGNETO, val);
            magneto.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_magneto++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }

    log_count = 0;
    while( !gps.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Gps::gps_metadata_t &val = gps.back();
        if( val.raw_time_micro > raw_time_micro ) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Gps:Record %ld %ld ", val.raw_time_micro, raw_time_micro);
                log_count++;
            }
            buff.push_front(val);
            gps.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_gps++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    gps_size_atomic = 0 ;

    log_count = 0;
    while( !gps_pps.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Gps::gps_pps_data_t &val = gps_pps.back();

        if( val.pps_raw_time > raw_time_micro ) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "GPS_PPS:Record %ld %ld", val.pps_raw_time, raw_time_micro);
                log_count++;
            }
            buff.push_front(val);
            gps_pps.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_gps_pps++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    gps_pps_size_atomic = 0 ;

    log_count = 0;
    while( !ublox.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Ublox::ublox_data_t &val = ublox.back();
        if( val.system_time > raw_time_micro ) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Ublox:Record %ld %ld", val.system_time, raw_time_micro);
                log_count++;
            }
            buff.push_front(val);
            ublox.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_ublox++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    ublox_size_atomic = 0 ;

    log_count = 0;
    while( !obd.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Obd::obd_data_t &val = obd.back();
        if( val.time > (epoch_time_micro / 1000) ) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Obd:Record %ld %ld ", val.time, epoch_time_micro / 1000);
                log_count++;
            }
            buff.push_front(val);
            obd.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_obd++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    obd_size_atomic = 0 ;

    log_count = 0;
    while( !freport.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Obd::fr_data_t &val = freport.back();
        pthread_mutex_unlock(&meta_buff_mutex);
        break;
    }
    freport_size_atomic = 0 ;

    log_count = 0;
    while( !ireport.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        Obd::ir_data_t &val = ireport.back();
        pthread_mutex_unlock(&meta_buff_mutex);
        break;
    }
    ireport_size_atomic = 0 ;

    log_count = 0;
    while( !frameinfo.empty() ) {
        pthread_mutex_lock(&meta_buff_mutex);
        FrameInfo::frameinfo_t &val = frameinfo.back();
        //This is highly unlikely because we end up here after last frame of one session is written.
        if(val.raw_time_micro > raw_time_micro) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_I(TAG, "Frameinfo:Record %ld %ld", val.raw_time_micro, raw_time_micro);
                log_count++;
            }
            buff.push_front(val);
            frameinfo.pop_back();
            pthread_mutex_unlock(&meta_buff_mutex);
            c_frameinfo++;
        } else {
            pthread_mutex_unlock(&meta_buff_mutex);
            break;
        }
    }
    frameinfo_size_atomic = 0;

    log_count = 0;
    LOG_I(TAG, "Audio:size %d", audio.size());
    while( !audio.empty() ) {
        pthread_mutex_lock(&audio_mutex);
        Audio::audio_t &val = audio.back();
        if( val.time_stamp > (epoch_time_micro / 1000) ) {
            if (log_count < NUM_LOGS_CAP )
            {
                LOG_D(TAG, "Audio:Record %ld %ld ", val.time_stamp, epoch_time_micro / 1000);
                log_count++;
            }
            buff.push_front(val);
            audio.pop_back();
            pthread_mutex_unlock(&audio_mutex);
            c_audio++;
        } else {
            pthread_mutex_unlock(&audio_mutex);
            break;
        }
    }
    audio_size_atomic = 0 ;


    if(!accel.empty()) {
        LOG_I(TAG, "Meta diff: %ld", (accel.back().raw_time - accel.front().raw_time));
    }
    LOG_I(TAG, "Split at %ld move cnt:: accel: %d, gyro: %d, magneto: %d, gps: %d, obd: %di, framedata: %d, PPS: %d, Ublox: %d",
            time, c_accel, c_gyro, c_magneto, c_gps, c_obd, c_frameinfo, c_gps_pps, c_ublox);

    return true;

}

bool meta_buff_t::fill_genmeta( Genmeta &meta, bool flipflop ) {
    LOG_I(TAG, "fill_genmeta              .............           "   );
    for( deque<Imu::val_t>::iterator iter=accel.begin(), end=accel.end();
                iter != end; iter++ ) {
        meta.push_data(Imu::IMU_ACCEL, *iter, flipflop);
    }

    if ((ctx.hdmaps_mode_enabled == true) && (ctx.imu_data == true)) {
        for( deque<Imu::val_t>::iterator iter=imu_temperature.begin(), end=imu_temperature.end();
                    iter != end; iter++ ) {
            meta.push_data(Imu::IMU_TEMP, *iter, flipflop);
        }
    }

    for( deque<Imu::val_t>::iterator iter=gyro.begin(), end=gyro.end();
                iter != end; iter++ ) {
        meta.push_data(Imu::IMU_GYRO, *iter, flipflop);
    }

    for( deque<Imu::val_t>::iterator iter=magneto.begin(), end=magneto.end();
                iter != end; iter++ ) {
        meta.push_data(Imu::IMU_MAGNETO, *iter, flipflop);
    }

    for( deque<Obd::obd_data_t>::iterator iter=obd.begin(), end=obd.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    for( deque<Obd::fr_data_t>::iterator iter=freport.begin(), end=freport.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    for( deque<Obd::ir_data_t>::iterator iter=ireport.begin(), end=ireport.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    for( deque<Gps::gps_metadata_t>::iterator iter=gps.begin(), end=gps.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    LOG_I(TAG, "ignition Size: %d" , ignition_status_records.size());
    for( vector<ignition_status_records_t>::iterator iter=ignition_status_records.begin(), end=ignition_status_records.end();
                iter != end; iter++ ) {
        meta.push_data_ignition(*iter, flipflop);
    }

    for( deque<Gps::gps_pps_data_t>::iterator iter=gps_pps.begin(), end=gps_pps.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    for( deque<Ublox::ublox_data_t>::iterator iter=ublox.begin(), end=ublox.end();
                iter != end; iter++ ) {
        meta.push_data(*iter, flipflop);
    }

    for(deque<Ublox::ublox_constellation_data_t>::iterator iter=ublox_constellation.begin(), end=ublox_constellation.end();
               iter!=end; iter++) {
        meta.push_data(*iter, flipflop);
    }

    if (ctx.hdmaps_mode_enabled == true) {
        for( deque<FrameInfo::frameinfo_t>::iterator iter=frameinfo.begin(), end=frameinfo.end();
                    iter != end; iter++ ) {
            meta.push_data(*iter, flipflop);
        }
    }

    ignition_status_records.clear();

    stringstream ss1;
    if (!gps.empty())
    {
        ss1 << gps.front().gps_data.timestamp;
        meta.update_genmeta_header("gps_start_time", ss1.str(), flipflop);
    }
    else
    {
        meta.update_genmeta_header("gps_start_time", "0", flipflop);
    }

    stringstream ss2;
    if (!gps.empty())
    {
        ss2 << gps.back().gps_data.timestamp;
        meta.update_genmeta_header("gps_end_time", ss2.str(), flipflop);
    }
    else
    {
        meta.update_genmeta_header("gps_end_time", "0", flipflop);
    }
    return true;
}


bool meta_buff_t::fill_audio(string pcm_file) {
    LOG_I(TAG, "creating audio file...");
    FILE *fp;
    fp = fopen( pcm_file.c_str() , "wb" );
    if(fp == NULL) {
        LOG_E(TAG, "Could not open audio file: %s", pcm_file.c_str());
        return false;
    }

    while( !audio.empty() ) {
        pthread_mutex_lock(&audio_mutex);
        Audio::audio_t &val = audio.front();
        if( val.size != fwrite((const void *)&(val.data), 1, val.size, fp)) {
            LOG_E(TAG, "audio pcm file write failed");
        }
        audio.pop_front();
        pthread_mutex_unlock(&audio_mutex);
    }
    fclose(fp);
    return true;

}
