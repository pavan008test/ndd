#ifndef FRAMEINFO_H
#define FRAMEINFO_H
class FrameInfo {
    public:
        struct frameinfo_t {
            uint64_t raw_time_micro;
            uint64_t epoch_time_micro;
            uint64_t frame_number;
        };
};
#endif
