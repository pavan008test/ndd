#ifndef ND_MAP_H
#define ND_MAP_H
int get_timestamps_from_pts(uint64_t pts, uint64_t *raw_time_ns, uint64_t *epoch_time_ns, uint64_t *frame_num);
int insert_pts_and_timestamp(uint64_t pts, uint64_t timestamp, uint64_t frame_num, short ref_count);
void clear_map();
#endif
