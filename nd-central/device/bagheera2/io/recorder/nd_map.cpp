/* Copyright (C) 2018 NetraDyne, Inc - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Karthik Dumpala <karthik.dumpala@netradyne.com>, August 2018
 */

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <map>
#include <utility>
#include <log.h>
#include <nd_time.h>
#include<tuple>
#include "nd_map.h"

using namespace std;

#define TAG "ND_MAP"

static const int MAX_MAP_SIZE = 150;
pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static map <uint64_t, tuple <uint64_t, uint64_t, int> > nd_map;

// raw_to_epoch_map will store epoch ts corresponding to a monotonic raw ts.
// It is easier to keep a seperate map of monotonic -> epoch 
// rather than modifying existing map to store a tuple or struct of 3 values 
// since this requirement came way later and we don't want to change existing
// tried and tested logic.
// Also, this needs to be done in shorter time. So no enough testing time is available.
static map <uint64_t, pair <uint64_t, int> > raw_to_epoch_map;

bool monotonic_raw_ns_to_epoch_ns (uint64_t raw_time_ns, uint64_t *epoch_ns) {
    uint64_t cur_raw_ns = get_system_monotonic_time_ns ();
    uint64_t cur_epoch_ns = get_system_time_ns ();
    int64_t raw_diff_ns = 0;

    //LOG_I(TAG, "monotonic_raw = %lld, monotonic = %lld", get_system_monotonic_time_ns(), get_system_monotonic_time_ns_ntp());
    if (cur_raw_ns < raw_time_ns) {
        LOG_E (TAG, "cur_raw_ns<raw_time_ns, %llu %llu", cur_raw_ns, raw_time_ns);
        epoch_ns = 0;
        return false;
    }

    raw_diff_ns = cur_raw_ns - raw_time_ns;
    LOG_D (TAG, "RAW diff ns : %lld", raw_diff_ns);
    *epoch_ns = cur_epoch_ns - raw_diff_ns;
    return true;
}

/*
    This functions returns the value from map if key is found
    Returns -1 if map is empty or key not found
    Returns 0 on success
*/
int get_timestamps_from_pts(uint64_t pts, uint64_t *raw_time_ns, uint64_t *epoch_time_ns, uint64_t *frame_num) {
    
    map <uint64_t, tuple <uint64_t, uint64_t, int> >::iterator itr;
    int ret = 0;

    pthread_mutex_lock(&q_lock);
    // Check if size is empty
    if(nd_map.empty()) {
        LOG_E(TAG, "map is empty");
        pthread_mutex_unlock(&q_lock);
        return -1;
    }

    itr = nd_map.find (pts);
    if (itr == nd_map.end()) {
        LOG_E(TAG, "pts value %llu not found in map", pts);
        pthread_mutex_unlock(&q_lock);
        return -1;
    }
    
    //*raw_time_ns = itr->second.first;
    //int ref_count = itr->second.second;
    int ref_count = 0;
    tie(*raw_time_ns, *frame_num, ref_count) = itr->second;
    ref_count--;
    if (ref_count == 0) {
        nd_map.erase (itr);
    }
    else {
        nd_map [pts] = make_tuple (*raw_time_ns, *frame_num, ref_count);
    }

    //mono epoch map
    map <uint64_t, pair <uint64_t, int> >::iterator raw_epoch_iter;
    if (raw_to_epoch_map.empty()) {
        LOG_E(TAG, "map is empty");
        if (monotonic_raw_ns_to_epoch_ns (*raw_time_ns, epoch_time_ns) == false) {
            *epoch_time_ns = 0;
        }
        return -1;
    }
    raw_epoch_iter = raw_to_epoch_map.find (*raw_time_ns);
    if (raw_epoch_iter == raw_to_epoch_map.end()) {
        LOG_E (TAG, "value not found in raw-epoch map, %llu", *raw_time_ns);
        if (monotonic_raw_ns_to_epoch_ns (*raw_time_ns, epoch_time_ns) == false) {
            *epoch_time_ns = 0;
        }
        ret = -1;
    }
    *epoch_time_ns = raw_epoch_iter->second.first;
    int raw_epoch_refcount = raw_epoch_iter->second.second;
    raw_epoch_refcount--;
    if (raw_epoch_refcount == 0) {
        raw_to_epoch_map.erase ( raw_epoch_iter);
    }
    else {
        raw_to_epoch_map [*raw_time_ns] = make_pair (*epoch_time_ns, raw_epoch_refcount);
    }

    pthread_mutex_unlock(&q_lock);
    return ret;
}

/*
   This function inserts the pts and the timestamp to the map
   Returns -1 if map size exceeds some limit
   Returns 0 for success
*/
int insert_pts_and_timestamp(uint64_t pts, uint64_t raw_time_ns, uint64_t frame_num, short ref_count_init)
{
    int ref_count = ref_count_init;
    int ret = 0;
    map <uint64_t, tuple <uint64_t, uint64_t, int> >::iterator iter;

    LOG_D(TAG, "Insert happening here for pts: %lld, dts: %lld", pts, raw_time_ns);

    pthread_mutex_lock(&q_lock);

    uint64_t epoch_time_ns = 0;
    if (monotonic_raw_ns_to_epoch_ns (raw_time_ns, &epoch_time_ns)) {
        //LOG_I (TAG, "Epoch time corresponding to raw time: %llu - %llu", raw_time_ns, epoch_time_ns);
    }
    else {
        epoch_time_ns = get_system_time_ns();
    }

    if(nd_map.size() >= MAX_MAP_SIZE) {

        LOG_E(TAG, "map size exceeded limit. This is unexpected");
        LOG_I (TAG, "cleaning up entries with refcount 1");
        int count = 0;

        iter = nd_map.begin();
        while (iter != nd_map.end()) {
            //if (iter->second.second == 1) {
            if (get<1>(iter->second) == 1) {
		LOG_I(TAG, "!!! clearing for PTS: %lld", iter->first);
                count++;
                iter=nd_map.erase (iter);
            }
            else {
                iter++;
            }
        }
        LOG_I (TAG, "Cleard %d items from map", count);
        if (count == 0) {
            LOG_E (TAG, "Strange!! MAP is full with all entries with ref_count 2. Clearing");
            nd_map.clear();
        }
    }

    iter = nd_map.find (pts);
    if (iter != nd_map.end()) {
        LOG_E (TAG, "Replacing already existing timestamp value. pts: %llu, ts: %llu ref_count: %d with new_ts:%llu", 
                pts,  get<0>(iter->second), get<1>(iter->second), raw_time_ns);
        ret = -1;
    }
    nd_map [pts] = make_tuple (raw_time_ns, frame_num, ref_count);

    // monotonic -> epoch map
    int raw_epoch_refcount = ref_count_init;
    map <uint64_t, pair <uint64_t, int> >::iterator raw_epoch_iter;

    if (raw_to_epoch_map.size() >= MAX_MAP_SIZE) {
        LOG_E(TAG, "raw_epoch map size exceeded limit. This is unexpected");
        LOG_I (TAG, "cleaning up entries with refcount 1");
        int count = 0;

        raw_epoch_iter = raw_to_epoch_map.begin();
        while (raw_epoch_iter != raw_to_epoch_map.end()) {
            if (raw_epoch_iter->second.second == 1) {
                count++;
                raw_epoch_iter=raw_to_epoch_map.erase (raw_epoch_iter);
            }
            else {
                raw_epoch_iter++;
            }
        }
        LOG_I (TAG, "Cleared %d items from map", count);
        if (count == 0) {
            LOG_E (TAG, "Strange!! raw_epoch MAP is full with all entries with ref_count 2. Clearing");
            raw_to_epoch_map.clear();
        }
    }
    raw_epoch_iter = raw_to_epoch_map.find (raw_time_ns);
    if (raw_epoch_iter != raw_to_epoch_map.end()) {
        LOG_E (TAG, "Replacing already existing timestamp value from raw_epoch map. raw: %llu, epoch: %llu ref_count: %d with new_epoch:%llu", 
                raw_time_ns,  raw_epoch_iter->second.first, raw_epoch_iter->second.second, epoch_time_ns);
        ret = -1;
    }

    raw_to_epoch_map[raw_time_ns] = make_pair (epoch_time_ns, raw_epoch_refcount);
    pthread_mutex_unlock(&q_lock);
    return ret;
}

void clear_map()
{
	pthread_mutex_lock(&q_lock);
	nd_map.clear();
	raw_to_epoch_map.clear();
	pthread_mutex_unlock(&q_lock);
}
