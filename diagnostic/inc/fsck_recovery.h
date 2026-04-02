static const int FSCK_COMMAND_TASK_TIMEOUT = 120;
static const int FSCK_CORRECTION_COMMAND_TASK_TIMEOUT = 500;
static const int FSCK_CORRECTION_AND_RESIZE_COMMAND_TASK_TIMEOUT = 900;

#define SIZE_3GB_FROM_KB 3*1024*1024
#define ALIGN_64K 64*1024
#define align_down(num, align) \
    (((num) - ((align) - 1)) & ~((align) - 1))

#define E2FSCK_TIMEOUT 100
#define RESIZE_TIMEOUT 600
#define FSCK_TIMEOUT 180

bool get_resize_number(char *total_data);
void run_fsck_correction_command();
void run_fsck_correction_and_resize_command();
void run_fsck_command();
void fork_fsck_image_correction_run_process(bool runResize);
void fork_fsck_run_process();

