#ifndef GM_S_LITTLE_HELPERS_FILE_HELPERS_H
#define GM_S_LITTLE_HELPERS_FILE_HELPERS_H
 
#ifdef __cplusplus
extern "C" {
#endif
 
typedef struct binary_data_t binary_data_t;
struct binary_data_t {
  long size;
  void *data;
};
 
binary_data_t * read_file(const char *filename);
 
#ifdef __cplusplus
}
#endif
 
#endif //GM_S_LITTLE_HELPERS_FILE_HELPERS_H