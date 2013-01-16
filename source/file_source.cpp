
#include <string.h>

#include "ins.h"
#include "file_source.h"

static const int BUFFER_SIZE = (1 << 21);
static const int AVG_READ_SIZE = (BUFFER_SIZE >> 1);

#ifdef WIN32
static
uint64_t file_size(const char *filename)
{
    WIN32_FILE_ATTRIBUTE_DATA fad = { 0 };

    if (!::GetFileAttributesExA(filename, ::GetFileExInfoStandard, &fad))
        return -1;
    return (static_cast<uint64_t>(fad.nFileSizeHigh)
        << (sizeof(fad.nFileSizeLow) * 8)) + fad.nFileSizeLow;
}
#else
static
uint64_t file_size(const char *sFileName)
{
    struct stat buf;
    if(stat(sFileName, &buf) != 0)
        return(-1);
    return (buf.st_size);
}
#endif // WIN32

file_source::file_source()
: m_file(NULL)
, m_file_size(0)
{
    pthread_mutex_init(&m_mutex, NULL);
}

file_source::~file_source()
{
    close();
    pthread_mutex_destroy(&m_mutex);
    if (m_open_data)
        delete m_open_data;
}

bool file_source::open(void* ctx)
{
    // 保存ctx.
    m_open_data =(open_file_data*)ctx;

    // 打开文件.
    if (access(m_open_data->filename.c_str(), 0) == -1)
        return false;

    // 获得文件大小.
    m_file_size = file_size(m_open_data->filename.c_str());

    // 打开文件.
    m_file = fopen(m_open_data->filename.c_str(), "rb");
    if (!m_file)
        return false;
    // 设置缓冲区大小.
    setvbuf(m_file, NULL, _IOFBF, BUFFER_SIZE);

    return true;
}

bool file_source::read_data(char* data, uint64_t offset, size_t size, size_t& read_size)
{
    static char read_buffer[AVG_READ_SIZE];

    // 根据参数加锁.
    if (m_open_data->is_multithread)
        pthread_mutex_lock(&m_mutex);

    read_size = 0;

    // 读取数据越界.
    if (offset >= m_file_size)
    {
        if (m_open_data->is_multithread)
            pthread_mutex_unlock(&m_mutex);
        return false;
    }

    if (!m_file)
    {
        if (m_open_data->is_multithread)
            pthread_mutex_unlock(&m_mutex);
        return true;
    }

    // 从文件中读取数据.
    // 移到偏移位置.
    fseek(m_file, offset, SEEK_SET);
    // 开始读取数据.
    read_size = fread(data, 1, size, m_file);

    if (m_open_data->is_multithread)
        pthread_mutex_unlock(&m_mutex);

    return true;
}

void file_source::close()
{
    if (m_file)
    {
        fclose(m_file);
        m_file = NULL;
    }
}
