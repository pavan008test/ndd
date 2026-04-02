/* Copyright (C) 2017 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Arun V <arun.vj@netradyne.com>, Dec 2017
 */

#ifndef METADATA_BUFFER_H
#define METADATA_BUFFER_H

#include <deque>
#include <pthread.h>

#include <imu.h>
#include <gps.h>
#include <obd.h>
#include <log.h>
#include <genmeta.h>
#include <ublox.h>
#include <frameinfo.h>
#include <audio_record.h>

using namespace std;

class meta_buff_t {
    public:
        meta_buff_t();

        //Note:: all values should be pushed in temporal order
        //Push accel, gyro or magneto readings
        bool push(Imu::imu_sensor_t imu, Imu::val_t &val, bool hdmaps_enabled);
        //Push GPS data
        bool push( Gps::gps_metadata_t &val);
        //Push GPS PPS data
        bool push( Gps::gps_pps_data_t &val);
        //push OBD data
        bool push( Obd::obd_data_t &val);
        //push fuel report data
        bool push( Obd::fr_data_t &val);
        //push idling report data
        bool push( Obd::ir_data_t &val);
        //Push Ublox data
        bool push( Ublox::ublox_data_t &val);
        //Push Ublox constellation data
        bool push( Ublox::ublox_constellation_data_t &val);
        //push Audio data
        bool push( Audio::audio_t &val);
        // push frameinfo data
        bool push( FrameInfo::frameinfo_t &val);
        // push user alert
        bool push( genmeta_usralert_t &val);

        //Split the value of elements after 'time' into 'buff'
        //Warning:: 'buff' will be cleared
        bool split_after( uint64_t epoch_time_micro, uint64_t raw_time_micro, meta_buff_t &buff, bool hdmaps_mode_enabled );
        //Push data into metadata object
        bool fill_genmeta( Genmeta &meta, bool flipflop );
        //Function to clear all stored values
        bool clear( );
        // Function to fill pcm audio
        bool fill_audio(string audio_file);
        void pick_last_second_metadata(bool hdmaps_mode_enabled, bool imu_data, bool ublox_enabled);
        void add_ignition_status_records(bool status, int64_t time);
 
    private:
        //Push accel, gyro or magneto readings to the front
        bool push_front(Imu::imu_sensor_t imu, Imu::val_t &val );
        //Push GPS data to the front
        bool push_front( Gps::gps_metadata_t &val);
        bool push_front( Gps::gps_pps_data_t &val);
        bool push_front( Obd::obd_data_t &val);
        bool push_front( Obd::fr_data_t &val);
        bool push_front( Obd::ir_data_t &val);
        bool push_front( Audio::audio_t &val);
        bool push_front( FrameInfo::frameinfo_t &val);
        bool push_front( Ublox::ublox_data_t &val);
        bool push_front( Ublox::ublox_constellation_data_t &val);
        deque<Imu::val_t> accel;
        deque<Imu::val_t> imu_temperature;
        deque<Imu::val_t> gyro;
        deque<Imu::val_t> magneto;
        deque<Gps::gps_metadata_t> gps;
        deque<Gps::gps_pps_data_t> gps_pps;
        deque<Obd::obd_data_t> obd;
        deque<Obd::fr_data_t> freport;
        deque<Obd::ir_data_t> ireport;
        deque<Audio::audio_t> audio;
        
        deque<FrameInfo::frameinfo_t> frameinfo;
        deque<Ublox::ublox_data_t> ublox;

        deque<genmeta_usralert_t> usralert;

        deque<Ublox::ublox_constellation_data_t> ublox_constellation;
        vector<ignition_status_records_t> ignition_status_records;

        //Mutex for concurrency protection
        pthread_mutex_t meta_buff_mutex;
        pthread_mutex_t audio_mutex;
};

#endif // METADATA_BUFFER_H

