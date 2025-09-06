// disk.h
typedef struct GuppyImage GuppyImage;
GuppyImage* guppy_create_image(const char* path, uint64_t size_bytes, bool sparse);
int guppy_close_image(GuppyImage* img);
